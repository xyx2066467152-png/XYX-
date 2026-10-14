/**
 * @file bsp_i2c.h
 * 文件职责：I2C 初始化与静态句柄获取接口。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef __BSP_I2C_H
#define __BSP_I2C_H

#include "stm32f1xx_hal.h"

/**
 * @brief 初始化 I2C1：PB6 为 SCL、PB7 为 SDA，7 位地址、100 kHz 标准模式。
 * 调用时机：HAL 与时钟初始化后、OLED_Init 之前调用一次。
 * @return 无；HAL_I2C_Init 失败时进入 Error_Handler。
 * 使用示例：
 *   BSP_I2C1_Init(); 
 * @note 易错点：GPIO 使用复用开漏，SCL/SDA 需要合适的外部上拉。OwnAddress1=0 是主机自身地址配置，不是 OLED 从机地址。
 */
void BSP_I2C1_Init(void);
/**
 * @brief 返回本模块持有的静态 I2C1 句柄，供器件驱动调用 HAL。
 * 调用时机：BSP_I2C1_Init 完成后使用返回的句柄执行通信。
 * @return 指向 hi2c1 的指针，生命周期贯穿整个程序。
 * 使用示例：
 *   I2C_HandleTypeDef *hi2c = BSP_I2C1_GetHandle();
 * @note 易错点：返回非空不代表已经初始化成功；不要释放该指针、把它改指向其他实例，或在多个任务中无保护地共用同一总线。
 */
I2C_HandleTypeDef *BSP_I2C1_GetHandle(void);

#endif
