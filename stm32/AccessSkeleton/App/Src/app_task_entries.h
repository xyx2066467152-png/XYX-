/**
 * @file app_task_entries.h
 * 文件职责：App 内部任务入口声明；argument 是有效队列句柄。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef APP_TASK_ENTRIES_H
#define APP_TASK_ENTRIES_H
/* App 层内部任务入口；argument 均为同一个按键事件队列的句柄。 */
/**
 * @brief 轮询按键并消抖，把一次确认按下转成 KEY_EVENT_PRESSED 队列事件。
 * 调用时机：作为 FreeRTOS 任务入口交给 xTaskCreate，不直接当普通函数调用。
 * @param argument App 创建的有效 QueueHandle_t，通过 void * 传入。
 * @return 永不返回，循环中使用 vTaskDelay 让出 CPU。
 * 使用示例：
 *   由 App_CreateTasks 创建，业务层不要手动调用 KeyTask。
 * @note 易错点：按下低电平持续 20 ms 后确认；长按等待松开，通常一次按下只发一个事件。松开没有独立稳定消抖，严重松开抖动可能再次触发。队列满时 portMAX_DELAY 等待，可能延后后续按键采样。
 */
void KeyTask(void *argument);
/**
 * @brief 阻塞等待按键事件，每收到一个按下事件翻转一次 LED1 并打印日志。
 * 调用时机：作为 FreeRTOS 任务入口由 App_CreateTasks 创建。
 * @param argument 与 KeyTask 共用的有效事件队列句柄。
 * @return 永不返回；没有事件时阻塞，不忙等。
 * 使用示例：
 *   App_Init(); App_CreateTasks(); vTaskStartScheduler();
 * @note 易错点：队列复制的是事件值，不是局部变量地址。LED1 的实体灯珠是 LED6；如果没有启动此任务或队列无效，按键事件不会转成灯动作。
 */
void LedTask(void *argument);

/**
 * @brief RC522 RFID 检测任务
 *
 * @用途
 *   周期性检测卡片，并读取 UID。
 *
 * @param argument
 *   当前不使用，因此创建任务时传 NULL。
 */
void RfidTask(void *argument);
/**
 * @brief RC522刷卡检测任务
 */
void RfidTask(void *argument);

/**
 * @brief 门禁业务处理任务
 * @用途
 *   接收认证事件并决定：- 是否开门 - OLED显示什么 - 什么时候关门
 */
void AccessTask(void *argument);
/**
 * @brief K230D串口通信任务
 */
void FaceTask(void *argument);
#endif
