#include "bsp_servo.h"
#include "main.h"

/*
 * TIM4句柄。
 *
 * 使用 static 的原因：
 *   这个句柄只属于 bsp_servo.c 内部，
 *   不希望其他模块直接操作 TIM4。
 *
 * 上层如果想控制舵机，
 * 应通过 BSP_Servo_SetPulse()。
 */
static TIM_HandleTypeDef htim4;


/**
 * @brief 初始化 PB8 + TIM4_CH3 PWM
 */
void BSP_Servo_Init(uint16_t initial_pulse_us)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_OC_InitTypeDef pwm = {0};

    /*
     * 1. 开启 GPIOB 和 TIM4 外设时钟
     *
     * 外设没有时钟就无法工作。
     */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();


    /*
     * 2. 配置 PB8
     *
     * PB8 默认可以作为 TIM4_CH3。
     *
     * GPIO_MODE_AF_PP：
     *   Alternate Function Push-Pull
     *   复用推挽输出
     *
     * 也就是说 PB8 的电平不再由普通 GPIO 控制，
     * 而由 TIM4_CH3 的 PWM 外设控制。
     */
    gpio.Pin = GPIO_PIN_8;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(GPIOB, &gpio);


    /*
     * 3. 配置 TIM4 基本计数参数
     *
     * 当前 TIM4 时钟 = 8 MHz
     *
     * 计数频率：
     *
     *     8 MHz / (Prescaler + 1)
     *
     * Prescaler = 7：
     *
     *     8 MHz / 8 = 1 MHz
     *
     * 因此：
     *
     *     1个计数周期 = 1 us
     */
    htim4.Instance = TIM4;

    htim4.Init.Prescaler = 7;

    /*
     * 向上计数：
     *
     * 0 -> 1 -> 2 -> ... -> 19999 -> 0
     */
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;

    /*
     * ARR = 19999
     *
     * 实际计数数量：
     *
     *     19999 + 1 = 20000
     *
     * 每个计数 = 1 us
     *
     * 所以：
     *
     *     20000 us = 20 ms
     *
     * PWM频率：
     *
     *     1 / 20 ms = 50 Hz
     */
    htim4.Init.Period = 19999;

    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;

    htim4.Init.AutoReloadPreload =
        TIM_AUTORELOAD_PRELOAD_DISABLE;


    /*
     * 初始化 TIM4 PWM 功能。
     *
     * 如果失败，说明初始化过程有异常，
     * 直接进入 Error_Handler()。
     */
    if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
    {
        Error_Handler();
    }


    /*
     * 4. 配置 TIM4 Channel 3
     *
     * PWM1模式：
     *
     * 在一个PWM周期内：
     *
     * CNT < CCR3
     *     输出高电平
     *
     * CNT >= CCR3
     *     输出低电平
     */
    pwm.OCMode = TIM_OCMODE_PWM1;


    /*
     * 初始脉宽 = 1500 us
     *
     * 因为现在：
     *
     *     1 Count = 1 us
     *
     * 所以：
     *
     *     CCR3 = 1500
     *
     * 就代表高电平持续 1500 us。
     */
     if (initial_pulse_us < 1000U)
    {
        initial_pulse_us = 1000U;
    }

    if (initial_pulse_us > 2000U)
    {
        initial_pulse_us = 2000U;
    }

    /*
     * 关键：
     * PWM启动时直接使用目标初始位置，
     * 不再固定从1500us开始。
     */
    pwm.Pulse = initial_pulse_us;

    /*
     * PWM有效电平为高电平。
     */
    pwm.OCPolarity = TIM_OCPOLARITY_HIGH;

    pwm.OCFastMode = TIM_OCFAST_DISABLE;


    /*
     * 把上述 PWM 配置应用到 TIM4_CH3。
     */
    if (HAL_TIM_PWM_ConfigChannel(
            &htim4,
            &pwm,
            TIM_CHANNEL_3) != HAL_OK)
    {
        Error_Handler();
    }


    /*
     * 5. 启动 TIM4_CH3 PWM
     *
     * 如果不调用 Start，
     * 即使前面的配置全部正确，
     * PB8 也不会持续输出 PWM。
     */
    if (HAL_TIM_PWM_Start(
            &htim4,
            TIM_CHANNEL_3) != HAL_OK)
    {
        Error_Handler();
    }
}


/**
 * @brief 设置舵机PWM脉宽
 */
void BSP_Servo_SetPulse(uint16_t pulse_us)
{
    /*
     * 当前先限制在 1000~2000 us。
     *
     * 原因：
     *   防止测试阶段直接给出过大的机械角度，
     *   导致舵机顶到机械极限后持续堵转。
     */
    if (pulse_us < 1000)
    {
        pulse_us = 1000;
    }

    if (pulse_us > 2000)
    {
        pulse_us = 2000;
    }


    /*
     * 修改 TIM4_CH3 的 CCR3。
     *
     * 因为当前：
     *
     *     1 Count = 1 us
     *
     * 所以 pulse_us 可以直接作为 CCR3。
     *
     * 例如：
     *
     * pulse_us = 1000
     *     -> 高电平 1 ms
     *
     * pulse_us = 1500
     *     -> 高电平 1.5 ms
     *
     * pulse_us = 2000
     *     -> 高电平 2 ms
     */
    __HAL_TIM_SET_COMPARE(
        &htim4,
        TIM_CHANNEL_3,
        pulse_us
    );
}
