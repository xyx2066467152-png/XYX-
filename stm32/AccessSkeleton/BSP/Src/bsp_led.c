/**
 * @file bsp_led.c
 * 文件职责：LED1/PA11 的板级硬件实现，实际灯珠为 LED6。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
/* 仅封装本板 LED 的引脚和有效电平，上层无需知道 GPIO 编号。 */
#include "bsp_led.h"
#include "stm32f1xx_hal.h"

/**
 * @brief 使能 GPIOA，将 PA11 配成推挽输出，并使 LED1 初始熄灭。
 * 调用时机：使用 LED 前调用一次，通常由 BSP_Init 调用。
 * @return 无。
 * 使用示例：
 *   BSP_LED_Init();
 * @note 易错点：低电平点亮、高电平熄灭；LED1 是网络名，实体灯珠是 LED6。先设置高电平再切换输出模式，避免初始化时短暂亮灯。PA13/PA14 保留 SWD。
 */
void BSP_LED_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_SET);
    gpio.Pin = GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
    /* 不配置 PA13/PA14，也不关闭 SWD。 */
}

/**
 * @brief 翻转指定 LED 当前输出电平，亮变灭或灭变亮。
 * 调用时机：LED 初始化完成后，由 LedTask 收到按下事件时调用。
 * @param led 灯标识，当前只支持 LED1。
 * @return 无；未知灯标识不执行任何动作。
 * 使用示例：
 *   BSP_LED_Toggle(LED1);
 * @note 易错点：函数不创建定时、不自动闪烁，也不对多任务同时操作加锁；不要在 KeyTask 里直接调用而绕过事件队列。
 */
void BSP_LED_Toggle(BSP_LED_t led)
{
    if (led == LED1) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_11);
    }
}
