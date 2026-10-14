/**
 * @file bsp_key.h
 * 文件职责：按键标识与状态接口：按下=0，松开=1。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef BSP_KEY_H
#define BSP_KEY_H
typedef enum { KEY1 = 0 } BSP_Key_t;
/* 保留原 GPIO 电平含义：0 为按下，1 为松开。 */
typedef enum { BSP_KEY_PRESSED = 0, BSP_KEY_RELEASED = 1 } BSP_KeyState_t;
/**
 * @brief 将 PA1 配为输入，使用板上外部上拉读取 KEY1。
 * 调用时机：使用按键读取接口之前调用，通常由 BSP_Init 调用。
 * @return 无。
 * 使用示例：
 *   BSP_Key_Init();
 * @note 易错点：GPIO_NOPULL 不会开启内部上拉；外部上拉断开会导致输入悬空。按下为低电平。
 */
void BSP_Key_Init(void);
/* 只读取一次引脚状态；消抖和等待松开由 KeyTask 负责。 */
/**
 * @brief 读取 KEY1 的瞬时电平，并返回按下或松开状态。
 * 调用时机：按键初始化完成后调用；消抖由 KeyTask 负责。
 * @param key 按键标识，当前只支持 KEY1。
 * @return BSP_KEY_PRESSED=0；BSP_KEY_RELEASED=1。未知标识也返回松开。
 * 使用示例：
 *   if (BSP_Key_Read(KEY1) == BSP_KEY_PRESSED) {  }
 * @note 易错点：该接口不消抖、不等待松开。按下值为 0，不能直接用 if(BSP_Key_Read(KEY1)) 判断按下。
 */
BSP_KeyState_t BSP_Key_Read(BSP_Key_t key);
#endif
