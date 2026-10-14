/**
 * @file freertos_hooks.c
 * 文件职责：动态分配失败和栈溢出的 RTOS 故障停机钩子。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
/* FreeRTOS 故障钩子：内存分配失败或栈溢出时关闭中断并停机，便于定位。 */
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief FreeRTOS 动态分配失败时停机，保留堆不足的现场。
 * 调用时机：configUSE_MALLOC_FAILED_HOOK=1 时由内核自动调用。
 * @return 不返回。
 * 使用示例：
 *   xQueueCreate/xTaskCreate 失败时检查此钩子、20 KiB RTOS 堆和剩余堆大小。
 * @note 易错点：它可能在 API 返回 NULL/pdFAIL 之前停住。不要在这里继续动态分配或进行复杂 printf；正常任务调度已经停止。taskDISABLE_INTERRUPTS 屏蔽 RTOS 相关范围，不等于关闭全部中断。
 */
void vApplicationMallocFailedHook(void)
{
    /* Cortex-M3 port 使用 BASEPRI 屏蔽 RTOS 相关中断；优先级 4 的 TIM2 仍可能运行。 */
    taskDISABLE_INTERRUPTS();
    for (;;) { }
}

/**
 * @brief FreeRTOS 检测到任务栈溢出时停机，避免继续扩大内存破坏。
 * 调用时机：configCHECK_FOR_STACK_OVERFLOW=2 时由内核在任务切换检查中调用。
 * @param task 出现问题的任务句柄，用于调试器观察。
 * @param task_name 该任务名字；栈或 TCB 已损坏时不保证内容可靠。
 * @return 不返回。
 * 使用示例：
 *   检查任务栈高水位，特别注意 printf 和 OLED 发送函数的局部数组开销。
 * @note 易错点：当前钩子不打印名字；检测不能覆盖全部越界场景，也不能修复已经破坏的内存。
 */
void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    taskDISABLE_INTERRUPTS();
    for (;;) { }
}
