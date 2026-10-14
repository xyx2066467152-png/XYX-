/**
 * @file bsp_uart.c
 * 文件职责：USART1/PA9/PA10 的初始化和同步发送，句柄私有。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
/* USART1 句柄只归此模块管理，其他模块通过发送接口使用串口。 */
#include "bsp_uart.h"
#include "app_config.h"

static UART_HandleTypeDef huart1;
/*
 * USART2句柄。
 * 专门用于STM32与K230D之间的通信。
 */
static UART_HandleTypeDef huart2;

/* 单生产者 ISR / 单消费者 FaceTask；留一个槽区分空与满。 */
#if APP_FACE_RX_BUFFER_SIZE < 42U || APP_FACE_RX_BUFFER_SIZE > 65535U
#error Invalid_USART2_ring_buffer_size
#endif
static uint8_t uart2_rx_ring[APP_FACE_RX_BUFFER_SIZE];
static volatile uint16_t uart2_rx_head;
static volatile uint16_t uart2_rx_tail;
static volatile uint32_t uart2_rx_errors;
static uint8_t uart2_buffered;

/**
 * @brief 初始化 USART1 的 PA9/PA10 和串口参数，供 CH340 调试输出。
 * 调用时机：HAL 与时钟初始化后调用，通常由 BSP_Init 调用。
 * @return HAL_UART_Init 的状态；调用方应检查 HAL_OK。
 * 使用示例：
 *   if (BSP_UART_Init() != HAL_OK) { Error_Handler(); }
 * @note 易错点：当前波特率由 APP_UART_BAUDRATE 提供，格式 8N1。下载时 ROM 使用的串口参数与应用调试参数不同；TX/RX 接反或多个发送端共接 RX 会妨碍通信。
 */
HAL_StatusTypeDef BSP_UART_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    huart1.Instance = USART1;
    huart1.Init.BaudRate = APP_UART_BAUDRATE;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    return HAL_UART_Init(&huart1);
}

/**
 * @brief 同步阻塞发送指定长度的原始字节，不补结束符或换行。
 * 调用时机：USART1 初始化成功后，在普通任务或启动阶段调用。
 * @param data 至少包含 length 个可读取字节的有效缓冲区。
 * @param length 发送字节数，不是字符串容量；不要意外把字符串结尾的零字节一起发送。
 * @return HAL_UART_Transmit 的状态，可能为 HAL_OK、HAL_ERROR、HAL_BUSY 等。
 * 使用示例：
 *   const uint8_t msg[] = "OK\r\n"; BSP_UART_Write(msg, sizeof(msg) - 1U);
 * @note 易错点：使用 HAL_MAX_DELAY；硬件异常可能使调用长时间等待。没有空指针保护或发送互斥锁，不适合 ISR；多个任务的发送可能互相干扰。
 */
HAL_StatusTypeDef BSP_UART_Write(const uint8_t *data, uint16_t length)
{
    /* 旧版 HAL 参数未加 const；其发送函数只读取缓冲区。 */
    return HAL_UART_Transmit(&huart1, (uint8_t *)data, length, HAL_MAX_DELAY);
}
/**
 * @brief 初始化USART2
 * @用途
 *   USART1继续负责电脑调试日志；
 *   USART2专门负责K230D通信。
 * @硬件
 *   PA2 = USART2_TX
 *   PA3 = USART2_RX
 */
HAL_StatusTypeDef BSP_UART2_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 开启GPIOA与USART2外设时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    /*
     * PA2：USART2_TX
     * 配置为复用推挽输出。
     */
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /*
     * PA3：USART2_RX
     * 配置为输入模式。
     */
    gpio.Pin = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);

    /*
     * USART2通信参数。
     * K230D和STM32两边必须保持一致：
     * 115200 baud / 8 data bits / 1 stop bit / no parity
     */
    huart2.Instance = USART2;
    huart2.Init.BaudRate = APP_FACE_UART_BAUDRATE;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    return HAL_UART_Init(&huart2);
}

/**
 * @brief USART2阻塞接收
 */
HAL_StatusTypeDef BSP_UART2_Read(uint8_t *data,uint16_t length,uint32_t timeout)
{
    /* 中断缓冲启用后，禁止阻塞 HAL 接收竞争读取同一个 DR。 */
    if (uart2_buffered) { return HAL_BUSY; }
    return HAL_UART_Receive(
        &huart2,
        data,
        length,
        timeout
    );
}

void BSP_UART2_StartBufferedRx(void)
{
    volatile uint32_t discarded;
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    uart2_rx_head = 0U;
    uart2_rx_tail = 0U;
    uart2_rx_errors = 0U;
    /* F1 的 RXNE/ORE/FE/NE/PE 通过先读 SR、再读 DR 清除。 */
    discarded = USART2->SR;
    discarded = USART2->DR;
    (void)discarded;
    uart2_buffered = 1U;
    HAL_NVIC_SetPriority(USART2_IRQn, APP_FACE_RX_IRQ_PRIORITY, 0U);
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
    SET_BIT(USART2->CR1, USART_CR1_RXNEIE | USART_CR1_PEIE);
    SET_BIT(USART2->CR3, USART_CR3_EIE);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void BSP_UART2_IRQHandler(void)
{
    uint32_t status = USART2->SR;
    uint8_t value;
    uint16_t next;
    if ((status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_NE |
                   USART_SR_FE | USART_SR_PE)) == 0U) { return; }
    value = (uint8_t)USART2->DR;
    if ((status & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0U)
    {
        ++uart2_rx_errors;
        return;
    }
    if ((status & USART_SR_RXNE) == 0U) { return; }
    next = (uint16_t)((uart2_rx_head + 1U) % APP_FACE_RX_BUFFER_SIZE);
    if (next == uart2_rx_tail)
    {
        ++uart2_rx_errors;
        return;
    }
    uart2_rx_ring[uart2_rx_head] = value;
    __DMB();
    uart2_rx_head = next;
}

uint16_t BSP_UART2_ReadBuffered(uint8_t *data, uint16_t capacity)
{
    uint16_t count = 0U;
    while (count < capacity && uart2_rx_tail != uart2_rx_head)
    {
        data[count++] = uart2_rx_ring[uart2_rx_tail];
        __DMB();
        uart2_rx_tail = (uint16_t)((uart2_rx_tail + 1U) % APP_FACE_RX_BUFFER_SIZE);
    }
    return count;
}

uint32_t BSP_UART2_RxErrors(void) { return uart2_rx_errors; }

void BSP_UART2_DiscardBuffered(void)
{
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    uart2_rx_tail = uart2_rx_head;
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}
/**
 * @brief USART2阻塞发送
 */
HAL_StatusTypeDef BSP_UART2_Write(const uint8_t *data,
                                 uint16_t length)
{
    return HAL_UART_Transmit(
        &huart2,
        (uint8_t *)data,
        length,
        HAL_MAX_DELAY
    );
}
