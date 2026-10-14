#ifndef __SERVO_H
#define __SERVO_H

/**
 * @brief 初始化门禁舵机
 *
 * @用途
 *   初始化底层 PWM，并让门在上电后进入默认关闭状态。
 *
 * @使用方法
 *
 *   系统启动阶段调用一次：
 *
 *       Servo_Init();
 *
 * @注意
 *   不需要 App 层再单独调用 BSP_Servo_Init()。
 *   Servo_Init() 内部已经完成。
 */
void Servo_Init(void);


/**
 * @brief 打开门锁
 *
 * @用途
 *   控制舵机转到预先设定的“开门位置”。
 *
 * @注意
 *   当前开门角度由 servo.c 中的
 *   SERVO_OPEN_PULSE_US 决定。
 */
void Servo_Open(void);


/**
 * @brief 关闭门锁
 *
 * @用途
 *   控制舵机转到预先设定的“关门位置”。
 */
void Servo_Close(void);

#endif
