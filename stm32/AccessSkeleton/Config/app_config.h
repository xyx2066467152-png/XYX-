/**
 * @file app_config.h
 * 文件职责：应用参数：串口波特率、任务栈/优先级、队列长度和按键时序。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H
/* 应用参数集中在这里，数值全部沿用重构前的 main.c。 */
/* 串口调试波特率；上位机保持相同波特率和 8N1。 */
#define APP_UART_BAUDRATE          115200U
/* 队列容量是事件个数，不是字节数；元素大小由 xQueueCreate 的第二参数确定。 */
#define APP_KEY_QUEUE_LENGTH       4U
/* FreeRTOS 栈深度的单位是 StackType_t 个数；Cortex-M3 上 128 个字为 512 字节。 */
/* 原 configMINIMAL_STACK_SIZE 为 128，空闲任务优先级为 0。 */
#define APP_AUTH_QUEUE_LENGTH             4U
/* KeyTask 栈为 128 个 32 位字，即 512 字节；printf 会消耗额外栈空间。 */
/*
 * 用于：
 *   RfidTask / 以后K230D Task
 *              ↓
 *          AuthEvent
 *              ↓
 *          AccessTask
 */
#define APP_KEY_TASK_STACK_WORDS   128U
/* LedTask 栈为 128 个 32 位字，即 512 字节；不是两个任务共用这块栈。 */
#define APP_LED_TASK_STACK_WORDS   128U
/* 任务优先级数值越大越先运行；与 NVIC 中断优先级数值方向相反。 */
#define APP_KEY_TASK_PRIORITY      2U
/* 低于 KeyTask；没有事件时通过队列阻塞。 */
#define APP_LED_TASK_PRIORITY      1U
/* 确认按下的等待时间；单位毫秒，经 pdMS_TO_TICKS 转换为 RTOS tick。 */
#define APP_KEY_DEBOUNCE_MS        20U
/* 轮询及等待松开的采样间隔；不是硬件中断周期。 */
#define APP_KEY_POLL_MS            10U
/* * 当前任务会使用 printf() 打印 UID。*/
#define APP_RFID_TASK_STACK_WORDS    (configMINIMAL_STACK_SIZE * 2U)
/* RC522 当前只是每隔几十毫秒轮询一次，不属于高实时性任务，所以普通优先级即可。*/ 
#define APP_RFID_TASK_PRIORITY       (tskIDLE_PRIORITY + 1U)
/*
 * AccessTask
 * AccessTask会操作：
 *   OLED
 *   Servo
 *   printf
 * 因此当前给它比最简单GPIO任务稍大的栈。
 */
#define APP_ACCESS_TASK_STACK_WORDS   (configMINIMAL_STACK_SIZE * 2U)
#define APP_ACCESS_TASK_PRIORITY      (tskIDLE_PRIORITY + 2U)
/*
 * 门禁动作任务稍高于普通轮询任务即可。
 */
 /* K230D串口通信任务 */
#define APP_FACE_TASK_STACK_WORDS \
    (configMINIMAL_STACK_SIZE * 2U)

#define APP_FACE_TASK_PRIORITY \
    (tskIDLE_PRIORITY + 1U)
/* V1 人脸通信参数；权限仅在 STM32 维护。 */
#define APP_FACE_AUTHORIZED_USER_IDS     {1U}
#define APP_FACE_UART_BAUDRATE           115200U
#define APP_FACE_RX_BUFFER_SIZE          256U
#define APP_FACE_RX_IRQ_PRIORITY         6U
#define APP_FACE_READ_CHUNK              64U
#define APP_FACE_READ_BUDGET             128U
#define APP_FACE_POLL_MS                 1U
#define APP_FACE_DIAGNOSTIC_MS           5000U
#define APP_FACE_NORMAL_LOG_MS           1000U
#endif
