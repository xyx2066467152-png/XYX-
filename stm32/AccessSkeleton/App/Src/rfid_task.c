#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "rc522.h"
#include "app_events.h"
#include <stdio.h>
#include <string.h>


typedef enum
{
    RFID_STATE_WAIT_CARD = 0,   /* 等待新卡 */
    RFID_STATE_WAIT_REMOVE      /* 已处理卡片，等待卡离开 */
} RfidState_t;

/*
 * 第一张授权卡UID。
 * 后续可以扩展成：authorized_cards[][4]
 * 甚至存到W25Q128。
 * 当前先用固定UID把完整门禁流程跑通。
 */
#include "rfid_authorized_uid.h"

static const uint8_t authorized_uid[4] =
{
    AUTHORIZED_RFID_UID_BYTES
};

/**
 * @brief 根据 RFID UID 生成认证事件并发送给 AccessTask
 * @param authQueue 认证事件队列
 * @param uid       RC522读取到的4字节UID
 * @说明
 * 该函数只负责：
 * 1. 判断UID是否属于授权卡
 * 2. 构造 AuthEvent_t
 * 3. 发送到 authQueue
 * 不直接控制舵机，门锁动作由 AccessTask 负责。
 */
static void RFID_SendAuthEvent(
    QueueHandle_t authQueue,
    const uint8_t *uid);
/**
 * @brief RFID检测任务
 * @职责
 *   1. 检测卡片
 *   2. 读取UID
 *   3. 判断UID是否授权
 *   4. 发送AuthEven
 * @注意
 *   本任务不直接控制舵机。
 *   开不开门由AccessTask决定。
 */
void RfidTask(void *argument)
{
    QueueHandle_t authQueue = (QueueHandle_t)argument;

    RfidState_t state = RFID_STATE_WAIT_CARD;

    uint8_t uid[4];
    uint8_t no_card_count = 0U;

    for (;;)
    {
        switch (state)
{
    /* =====================================================
     * 状态1：等待新卡
     * ===================================================== */
    case RFID_STATE_WAIT_CARD:

        /*
         * 检测到卡并成功读取UID后，
         * 只产生一次认证事件。
         */
        if (RC522_Request() && RC522_ReadUID(uid))
        {
            RFID_SendAuthEvent(authQueue, uid);

            /*
             * 当前卡已经处理过。
             * 接下来必须等它离开，不能再次认证。
             */
            no_card_count = 0U;
            state = RFID_STATE_WAIT_REMOVE;
        }

        break;


    /* =====================================================
     * 状态2：已经处理过卡，等待卡离开
     * ===================================================== */
    case RFID_STATE_WAIT_REMOVE:

        /*
         * 此时不再读取UID，
         * 只检测感应区域内还有没有卡。
         */
        if (RC522_IsCardPresent())
        {
            /*
             * 卡还在：
             * 继续保持 WAIT_REMOVE 状态。
             */
            no_card_count = 0U;
        }
        else
        {
            /*
             * 一次检测不到不能马上认为卡离开，
             * 连续多次无卡才确认离开。
             */
            no_card_count++;

            if (no_card_count >= 6U)
            {
                no_card_count = 0U;

                /*
                 * 卡已真正离开。
                 * 下一次重新放卡才允许再次认证。
                 */
                state = RFID_STATE_WAIT_CARD;

                printf("Card removed, RFID ready\r\n");
            }
        }

        break;


    default:

        /*
         * 状态异常时回到安全的等待状态。
         */
        no_card_count = 0U;
        state = RFID_STATE_WAIT_CARD;

        break;
}

        vTaskDelay(pdMS_TO_TICKS(50U));
    }
}
/**
 * @brief 判断 RFID UID 并发送认证结果
 *
 * @param authQueue 发送认证结果使用的 FreeRTOS Queue
 * @param uid       当前检测到的4字节卡片UID
 */
static void RFID_SendAuthEvent(
    QueueHandle_t authQueue,
    const uint8_t *uid)
{
    AuthEvent_t event;

    /* 统一事件新增人脸字段，RFID 事件完整初始化，原卡片状态机不变。 */
    memset(&event, 0, sizeof(event));

    /*
     * 保存认证来源。
     * 后续K230D会使用 AUTH_SOURCE_FACE，
     * 因此AccessTask不需要关心认证模块的底层实现。
     */
    event.source = AUTH_SOURCE_RFID;

    /*
     * 保存当前卡片UID。
     */
    memcpy(
        event.uid,
        uid,
        sizeof(event.uid)
    );

    /*
     * 和当前授权卡UID进行比较。
     *
     * memcmp返回0表示4个字节完全相同。
     */
    if (memcmp(
            uid,
            authorized_uid,
            sizeof(authorized_uid)) == 0)
    {
        event.result = AUTH_GRANTED;

        printf(
            "Card UID: %02X %02X %02X %02X\r\n",
            uid[0],
            uid[1],
            uid[2],
            uid[3]
        );

        printf("RFID Access Granted\r\n");
    }
    else
    {
        event.result = AUTH_DENIED;

        printf(
            "Card UID: %02X %02X %02X %02X\r\n",
            uid[0],
            uid[1],
            uid[2],
            uid[3]
        );

        printf("RFID Access Denied\r\n");
    }

    /*
     * 将认证结果发送给AccessTask。
     *
     * 当前设置100ms发送超时，
     * 方便调试Queue是否工作正常。
     */
    if (xQueueSend(
            authQueue,
            &event,
            pdMS_TO_TICKS(100U)) == pdPASS)
    {
        printf("Auth event sent\r\n");
    }
    else
    {
        printf("ERROR: Auth event send failed\r\n");
    }
}
