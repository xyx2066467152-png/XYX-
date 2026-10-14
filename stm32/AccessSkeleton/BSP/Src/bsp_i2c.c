/**
 * @file bsp_i2c.c
 * 文件职责：I2C1/PB6/PB7 的 100 kHz 开漏总线初始化。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#include "bsp_i2c.h"
#include "main.h"


static I2C_HandleTypeDef hi2c1;

/**
 * @brief 初始化 I2C1：PB6 为 SCL、PB7 为 SDA，7 位地址、100 kHz 标准模式。
 * 调用时机：HAL 与时钟初始化后、OLED_Init 之前调用一次。
 * @return 无；HAL_I2C_Init 失败时进入 Error_Handler。
 * 使用示例：
 *   BSP_I2C1_Init(); 
 * @note 易错点：GPIO 使用复用开漏，SCL/SDA 需要合适的外部上拉。OwnAddress1=0 是主机自身地址配置，不是 OLED 从机地址。
 */
void BSP_I2C1_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 1. 打开 GPIOB 和 I2C1 时钟 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    /* 2. PB6 = SCL, PB7 = SDA */
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_OD;          // I2C 必须开漏
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* 3. 配置 I2C1 */
    hi2c1.Instance = I2C1;

    hi2c1.Init.ClockSpeed = 100000;        // 先用标准模式 100kHz
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    /* 主机本机地址配置与 OLED 的从机地址 0x3C 是不同概念。 */
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }

}

/**
 * @brief 返回本模块持有的静态 I2C1 句柄，供器件驱动调用 HAL。
 * 调用时机：BSP_I2C1_Init 完成后使用返回的句柄执行通信。
 * @return 指向 hi2c1 的指针，生命周期贯穿整个程序。
 * 使用示例：
 *   I2C_HandleTypeDef *hi2c = BSP_I2C1_GetHandle();
 * @note 易错点：返回非空不代表已经初始化成功；不要释放该指针、把它改指向其他实例，或在多个任务中无保护地共用同一总线。
 */
I2C_HandleTypeDef *BSP_I2C1_GetHandle(void)
{
    return &hi2c1;
}
