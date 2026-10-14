/**
 * @file stm32f1xx_hal_msp.c
 * 文件职责：HAL 基础时钟支持；具体外设引脚由 BSP 管理。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#include "main.h"

/**
 * @brief 使能 AFIO 与 PWR 基础时钟，为后续 BSP 配置提供条件。
 * 调用时机：HAL_Init 内部通过 HAL 的弱函数覆盖机制调用，不必再手动调用。
 * @return 无。
 * 使用示例：
 *   HAL_Init(); 
 * @note 易错点：这里没有初始化 UART/I2C 引脚；这些在 BSP 中完成。保留 PA13/PA14 的 SWD，避免误用关闭 SWJ 的重映射宏。
 */
void HAL_MspInit(void)
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    /* 具体引脚由 BSP 初始化；保留 PA13/PA14 的 SWD 功能。 */
}
