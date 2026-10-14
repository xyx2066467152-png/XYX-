/**
 * @file oled_ssd1315.h
 * 文件职责：OLED 公共接口；绘制只改缓冲，Clear 与 UpdateScreen 实际发送。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#ifndef __OLED_SSD1315_H
#define __OLED_SSD1315_H

#include "stm32f1xx_hal.h"

/**
 * @brief 设置或清除缓冲区中的一个像素，不立即更新屏幕。
 * 调用时机：构建一帧画面时使用，最后统一 UpdateScreen。
 * @param x 水平像素坐标，0～127。
 * @param y 垂直像素坐标，0～63，单位是像素而不是页。
 * @param on 0 清除像素，其他非零值点亮。
 * @return 无；超出屏幕范围时直接返回。
 * 使用示例：
 *   OLED_SetPixel(10, 10, 1); OLED_UpdateScreen();
 * @note 易错点：索引=x+(y/8)*128，字节中的位号=y%8。单字节修改是读改写操作，多个任务并发画同一缓冲会互相覆盖。
 */
void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on);
/**
 * @brief 设置完整显存范围，把当前 1024 字节 RAM 缓冲区按 8 页发送到屏幕。
 * 调用时机：一帧中所有 SetPixel/ShowChar/ShowString 完成后调用一次。
 * @return 全部命令和页数据发送成功返回 HAL_OK，否则返回 HAL_ERROR。
 * 使用示例：
 *   OLED_ShowString(0, 0, "Ready"); if (OLED_UpdateScreen() != HAL_OK) {  }
 * @note 易错点：同步阻塞刷新，不是原子帧切换；中途通信失败或其他任务修改缓冲时，屏幕可能部分更新或画面混杂。刷新不会清空缓冲区。
 */
HAL_StatusTypeDef OLED_UpdateScreen(void);

/* 6×8 可打印 ASCII；x、y 为像素坐标，仅修改现有 oled_buffer。
 * 字符覆盖原 6×8 区域；边缘裁剪；不支持的字符显示 '?'。
 */
/**
 * @brief 把一个 6×8 ASCII 字符写入现有缓冲区，覆盖该字符区域的亮点与暗点。
 * 调用时机：OLED 初始化后的绘制阶段调用，完成一组绘制后再刷新。
 * @param x 字符左上角水平像素坐标。
 * @param y 字符左上角垂直像素坐标；不要求 8 的倍数。
 * @param ch 可打印 ASCII 0x20～0x7E，其他字节显示问号。
 * @return 无；边缘部分裁剪，起点越界时不写入。
 * 使用示例：
 *   OLED_ShowChar(0, 0, 'A'); OLED_UpdateScreen();
 * @note 易错点：不发送 I2C。字库第六列是间距，第八行留白；空格会擦掉该 6×8 区域。char 与字符串是不同参数类型。
 */
void OLED_ShowChar(uint8_t x, uint8_t y, char ch);
/* 不自动换行，到右边界停止；空指针不执行任何操作。 */
/**
 * @brief 从指定像素位置连续写入 ASCII 文本，每字符前进 6 像素。
 * 调用时机：绘制状态文本时调用；所有文字写好后统一刷新。
 * @param x 首字符左上角水平像素坐标。
 * @param y 首字符左上角垂直像素坐标。
 * @param str 以零字节结束的有效字符串；NULL 时不操作。
 * @return 无；到字符串结尾或屏幕右边界停止，不自动换行。
 * 使用示例：
 *   OLED_ShowString(0, 0, "Access Control"); OLED_ShowString(0, 16, "System Ready"); OLED_UpdateScreen();
 * @note 易错点：不支持中文，也不把换行字符当换行；需要第二行时另传 y。把长文本替换成短文本不会擦除末尾旧字符，可先清屏或用空格补足。必须保证字符串有结束符。
 */
void OLED_ShowString(uint8_t x, uint8_t y, const char *str);

/* OLED 初始化成功后的两行使用示例：
 * OLED_Clear();
 * OLED_ShowString(0, 0, "Access Control");
 * OLED_ShowString(0, 16, "System Ready");
 * OLED_UpdateScreen();   // 两行文字写好之后统一刷新一次。
 */

/**
 * @brief 向 SSD1315 发送一条命令：控制字节 0x00 加一个命令字节。
 * 调用时机：I2C1 初始化成功后，由 OLED 初始化和寻址接口调用。
 * @param cmd SSD1315 命令或紧接前一命令的参数字节。
 * @return 原样返回 HAL I2C 发送状态。
 * 使用示例：
 *   OLED_WriteCommand(0xAE); 
 * @note 易错点：命令顺序有意义，不要在任务中随意改地址模式。OLED 的 7 位地址为 0x3C，HAL 参数已左移为 0x78，不能再移一次。
 */
HAL_StatusTypeDef OLED_WriteCommand(uint8_t cmd);
/**
 * @brief 在控制字节 0x40 后发送最多 128 字节的显存数据。
 * 调用时机：I2C/OLED 已初始化，且显存地址范围已正确设置后使用。
 * @param data 至少 size 字节的有效数据；当前没有空指针保护。
 * @param size 数据字节数，范围 0～128；0 时仍发送一个控制字节。
 * @return size>128 返回 HAL_ERROR，否则返回 I2C 发送状态。
 * 使用示例：
 *   通常由 OLED_UpdateScreen 按页调用，无需业务直接操作。
 * @note 易错点：它直接写屏幕，不同步修改 oled_buffer。手动发送后再 UpdateScreen 会用缓冲内容覆盖屏幕。局部发送数组为 129 字节，注意任务栈余量。
 */
HAL_StatusTypeDef OLED_WriteData(uint8_t *data, uint16_t size);
/**
 * @brief 按当前 SSD1315 面板参数发送显示初始化序列并开启显示。
 * 调用时机：BSP_I2C1_Init 完成后，在清屏与绘制前调用一次。
 * @return 全部命令发送成功返回 HAL_OK；任一命令失败返回 HAL_ERROR。
 * 使用示例：
 *   BSP_I2C1_Init(); if (OLED_Init() != HAL_OK) { Error_Handler(); }
 * @note 易错点：仅初始化控制器，不清空 oled_buffer 或保证屏幕初始内容。通信失败应先查供电、上拉、地址和接线，不应随意修改已验证的初始化常数。
 */
HAL_StatusTypeDef OLED_Init(void);
/**
 * @brief 把 RAM 的 oled_buffer 清零，并立即通过 I2C 清除整块屏幕。
 * 调用时机：OLED 初始化成功后，首次显示或需要完整清屏时调用。
 * @return 清屏命令和页数据全部发送成功返回 HAL_OK，否则 HAL_ERROR。
 * 使用示例：
 *   OLED_Clear(); OLED_ShowString(0, 0, "Ready"); OLED_UpdateScreen();
 * @note 易错点：此接口有实际 I2C 发送，不是纯缓冲清空。失败时 RAM 已清零，但屏幕可能只清了一部分；当前多任务使用没有保护。
 */
HAL_StatusTypeDef OLED_Clear(void);
#endif
