/**
 * @file stm32f1xx_hal_timebase_tim.c
 * 文件职责：使用 TIM2 提供 HAL 毫秒计数，保持 SysTick 给 RTOS。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
/* HAL 独立时间基准：TIM2 计数频率 1 MHz，每 1000 次计数产生 1 ms 中断。
 * APB1 分频时考虑定时器时钟倍频；中断优先级沿用 4，不调用 FreeRTOS API。
 */
#include "main.h"

TIM_HandleTypeDef htim2_hal_tick;

/**
 * @brief 用 TIM2 提供 HAL 的 1 ms 时间基准，避免与 FreeRTOS 的 SysTick 冲突。
 * 调用时机：由 HAL_Init 和 HAL 时钟配置函数自动调用；不用业务代码手动创建另一个 TIM2 tick。
 * @param TickPriority HAL 传入的优先级参数；当前实现忽略该值，固定使用优先级 4。
 * @return 初始化并开启 TIM2 中断成功返回 HAL_OK；时钟分频异常或 HAL 初始化失败返回 HAL_ERROR。
 * 使用示例：
 *   HAL_Init(); SystemClock_Config(); 
 * @note 易错点：定时器时钟在 APB1 分频时为 PCLK1 的两倍；PSC/ARR 硬件值均是目标计数减一。当前先分频到 1 MHz，再每 1000 次计数中断。TIM2 已被占用。
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    uint32_t pclk1, timer_clock;
    RCC_ClkInitTypeDef clock_config;
    uint32_t flash_latency;
    uint32_t prescaler;
    /* 固定用 4；传入的 TickPriority 不影响本实现，HAL 配置宏也不会覆盖此处。 */
    (void)TickPriority;
    HAL_RCC_GetClockConfig(&clock_config, &flash_latency);
    pclk1 = HAL_RCC_GetPCLK1Freq();
    timer_clock = (clock_config.APB1CLKDivider == RCC_HCLK_DIV1) ? pclk1 : 2U * pclk1;
    /* 当前 8 MHz 可整除 1 MHz；未来改成其他频率时需核对整除及定时误差。 */
    prescaler = timer_clock / 1000000U;
    if (prescaler == 0U) { return HAL_ERROR; }
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2_hal_tick.Instance = TIM2;
    htim2_hal_tick.Init.Prescaler = prescaler - 1U;
    htim2_hal_tick.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2_hal_tick.Init.Period = 1000U - 1U;
    htim2_hal_tick.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    if (HAL_TIM_Base_Init(&htim2_hal_tick) != HAL_OK) { return HAL_ERROR; }
    if (HAL_TIM_Base_Start_IT(&htim2_hal_tick) != HAL_OK) { return HAL_ERROR; }
    HAL_NVIC_SetPriority(TIM2_IRQn, 4U, 0U);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    return HAL_OK;
}

/**
 * @brief 关闭 TIM2 更新中断，暂停 HAL 毫秒计数。
 * 调用时机：HAL 时间基准已初始化、确实需要暂停 HAL tick 时调用。
 * @return 无。
 * 使用示例：
 *   HAL_SuspendTick(); 
 * @note 易错点：暂停后 HAL_Delay 和依赖 HAL_GetTick 的超时可能无法结束；它没有暂停 FreeRTOS 的 SysTick。
 */
void HAL_SuspendTick(void) { __HAL_TIM_DISABLE_IT(&htim2_hal_tick, TIM_IT_UPDATE); }
/**
 * @brief 重新开启 TIM2 更新中断，恢复 HAL 毫秒计数。
 * 调用时机：与 HAL_SuspendTick 配对使用。
 * @return 无。
 * 使用示例：
 *   HAL_ResumeTick();
 * @note 易错点：函数不补偿暂停期间流逝的时间，也不重新初始化定时器；调用前 TIM2 必须已配置。
 */
void HAL_ResumeTick(void) { __HAL_TIM_ENABLE_IT(&htim2_hal_tick, TIM_IT_UPDATE); }
