#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "bsp_uart.h"
#include "app_config.h"
#include "app_events.h"
#include "face_auth.h"
#include "k230_protocol.h"
#include <stdio.h>

typedef struct
{
    QueueHandle_t queue;
    FaceAuth_t auth;
    uint32_t rx_errors;
    uint32_t bad_payloads;
    uint32_t queue_drops;
    uint32_t last_log_ms[8];
    uint16_t logged_mask;
} FaceContext_t;

/** 完整且 CRC 正确的帧才到达这里；ISR 不调用本函数。 */
static void Face_OnFrame(const K230_Frame_t *frame, void *argument)
{
    FaceContext_t *context = (FaceContext_t *)argument;
    AuthEvent_t event;
    FaceAction_t action;
    uint32_t now;
    uint16_t log_bit;

    /* 解析期间若发生溢出，也不能基于不连续的数据产生认证。 */
    if (BSP_UART2_RxErrors() != context->rx_errors)
    {
        FaceAuth_OnTransportLoss(&context->auth);
        return;
    }
    action = FaceAuth_Process(&context->auth, frame, &event);
    if (action == FACE_ACTION_IGNORED) { return; }
    if (action == FACE_ACTION_BAD_PAYLOAD)
    {
        ++context->bad_payloads;
        return;
    }

    /* STATUS/PONG 等合法连发也限频；先完成状态处理，日志不改变语义。 */
    if (action != FACE_ACTION_AUTH)
    {
        now = HAL_GetTick();
        log_bit = (uint16_t)(1U << (unsigned int)action);
        if ((context->logged_mask & log_bit) != 0U &&
            (uint32_t)(now - context->last_log_ms[action]) < APP_FACE_NORMAL_LOG_MS)
        {
            return;
        }
        context->logged_mask |= log_bit;
        context->last_log_ms[action] = now;
    }

    printf("K230 frame: CMD=0x%02X SEQ=%u LEN=%u\r\n",
           frame->cmd, frame->seq, frame->len);
    switch (action)
    {
    case FACE_ACTION_AUTH:
        printf("Face user=%u similarity=%u.%04u\r\n", event.user_id,
               event.similarity_q / 10000U, event.similarity_q % 10000U);
        /* 不在接收任务中等待队列，防止后续串口帧积压。 */
        if (xQueueSend(context->queue, &event, 0U) == pdPASS)
        {
            printf("Face Access %s; auth event sent\r\n",
                   event.result == AUTH_GRANTED ? "Granted" : "Denied");
        }
        else
        {
            ++context->queue_drops;
            printf("Face auth queue full; leave before retry\r\n");
        }
        break;
    case FACE_ACTION_REMOVED:
        printf("Face removed\r\n");
        break;
    case FACE_ACTION_UNKNOWN:
        printf("Face unknown reason=%u; no access event\r\n", frame->data[0]);
        break;
    case FACE_ACTION_READY:
        printf("K230 MODULE_READY api=%u.%u dim=%u threshold_q=%u\r\n",
               frame->data[0], frame->data[1], K230_ReadLE16(frame->data + 2U),
               K230_ReadLE16(frame->data + 4U));
        break;
    case FACE_ACTION_ERROR:
        printf("K230 ERROR code=0x%04X detail=%u; wait NO_FACE\r\n",
               K230_ReadLE16(frame->data), K230_ReadLE16(frame->data + 2U));
        break;
    case FACE_ACTION_STATUS:
        /* 历史状态只打印；即使 last_event=3 / user=1，也不发送 authQueue。 */
        printf("K230 STATUS state=%u last_event=%u last_user=%u (history only)\r\n",
               frame->data[0], frame->data[1], K230_ReadLE16(frame->data + 2U));
        break;
    case FACE_ACTION_PONG:
        printf("K230 PONG\r\n");
        break;
    default:
        break;
    }
}

/** 启动时查询已在运行的模块；回包仅作诊断，不用于补发认证。 */
static void Face_QueryModule(void)
{
    uint8_t frame[K230_FRAME_OVERHEAD];
    size_t length;
    length = K230_EncodeFrame(K230_CMD_PING, 0U, NULL, 0U, frame, sizeof(frame));
    (void)BSP_UART2_Write(frame, (uint16_t)length);
    length = K230_EncodeFrame(K230_CMD_GET_STATUS, 1U, NULL, 0U, frame, sizeof(frame));
    (void)BSP_UART2_Write(frame, (uint16_t)length);
}

/** USART2 字节流 -> V1 parser -> STM32 权限表 -> authQueue。 */
void FaceTask(void *argument)
{
    /* 静态存储减少任务栈占用；本工程只创建一个 FaceTask。 */
    static K230_Parser_t parser;
    static FaceContext_t context;
    uint8_t bytes[APP_FACE_READ_CHUNK];
    uint16_t count;
    uint16_t budget;
    uint32_t now;
    uint32_t last_diagnostic = 0U;
    uint32_t previous_errors = 0U;
    uint32_t total_errors;

    context.queue = (QueueHandle_t)argument;
    configASSERT(context.queue != NULL);
    FaceAuth_Init(&context.auth);
    K230_ParserInit(&parser, Face_OnFrame, &context);
    BSP_UART2_StartBufferedRx();
    context.rx_errors = BSP_UART2_RxErrors();
    printf("FaceTask V1 started: USART2 115200 8N1\r\n");
    Face_QueryModule();

    for (;;)
    {
        budget = APP_FACE_READ_BUDGET;
        now = HAL_GetTick();
        do
        {
            count = BSP_UART2_ReadBuffered(bytes, sizeof(bytes));
            if (BSP_UART2_RxErrors() != context.rx_errors)
            {
                FaceAuth_OnTransportLoss(&context.auth);
                K230_ParserReset(&parser);
                BSP_UART2_DiscardBuffered();
                context.rx_errors = BSP_UART2_RxErrors();
                break;
            }
            if (count == 0U) { break; }
            (void)K230_ParserFeed(&parser, bytes, count, now);
            budget = (uint16_t)(budget - count);
        } while (budget >= sizeof(bytes));
        (void)K230_ParserExpire(&parser, now);

        /* 只在计数变化时限频输出；不逐字节打印。 */
        total_errors = parser.stats.crc_errors + parser.stats.invalid_lengths +
            parser.stats.unsupported_versions + parser.stats.partial_timeouts +
            context.bad_payloads + context.queue_drops + context.rx_errors;
        if (total_errors != previous_errors &&
            (uint32_t)(now - last_diagnostic) >= APP_FACE_DIAGNOSTIC_MS)
        {
            printf("K230 errors: crc=%lu len=%lu ver=%lu timeout=%lu payload=%lu rx=%lu queue=%lu\r\n",
                (unsigned long)parser.stats.crc_errors,
                (unsigned long)parser.stats.invalid_lengths,
                (unsigned long)parser.stats.unsupported_versions,
                (unsigned long)parser.stats.partial_timeouts,
                (unsigned long)context.bad_payloads,
                (unsigned long)context.rx_errors,
                (unsigned long)context.queue_drops);
            previous_errors = total_errors;
            last_diagnostic = now;
        }
        vTaskDelay(pdMS_TO_TICKS(APP_FACE_POLL_MS));
    }
}
