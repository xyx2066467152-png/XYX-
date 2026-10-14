/**
 * @file log.c
 * 文件职责：标准 printf 的 USART1 重定向及原始文本发送。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
/* 日志输出适配：格式化使用标准 printf，实际发送交给板级串口。 */
#include "log.h"
#include "bsp_uart.h"

/**
 * @brief 将标准 printf 输出的单个字符重定向到 USART1。
 * 调用时机：由 C 库自动调用，一般不需要业务代码直接调用。
 * @param ch 要发送的字符，内部只发送低 8 位。
 * @param stream 标准输出流参数，当前实现不区分流。
 * @return 返回原字符；当前忽略底层发送状态，不报告发送错误。
 * 使用示例：
 *   printf("KEY1 pressed\r\n");
 * @note 易错点：串口须先初始化，Keil 应保持 Use MicroLIB。阻塞发送会消耗任务时间和栈；没有日志锁，两个任务的文本可能交错。不要放在 ISR 或故障死循环内。
 */
int fputc(int ch, FILE *stream)
{
    uint8_t byte = (uint8_t)ch;
    (void)stream;
    /* 与原工程一致：阻塞发送一个字符，返回原字符。 */
    /* 当前有意保持原接口行为，发送失败不会通过 fputc 返回值反映出来。 */
    (void)BSP_UART_Write(&byte, 1U);
    return ch;
}

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
void Log_Write(const char *text, uint16_t length)
{
    /* 保留原启动消息一次发送整个缓冲区的方式。 */
    (void)BSP_UART_Write((const uint8_t *)text, length);
}
