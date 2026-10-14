#include "bsp_spi.h"
#include "main.h"

/*
 * SPI2句柄只由本文件管理。
 *
 * 上层如果需要使用SPI2，
 * 通过 BSP_SPI2_GetHandle() 获取。
 */
static SPI_HandleTypeDef hspi2;


/**
 * @brief 初始化SPI2及RC522相关GPIO
 */
void BSP_SPI2_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * 1. 开启相关外设时钟
     */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();


    /*
     * ============================================================
     * 2. 配置 SPI2 SCK / MOSI
     * ============================================================
     *
     * PB13 -> SCK
     * PB15 -> MOSI
     *
     * 主机输出，因此配置成复用推挽输出。
     */
    gpio.Pin = GPIO_PIN_13 | GPIO_PIN_15;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(GPIOB, &gpio);


    /*
     * PB14 -> MISO
     *
     * MISO由从机RC522/W25Q128输出，
     * STM32这里只负责输入。
     */
    gpio.Pin = GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(GPIOB, &gpio);


    /*
     * ============================================================
     * 3. 配置两个设备的CS
     * ============================================================
     *
     * PB12 -> W25Q128_CS
     *
     * 当前暂时不访问W25Q128，
     * 所以上电后立即拉高，保持它处于未选中状态。
     */
    gpio.Pin = GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_GPIO_WritePin(
        GPIOB,
        GPIO_PIN_12,
        GPIO_PIN_SET
    );


    /*
     * PC0 -> RC522_CS
     *
     * CS低电平有效。
     * 初始化时先拉高，避免初始化过程中误触发通信。
     */
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(GPIOC, &gpio);

    HAL_GPIO_WritePin(
        GPIOC,
        GPIO_PIN_0,
        GPIO_PIN_SET
    );


    /*
     * PC1 -> RC522_RST
     *
     * RST用于硬件复位RC522。
     */
    gpio.Pin = GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(GPIOC, &gpio);

    /*
     * 默认保持RC522退出复位状态。
     */
    HAL_GPIO_WritePin(
        GPIOC,
        GPIO_PIN_1,
        GPIO_PIN_SET
    );


    /*
     * ============================================================
     * 4. 配置 SPI2
     * ============================================================
     *
     * STM32作为主机。
     */
    hspi2.Instance = SPI2;

    hspi2.Init.Mode = SPI_MODE_MASTER;

    /*
     * 两根数据线：
     * MOSI + MISO
     */
    hspi2.Init.Direction = SPI_DIRECTION_2LINES;

    /*
     * RC522每次传输8bit。
     */
    hspi2.Init.DataSize = SPI_DATASIZE_8BIT;

    /*
     * RC522常用SPI Mode 0：
     *
     * CPOL = 0
     * CPHA = 0
     *
     * 空闲时SCK为低，
     * 第一个边沿采样。
     */
    hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;

    /*
     * RC522的CS由软件GPIO PC0控制，
     * 所以这里使用软件NSS。
     */
    hspi2.Init.NSS = SPI_NSS_SOFT;

    /*
     * 当前PCLK1 = 8 MHz。
     *
     * /8 后：
     *
     * 8 MHz / 8 = 1 MHz
     *
     * 第一次调试先使用1MHz，
     * 稳定性优先，不追求速度。
     */
    hspi2.Init.BaudRatePrescaler =
        SPI_BAUDRATEPRESCALER_8;

    /*
     * RC522 SPI先发送最高位。
     */
    hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;

    /*
     * 普通SPI通信不使用TI协议。
     */
    hspi2.Init.TIMode = SPI_TIMODE_DISABLE;

    /*
     * CRC暂时不使用。
     */
    hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.CRCPolynomial = 7;


    /*
     * 初始化SPI2。
     *
     * 如果失败，说明SPI配置异常，
     * 直接进入Error_Handler方便排错。
     */
    if (HAL_SPI_Init(&hspi2) != HAL_OK)
    {
        Error_Handler();
    }
}


/**
 * @brief 返回SPI2句柄
 */
SPI_HandleTypeDef *BSP_SPI2_GetHandle(void)
{
    return &hspi2;
}


/**
 * @brief 选中RC522
 */
void BSP_RC522_Select(void)
{
    /*
     * CS低电平有效。
     */
    HAL_GPIO_WritePin(
        GPIOC,
        GPIO_PIN_0,
        GPIO_PIN_RESET
    );
}


/**
 * @brief 取消选中RC522
 */
void BSP_RC522_Deselect(void)
{
    HAL_GPIO_WritePin(
        GPIOC,
        GPIO_PIN_0,
        GPIO_PIN_SET
    );
}


/**
 * @brief 控制RC522硬件复位引脚
 */
void BSP_RC522_SetReset(GPIO_PinState state)
{
    HAL_GPIO_WritePin(
        GPIOC,
        GPIO_PIN_1,
        state
    );
}
