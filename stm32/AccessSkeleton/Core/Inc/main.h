/**
 * @file main.h
 * 文件职责：系统错误处理声明；使用其他模块仍应包含对应模块的头文件。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef MAIN_H
#define MAIN_H
#include "stm32f1xx_hal.h"
/* 系统初始化失败时关闭中断并停机。 */
/**
 * @brief 关闭中断并停在死循环，保留现场便于调试器检查初始化故障。
 * 调用时机：由发现严重错误的初始化路径调用。
 * @return 不返回。
 * 使用示例：
 *   if (BSP_Init() != HAL_OK) { Error_Handler(); }
 * @note 易错点：进入后 TIM2/SysTick 不能推进，不要再用依赖中断的 HAL_Delay 或阻塞日志等待；当前函数不输出故障原因，需检查调用位置。
 */
void Error_Handler(void);
#endif
