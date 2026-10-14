/**
 * @file FreeRTOSConfig.h
 * 文件职责：内核配置：堆、任务、ISR 优先级边界和异常入口映射。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
/* 内核配置保持原值：SysTick 为 1 kHz，heap_4 堆为 20 KiB。
 * 使用 FromISR 接口的外设中断优先级数值须为 5～15；TIM2 优先级 4 不调用内核。
 * 文件末尾将 SVC、PendSV、SysTick 入口映射到 Cortex-M3 port。
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H
#include "stm32f1xx.h"
/* 1 表示抢占式调度；高优先级就绪任务可抢占当前任务。 */
#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
/* 读取 SystemCoreClock，而非直接写死频率；HSI 配置变化后需正确更新该变量。 */
#define configCPU_CLOCK_HZ                      ( SystemCoreClock )
/* SysTick 每秒 1000 次，当前每个 RTOS tick 为 1 ms。 */
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
/* 任务优先级范围为 0～4，不能使用值 5 或更大。 */
#define configMAX_PRIORITIES                    5
/* 内核最小栈参数单位为字；应用两个任务另由 app_config.h 的宏指定。 */
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 128 )
/* heap_4 的 20 KiB RTOS 堆，创建任务、队列等从中分配；与 C 库堆不是同一个配置。 */
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 20 * 1024 ) )
#define configMAX_TASK_NAME_LEN                 16
/* 0 使用 32 位 tick；系统长期运行仍有计数回绕，时间差应使用合适的无符号算法。 */
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
/* 启用 Mutex 功能不代表 UART/I2C 已自动加锁；当前驱动没有互斥保护。 */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
/* 启用任务通知 API；当前按键通信实际使用 Queue。 */
#define configUSE_TASK_NOTIFICATIONS            1
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_QUEUE_SETS                    1
/* 启用软件定时器会额外创建定时器服务任务，其栈和优先级见下面参数。 */
#define configUSE_TIMERS                        1
/* 软件定时器服务任务优先级；回调不能长时间阻塞，否则拖延其他定时器。 */
#define configTIMER_TASK_PRIORITY               2
#define configTIMER_QUEUE_LENGTH                5
/* 定时器服务任务栈单位也是字，回调函数共享这块任务栈。 */
#define configTIMER_TASK_STACK_DEPTH            128
/* 模式 2 增加栈边界检测，不能替代合理的栈预算或覆盖全部内存错误。 */
#define configCHECK_FOR_STACK_OVERFLOW          2
/* 分配失败可能先进入故障钩子死循环，而不是让上层看到 NULL。 */
#define configUSE_MALLOC_FAILED_HOOK            1
/* STM32F103 实现 4 位 NVIC 优先级，未移位的数值为 0～15。 */
#define configPRIO_BITS                         4
/* 未移位的最低中断优先级为 15；NVIC 数值越小越紧急。 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
/* 调用 FreeRTOS FromISR API 的 ISR 优先级数值必须为 5～15；0～4 不能调用。 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
/* 转换为寄存器需要的左移格式，不要直接把该值当 HAL_NVIC_SetPriority 参数。 */
#define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
/* 同样是已移位寄存器值；与上面未移位的 LIBRARY 宏区分使用。 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
/* 断言失败关闭 RTOS 相关中断并停机，调试时需要查看触发位置。 */
#define configASSERT(x)                         if ((x) == 0) { taskDISABLE_INTERRUPTS(); for(;;); }
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskPrioritySet                1
/* 将 port 的 SVC 实现对接启动向量表；不能在 it.c 再定义同名入口。 */
#define vPortSVCHandler                         SVC_Handler
/* PendSV 负责任务切换；重复定义会导致链接冲突或错误切换。 */
#define xPortPendSVHandler                      PendSV_Handler
/* SysTick 属于 FreeRTOS；HAL 毫秒计数已改用 TIM2。 */
#define xPortSysTickHandler                     SysTick_Handler
#endif
