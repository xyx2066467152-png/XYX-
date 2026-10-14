/**
 * @file stm32f1xx_it.h
 * 文件职责：异常及 TIM2 ISR 声明；名称必须与启动向量表一致。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef STM32F1XX_IT_H
#define STM32F1XX_IT_H
/**
 * @brief 保留不可屏蔽中断的现场，进入死循环等待调试。
 * 调用时机：由 CPU 异常入口自动调用。
 * @return 不返回。
 * 使用示例：
 *   无需手动调用；用调试器定位异常来源。
 * @note 易错点：这是停机占位实现，不会自动恢复或重启系统。
 */
void NMI_Handler(void);
/**
 * @brief 在硬故障发生后停机，便于定位非法访问或异常指令。
 * 调用时机：由 CPU 自动调用。
 * @return 不返回。
 * 使用示例：
 *   若程序完全无响应，可用调试器检查是否停在此函数。
 * @note 易错点：常见排查方向是越界、空指针、栈破坏和半主机请求；当前没有自动保存 PC/LR 的故障分析代码。
 */
void HardFault_Handler(void);
/**
 * @brief 在内存管理异常发生后保留现场并停机。
 * 调用时机：由 CPU 自动调用，是否启用取决于异常控制配置。
 * @return 不返回。
 * 使用示例：
 *   无需手动调用。
 * @note 易错点：某些异常未独立使能时可能升级为 HardFault；本函数不输出诊断。
 */
void MemManage_Handler(void);
/**
 * @brief 在总线访问异常发生后停机，供调试器检查。
 * 调用时机：由 CPU 自动调用。
 * @return 不返回。
 * 使用示例：
 *   检查外设地址与 SRAM/Flash 边界，不在业务中调用。
 * @note 易错点：异常若未独立使能可能升级为 HardFault；死循环不代表系统仍正常执行任务。
 */
void BusFault_Handler(void);
/**
 * @brief 在指令使用异常发生后停机，例如非法指令或异常返回状态。
 * 调用时机：由 CPU 自动调用。
 * @return 不返回。
 * 使用示例：
 *   无需手动调用，通过调试器检查异常状态寄存器。
 * @note 易错点：是否把除零等操作变成异常由 CPU 配置决定，不能仅凭此入口认定已启用所有检查。
 */
void UsageFault_Handler(void);
/**
 * @brief 处理 TIM2 更新中断，清除标志后推进 HAL 毫秒计数。
 * 调用时机：由硬件通过启动向量表自动调用，不从任务中手动调用。
 * @return 无。
 * 使用示例：
 *   无需业务调用；HAL_Delay/HAL_GetTick 依赖它正常触发。
 * @note 易错点：必须同时检查标志与中断使能并及时清标志。优先级 4 高于 RTOS 可调用接口的边界，不调用 FreeRTOS FromISR API，也不在此打印日志。
 */
void TIM2_IRQHandler(void);
/* SVC、PendSV 和 SysTick 入口由 FreeRTOS Cortex-M3 port 提供，此处不重复定义。 */
#endif
