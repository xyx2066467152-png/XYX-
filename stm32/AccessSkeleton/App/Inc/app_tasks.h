/**
 * @file app_tasks.h
 * 文件职责：App 初始化/创建任务接口；按返回值处理失败。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H
#include "FreeRTOS.h"
/* BSP_Init 成功后、调度器启动前调用一次：输出启动消息并创建事件队列。 */
/**
 * @brief 输出启动消息并创建按键事件队列，供两个应用任务通信。
 * 调用时机：BSP_Init 成功后、创建任务和启动调度器之前调用一次。
 * @return 队列创建成功返回 pdPASS，失败返回 pdFAIL；启用内存失败钩子时可能先停在钩子里。
 * 使用示例：
 *   if (App_Init() != pdPASS) { Error_Handler(); }
 * @note 易错点：队列长度以事件个数计，元素大小为 sizeof(KeyEvent_t)。重复调用会重新分配队列并覆盖句柄；不要在运行过程中重复初始化。
 */
BaseType_t App_Init(void);
/* App_Init 成功后调用一次：创建 KeyTask 和 LedTask，失败返回 pdFAIL。 */
/**
 * @brief 动态创建 KeyTask 和 LedTask，把同一个事件队列句柄传给两者。
 * 调用时机：App_Init 返回 pdPASS 后，调度器启动前调用一次。
 * @return 两个任务均创建成功返回 pdPASS；队列不存在或创建失败返回 pdFAIL。
 * 使用示例：
 *   if (App_CreateTasks() != pdPASS) { Error_Handler(); }
 * @note 易错点：栈参数单位是 StackType_t 个数，不是字节。当前不回滚部分创建结果，也不防止重复创建；调用方必须处理失败，不能忽略后继续启动。
 */
BaseType_t App_CreateTasks(void);
/* 保留旧 main 在调度器意外返回时的状态输出，不在正常任务流程中调用。 */
/**
 * @brief 输出当前 KEY1 的原始状态，用于原工程调度器返回路径诊断。
 * 调用时机：只在 vTaskStartScheduler 意外返回后需要该诊断时调用。
 * @return 无；输出 KEY1=0 表示按下，1 表示松开。
 * 使用示例：
 *   vTaskStartScheduler(); App_OnSchedulerReturn();
 * @note 易错点：正常启动调度器后不会执行到此处。当前 main 实际直接调用 Error_Handler，不会自动调用本接口。
 */
void App_OnSchedulerReturn(void);

#endif
