/**
 * @file bsp_uart.h
 * 文件职责：阻塞串口发送公共接口和参数约束。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef BSP_UART_H
#define BSP_UART_H
#include "stm32f1xx_hal.h"
/* USART1：PA9 发送、PA10 接收，115200 波特率、8N1，无硬件流控。 */
/**
 * @brief 初始化 USART1 的 PA9/PA10 和串口参数，供 CH340 调试输出。
 * 调用时机：HAL 与时钟初始化后调用，通常由 BSP_Init 调用。
 * @return HAL_UART_Init 的状态；调用方应检查 HAL_OK。
 * 使用示例：
 *   if (BSP_UART_Init() != HAL_OK) { Error_Handler(); }
 * @note 易错点：当前波特率由 APP_UART_BAUDRATE 提供，格式 8N1。下载时 ROM 使用的串口参数与应用调试参数不同；TX/RX 接反或多个发送端共接 RX 会妨碍通信。
 */
HAL_StatusTypeDef BSP_UART_Init(void);
/* 阻塞发送：沿用 HAL_MAX_DELAY，不创建中断发送、DMA 或日志任务。 */
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
HAL_StatusTypeDef BSP_UART_Write(const uint8_t *data, uint16_t length);
/**
 * @brief 初始化 USART2，用于 STM32 与 K230D 通信
 * @硬件连接
 *   PA2 -> USART2_TX -> K230D RX
 *   PA3 -> USART2_RX <- K230D TX
 * @通信参数
 *   115200 / 8N1 / 无硬件流控
 * @return
 *   HAL_OK：初始化成功
 *   其他值：初始化失败
 */
HAL_StatusTypeDef BSP_UART2_Init(void);
/**
 * @brief 从 USART2 接收指定长度的数据
 *
 * @param data    接收缓冲区
 * @param length  需要接收的字节数
 * @param timeout 最大等待时间，单位 ms
 * @return HAL UART 接收状态
 * @note
 * 当前先使用阻塞接收把 K230D 通信跑通，
 * 后续如果需要可升级成中断或DMA接收。
 */
HAL_StatusTypeDef BSP_UART2_Read(uint8_t *data,uint16_t length,uint32_t timeout);
/**
 * @brief 通过 USART2 向 K230D 发送原始数据
 * @param data   待发送数据
 * @param length 发送长度
 * @return HAL UART 发送状态
 * @note
 * 目前第一阶段主要使用接收功能，
 * 该接口为后续ACK或自定义协议预留。
 */
HAL_StatusTypeDef BSP_UART2_Write(const uint8_t *data,
                                 uint16_t length);

/** 启用 USART2 字节接收中断；在 FaceTask 启动时调用一次并丢弃旧字节。 */
void BSP_UART2_StartBufferedRx(void);
/** 非阻塞取出最多 capacity 字节，返回实际长度；只允许 FaceTask 消费。 */
uint16_t BSP_UART2_ReadBuffered(uint8_t *data, uint16_t capacity);
/** 返回累计硬件错误/环形缓冲溢出次数；变化时上层重置 parser 并锁定认证。 */
uint32_t BSP_UART2_RxErrors(void);
/** 丢弃不连续的残留数据；不清累计错误计数。 */
void BSP_UART2_DiscardBuffered(void);
/** 由 USART2_IRQHandler 调用；ISR 只存字节/计数，不解析、不打印。 */
void BSP_UART2_IRQHandler(void);
#endif
