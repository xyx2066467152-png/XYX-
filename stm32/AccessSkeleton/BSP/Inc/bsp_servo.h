#ifndef __BSP_SERVO_H
#define __BSP_SERVO_H

#include "stm32f1xx_hal.h"

/**
 * @brief  初始化舵机PWM，并指定PWM首次启动时的脉宽
 *
 * @param  initial_pulse_us
 *         舵机上电后首次输出的PWM高电平脉宽，单位us。
 *
 * @用途
 *   让PWM从一开始就输出正确的门锁位置，
 *   避免先输出默认1500us，再突然切到关门位置造成舵机抽动。
 *
 * @注意
 *   当前函数内部会将脉宽限制到1000~2000us。
 */
void BSP_Servo_Init(uint16_t initial_pulse_us);


/**
 * @brief  修改舵机PWM高电平脉宽
 *
 * @param  pulse_us
 *         PWM高电平脉宽，单位us。
 */
void BSP_Servo_SetPulse(uint16_t pulse_us);

#endif
