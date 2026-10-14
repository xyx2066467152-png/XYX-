/**
 * @file led_task.c
 * 文件职责：等待按键事件，再通过 BSP 翻转 LED 并输出日志。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
/* LED 任务阻塞等待事件，每收到一次确认的按下事件只翻转一次灯。 */
#include "app_task_entries.h"
#include "app_events.h"
#include "bsp_led.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "FreeRTOS.h"
#include "task.h"
#include "servo.h"
#include "oled_ssd1315.h"
#include <stdio.h>
/**
 * @brief 阻塞等待按键事件，每收到一个按下事件翻转一次 LED1 并打印日志。
 * 调用时机：作为 FreeRTOS 任务入口由 App_CreateTasks 创建。
 * @param argument 与 KeyTask 共用的有效事件队列句柄。
 * @return 永不返回；没有事件时阻塞，不忙等。
 * 使用示例：
 *   App_Init(); App_CreateTasks(); vTaskStartScheduler();
 * @note 易错点：队列复制的是事件值，不是局部变量地址。LED1 的实体灯珠是 LED6；如果没有启动此任务或队列无效，按键事件不会转成灯动作。
 */
void LedTask(void *argument)
{
    KeyEvent_t event;
    (void)argument;
    QueueHandle_t keyQueue = (QueueHandle_t)argument;

   for (;;)
    {
        /*
         * 等待Queue事件。
         *
         * portMAX_DELAY：
         *   Queue没有消息时，当前Task进入Blocked状态，
         *   不会一直循环浪费CPU。
         *
         * 注意：
         *   这里的 keyQueue 请继续使用你工程中
         *   原本已经能够正常工作的Queue Handle。
         */
        if (xQueueReceive(keyQueue,
                          &event,
                          portMAX_DELAY) == pdPASS)
        {
           if (event == KEY_EVENT_PRESSED)
            {
            /*
             * KEY1 当前只保留基础按键测试功能。
             * 门锁动作以后统一交给 AccessTask。
             */
            BSP_LED_Toggle(LED1);
            printf("KEY1 pressed\r\n");
            }
        }
    }
}
