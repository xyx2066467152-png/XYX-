#ifndef __BSP_SPI_H
#define __BSP_SPI_H

#include "stm32f1xx_hal.h"

/**
 * @brief 初始化 SPI2 以及 RC522/W25Q128 的片选相关 GPIO
 *
 * @硬件资源
 *   PB13 -> SPI2_SCK
 *   PB14 -> SPI2_MISO
 *   PB15 -> SPI2_MOSI
 *
 *   PB12 -> 板载 W25Q128_CS
 *   PC0  -> RC522_CS
 *   PC1  -> RC522_RST
 *
 * @用途
 *   为后续 RC522 Driver 提供 SPI2 底层通信能力。
 *
 * @注意
 *   RC522 与 W25Q128 共用 SPI2 总线，
 *   因此两个设备必须使用独立 CS。
 */
void BSP_SPI2_Init(void);


/**
 * @brief 获取 SPI2 HAL 句柄
 *
 * @return SPI2 的 HAL Handle 地址
 *
 * @用途
 *   Driver 层通过该接口调用 HAL_SPI_TransmitReceive()，
 *   避免直接访问 BSP 内部的 static 变量。
 */
SPI_HandleTypeDef *BSP_SPI2_GetHandle(void);


/**
 * @brief 选中 RC522
 *
 * @说明
 *   RC522 的 NSS/CS 为低电平有效。
 */
void BSP_RC522_Select(void);


/**
 * @brief 取消选中 RC522
 */
void BSP_RC522_Deselect(void);


/**
 * @brief 控制 RC522 RST 引脚
 *
 * @param state
 *        GPIO_PIN_SET   -> RST 高电平
 *        GPIO_PIN_RESET -> RST 低电平
 */
void BSP_RC522_SetReset(GPIO_PinState state);

#endif
