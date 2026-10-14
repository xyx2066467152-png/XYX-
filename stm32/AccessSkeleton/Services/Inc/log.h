/**
 * @file log.h
 * 文件职责：日志使用入口，调用前必须先初始化板级串口。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef LOG_H
#define LOG_H
#include <stdio.h>
#include <stdint.h>
/*
 * BSP_Init 成功后可直接使用 printf；log.c 的 fputc 将字符送到 USART1。
 * 保持原工程的同步阻塞方式，不增加互斥锁或日志任务；不用于中断服务函数。
 */
/**
 * @brief 通过 USART1 一次阻塞发送指定长度的文本缓冲区。
 * 调用时机：板级串口初始化成功后调用。
 * @param text 有效的文本地址，至少 length 字节。
 * @param length 发送长度；使用 sizeof(字符数组)-1 排除结尾零字节。
 * @return 无；当前实现忽略发送状态。
 * 使用示例：
 *   const char msg[] = "Ready\r\n"; Log_Write(msg, sizeof(msg) - 1U);
 * @note 易错点：它不是 printf，不解释格式字符串，也不自动换行；无空指针保护或并发互斥。若 msg 是指针，sizeof(msg) 是指针长度，不能当文本长度。
 */
void Log_Write(const char *text, uint16_t length);
#endif
