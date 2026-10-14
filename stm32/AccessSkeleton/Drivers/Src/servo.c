#include "servo.h"
#include "bsp_servo.h"

/*
 * 门锁机械位置对应的 PWM 脉宽。
 *
 * 当前暂时使用：
 *
 *     Close = 1000 us
 *     Open  = 2000 us
 *
 * 后续把舵机真正装到门锁机构以后，
 * 根据实际机械位置调整这两个参数即可。
 *
 * 上层业务代码完全不需要修改。
 */
#define SERVO_CLOSE_PULSE_US    1000U
#define SERVO_OPEN_PULSE_US     2000U


/**
 * @brief 初始化舵机模块
 */
/**
 * @brief 初始化门禁舵机
 *
 * @用途
 *   初始化 PB8 / TIM4_CH3 的 PWM。
 *
 *   PWM启动时就直接使用“关门位置”的脉宽，
 *   避免先输出1500us、随后马上跳到1000us造成上电抽动。
 *
 * @注意
 *   此函数只需要在系统启动阶段调用一次。
 */
void Servo_Init(void)
{
    /*
     * 系统上电后直接从关门位置开始输出PWM。
     */
    BSP_Servo_Init(SERVO_CLOSE_PULSE_US);
}

/**
 * @brief 开门
 */
void Servo_Open(void)
{
    /*
     * 上层不需要知道 2000 us 这种硬件参数。
     *
     * 如果以后实际安装后发现 1800 us
     * 才是正确开门位置，只需要修改宏即可。
     */
    BSP_Servo_SetPulse(SERVO_OPEN_PULSE_US);
}


/**
 * @brief 关门
 */
void Servo_Close(void)
{
    BSP_Servo_SetPulse(SERVO_CLOSE_PULSE_US);
}
