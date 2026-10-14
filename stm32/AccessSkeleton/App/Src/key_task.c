/**
 * @file key_task.c
 * 文件职责：按键轮询、按下消抖、发送事件并等待松开。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
/* 按键任务只产生事件，通过队列通知 LED 任务，不直接控制灯。 */
#include "app_task_entries.h"
#include "app_events.h"
#include "app_config.h"
#include "bsp_key.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/**
 * @brief 轮询按键并消抖，把一次确认按下转成 KEY_EVENT_PRESSED 队列事件。
 * 调用时机：作为 FreeRTOS 任务入口交给 xTaskCreate，不直接当普通函数调用。
 * @param argument App 创建的有效 QueueHandle_t，通过 void * 传入。
 * @return 永不返回，循环中使用 vTaskDelay 让出 CPU。
 * 使用示例：
 *   由 App_CreateTasks 创建，业务层不要手动调用 KeyTask。
 * @note 易错点：按下低电平持续 20 ms 后确认；长按等待松开，通常一次按下只发一个事件。松开没有独立稳定消抖，严重松开抖动可能再次触发。队列满时 portMAX_DELAY 等待，可能延后后续按键采样。
 */
void KeyTask(void *argument)
{
    KeyEvent_t event = KEY_EVENT_PRESSED;
    QueueHandle_t keyQueue = (QueueHandle_t)argument;

    for (;;) {
        if (BSP_Key_Read(KEY1) == BSP_KEY_PRESSED) {
            /* 先等 20 ms，再检查一次；保持原有按下消抖算法。 */
            vTaskDelay(pdMS_TO_TICKS(APP_KEY_DEBOUNCE_MS));
            if (BSP_Key_Read(KEY1) == BSP_KEY_PRESSED) {
                printf("KEY1 pressed\r\n");
                /* 队列满时阻塞等待，不丢弃已确认的按下事件。 */
                xQueueSend(keyQueue, &event, portMAX_DELAY);
                /* 长按期间等待松开，避免一次按下产生重复事件。 */
                while (BSP_Key_Read(KEY1) == BSP_KEY_PRESSED) {
                    vTaskDelay(pdMS_TO_TICKS(APP_KEY_POLL_MS));
                }
            }
        }
        /* 每轮让出 CPU，沿用原工程的 10 ms 轮询间隔。 */
        vTaskDelay(pdMS_TO_TICKS(APP_KEY_POLL_MS));
    }
}
