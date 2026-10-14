/**
 * @file bsp.c
 * 文件职责：LED、按键、USART1 的板级统一初始化。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
/* 板级初始化入口：不创建任务，不参与按键事件处理。 */
#include "bsp.h"
#include "bsp_led.h"
#include "bsp_key.h"
#include "bsp_uart.h"

/**
 * @brief 依次初始化 LED1、KEY1、USART1，提供最小板级初始化入口。
 * 调用时机：HAL_Init 和系统时钟配置之后调用一次。
 * @return 返回 USART1 初始化的 HAL 状态；HAL_OK 表示该初始化成功。
 * 使用示例：
 *   if (BSP_Init() != HAL_OK) { Error_Handler(); }
 * @note 易错点：此入口没有初始化 I2C，也没有初始化 OLED。串口初始化失败时 printf 尚不可依赖；不要忽略返回值。
 */
HAL_StatusTypeDef BSP_Init(void)
{
    BSP_LED_Init();
    BSP_Key_Init();
    return BSP_UART_Init();
}
