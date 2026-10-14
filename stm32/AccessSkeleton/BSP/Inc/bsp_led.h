/**
 * @file bsp_led.h
 * 文件职责：LED 标识与初始化/翻转接口。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef BSP_LED_H
#define BSP_LED_H
/* LED1 是网络名，对应 PA11；实际灯珠元件号为 LED6。 */
typedef enum { LED1 = 0 } BSP_LED_t;
/* 低电平点亮；初始化时先输出高电平，使灯保持熄灭。 */
/**
 * @brief 使能 GPIOA，将 PA11 配成推挽输出，并使 LED1 初始熄灭。
 * 调用时机：使用 LED 前调用一次，通常由 BSP_Init 调用。
 * @return 无。
 * 使用示例：
 *   BSP_LED_Init();
 * @note 易错点：低电平点亮、高电平熄灭；LED1 是网络名，实体灯珠是 LED6。先设置高电平再切换输出模式，避免初始化时短暂亮灯。PA13/PA14 保留 SWD。
 */
void BSP_LED_Init(void);
/* 翻转指定灯的当前输出电平，不负责定时或事件处理。 */
/**
 * @brief 翻转指定 LED 当前输出电平，亮变灭或灭变亮。
 * 调用时机：LED 初始化完成后，由 LedTask 收到按下事件时调用。
 * @param led 灯标识，当前只支持 LED1。
 * @return 无；未知灯标识不执行任何动作。
 * 使用示例：
 *   BSP_LED_Toggle(LED1);
 * @note 易错点：函数不创建定时、不自动闪烁，也不对多任务同时操作加锁；不要在 KeyTask 里直接调用而绕过事件队列。
 */
void BSP_LED_Toggle(BSP_LED_t led);
#endif
