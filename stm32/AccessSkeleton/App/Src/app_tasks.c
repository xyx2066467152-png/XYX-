/**
 * @file app_tasks.c
 * 文件职责：App 队列生命周期和两个 FreeRTOS 任务的创建。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
/* 应用装配：管理队列的生命周期和任务创建，不直接操作 GPIO。 */
#include "app_tasks.h"
#include "app_events.h"
#include "app_task_entries.h"
#include "app_config.h"
#include "bsp_key.h"
#include "log.h"
#include "task.h"
#include "queue.h"
#include "FreeRTOS.h"
#include "task.h"
#include "servo.h"

/* 句柄归 App 所有；任务运行期间队列必须一直有效，不能随意重新创建或删除。 */
static QueueHandle_t keyQueue;
/*
 * 身份认证事件队列。
 * RfidTask / 后续 FaceTask：
 *     发送认证结果
 * AccessTask： 接收认证结果并控制门禁
 */
static QueueHandle_t authQueue;
/**
 * @brief  初始化应用层 FreeRTOS 对象
 *
 * @用途
 *   当前负责创建：
 *
 *   1. keyQueue
 *      KeyTask -> LedTask
 *
 *   2. authQueue
 *      RfidTask / 后续 FaceTask -> AccessTask
 *
 * @return
 *   pdPASS：所有 Queue 创建成功
 *   pdFAIL：任意一个 Queue 创建失败
 *
 * @注意
 *   本函数必须在 App_CreateTasks() 之前调用，
 *   因为任务创建时会使用这些 Queue Handle。
 */
BaseType_t App_Init(void)
{
    const char msg[] = "Hello STM32\r\n";

    printf("FreeRTOS Key Queue Test\r\n");
    Log_Write(msg, sizeof(msg) - 1U);
    /*
     * 创建按键事件队列。
     * 当前用途：
     *   KeyTask
     *      ↓
     *   keyQueue
     *      ↓
     *   LedTask
     */
    keyQueue = xQueueCreate(
        APP_KEY_QUEUE_LENGTH,
        sizeof(KeyEvent_t)
    );

    if (keyQueue == NULL)
    {
        return pdFAIL;
    }
    /*
     * 创建身份认证事件队列。
     * 当前用途：
     *   RfidTask
     *      ↓
     *   authQueue
     *      ↓
     *   AccessTask
     * 后续 K230D 人脸认证也可以向这个 Queue,发送 AuthEvent_t。
     */
    authQueue = xQueueCreate(
        APP_AUTH_QUEUE_LENGTH,
        sizeof(AuthEvent_t)
    );

    if (authQueue == NULL)
    {
        return pdFAIL;
    }
    /*
     * 两个 Queue 都创建成功。
     */
    return pdPASS;
}

/**
 * @brief  创建应用层 FreeRTOS 任务
 *
 * @return pdPASS
 *         所有任务创建成功
 *
 * @return pdFAIL
 *         Queue 未创建，或者任意一个 Task 创建失败
 *
 * @当前任务
 *   KeyTask
 *      - 检测 KEY1
 *      - 将按键事件发送到 keyQueue
 *
 *   LedTask
 *      - 接收 keyQueue
 *      - 当前用于 LED / 临时门禁动作测试
 *
 *   RfidTask
 *      - 周期性检测 RC522
 *      - 当前用于寻卡并打印 UID
 *
 * @注意
 *   keyQueue 必须在调用本函数之前创建。
 */
BaseType_t App_CreateTasks(void)
{
    /*
     * 两个Queue都必须已经由 App_Init() 创建成功。
     */
    if ((keyQueue == NULL) || (authQueue == NULL))
    {
        return pdFAIL;
    }

    /*
     * KEY1扫描任务。
     * keyQueue通过任务参数传给KeyTask。
     */
    if (xTaskCreate(
            KeyTask,
            "KeyTask",
            APP_KEY_TASK_STACK_WORDS,
            (void *)keyQueue,
            APP_KEY_TASK_PRIORITY,
            NULL) != pdPASS)
    {
        return pdFAIL;
    }

    /*
     * LED任务。
     * 当前只做KEY1基础测试。
     */
    if (xTaskCreate(
            LedTask,
            "LedTask",
            APP_LED_TASK_STACK_WORDS,
            (void *)keyQueue,
            APP_LED_TASK_PRIORITY,
            NULL) != pdPASS)
    {
        return pdFAIL;
    }

    /*
     * RFID任务。
     *
     * 重点：
     * 必须把 authQueue 传给它。
     *
     * RfidTask读取UID、判断权限以后，
     * 会往这个Queue发送AuthEvent_t。
     */
    if (xTaskCreate(
            RfidTask,
            "RfidTask",
            APP_RFID_TASK_STACK_WORDS,
            (void *)authQueue,
            APP_RFID_TASK_PRIORITY,
            NULL) != pdPASS)
    {
        return pdFAIL;
    }

    /*
     * 门禁控制任务。
     *
     * 和RfidTask使用同一个authQueue：
     *
     * RfidTask   -> xQueueSend()
     * AccessTask -> xQueueReceive()
     */
    if (xTaskCreate(
            AccessTask,
            "AccessTask",
            APP_ACCESS_TASK_STACK_WORDS,
            (void *)authQueue,
            APP_ACCESS_TASK_PRIORITY,
            NULL) != pdPASS)
    {
        return pdFAIL;
    }

        /*
    * 创建K230D通信任务。
    *
    * 与 RfidTask 使用同一个 authQueue，V1 解析后交给 AccessTask。
    */
    if (xTaskCreate(
            FaceTask,
            "FaceTask",
            APP_FACE_TASK_STACK_WORDS,
           (void *)authQueue,
            APP_FACE_TASK_PRIORITY,
            NULL) != pdPASS)
    {
        return pdFAIL;
    }
        return pdPASS;
}
/**
 * @brief 输出当前 KEY1 的原始状态，用于原工程调度器返回路径诊断。
 * 调用时机：只在 vTaskStartScheduler 意外返回后需要该诊断时调用。
 * @return 无；输出 KEY1=0 表示按下，1 表示松开。
 * 使用示例：
 *   vTaskStartScheduler(); App_OnSchedulerReturn();
 * @note 易错点：正常启动调度器后不会执行到此处。当前 main 实际直接调用 Error_Handler，不会自动调用本接口。
 */
void App_OnSchedulerReturn(void)
{
    printf("KEY1 = %d\r\n", (int)BSP_Key_Read(KEY1));
}
/**
 * @brief  临时执行一次“开门 -> 等待 -> 关门”的门禁动作
 *
 * @用途
 *   当前用于 KEY1 模拟认证成功后的门禁动作。
 *
 *   调用后流程：
 *
 *       Servo_Open()
 *           ↓
 *       保持开门 2 秒
 *           ↓
 *       Servo_Close()
 *
 * @注意
 *   1. 本函数属于 App 层业务逻辑，不应该放到 servo.c。
 *   2. 函数内部使用 vTaskDelay()，
 *      因此只能在 FreeRTOS Scheduler 已经启动后的 Task 中调用。
 *      不要在 main() 启动调度器之前调用。
 *   3. vTaskDelay() 会让“当前调用这个函数的 Task”阻塞 2 秒，
 *      其他 Task 仍然可以正常运行。
 *   4. 这是当前测试阶段的临时实现。
 *      后续正式门禁状态机会改成非阻塞方式：
 *          OPEN_DOOR
 *              ↓
 *          WAIT_CLOSE
 *              ↓
 *          超时
 *              ↓
 *          CLOSE_DOOR
 *      届时这个函数可以删除。
 */


