/**
 * @file oled_ssd1315.c
 * 文件职责：SSD1315 控制器初始化、1024 字节帧缓冲和 6×8 ASCII 显示。
 * 阅读顺序：先看头文件中的接口说明，再看对应 .c 的实现和调用限制。
 */
#include "oled_ssd1315.h"
#include "bsp_i2c.h"
#include <string.h>


#define OLED_I2C_ADDR      (0x3C << 1)
#define OLED_CONTROL_CMD   0x00
#define OLED_CONTROL_DATA  0x40
#define OLED_WIDTH     128
#define OLED_HEIGHT    64
#define OLED_PAGES     8

/* 页式显存：第 page 页覆盖 y=page*8～page*8+7；每列一字节，共 128*8=1024 字节。 */
static uint8_t oled_buffer[OLED_WIDTH * OLED_PAGES];

#define OLED_FONT_WIDTH   6U
#define OLED_FONT_HEIGHT  8U

/* 可打印 ASCII 0x20～0x7E：每个字节表示一列，bit0 对应字形顶部。
 * 前五列为字形，第六列为间隔；字库放在 Flash，不额外占用 RAM 缓冲区。
 */
static const uint8_t oled_font6x8[95][OLED_FONT_WIDTH] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ASCII 0x20 */
    {0x00, 0x00, 0x5F, 0x00, 0x00, 0x00}, /* ASCII 0x21 */
    {0x00, 0x07, 0x00, 0x07, 0x00, 0x00}, /* ASCII 0x22 */
    {0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00}, /* ASCII 0x23 */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00}, /* ASCII 0x24 */
    {0x13, 0x0B, 0x34, 0x32, 0x01, 0x00}, /* ASCII 0x25 */
    {0x36, 0x49, 0x55, 0x22, 0x50, 0x00}, /* ASCII 0x26 */
    {0x00, 0x04, 0x03, 0x00, 0x00, 0x00}, /* ASCII 0x27 */
    {0x00, 0x1C, 0x22, 0x41, 0x00, 0x00}, /* ASCII 0x28 */
    {0x00, 0x41, 0x22, 0x1C, 0x00, 0x00}, /* ASCII 0x29 */
    {0x2A, 0x1C, 0x3E, 0x1C, 0x2A, 0x00}, /* ASCII 0x2A */
    {0x08, 0x08, 0x3E, 0x08, 0x08, 0x00}, /* ASCII 0x2B */
    {0x00, 0x40, 0x30, 0x10, 0x00, 0x00}, /* ASCII 0x2C */
    {0x08, 0x08, 0x08, 0x08, 0x08, 0x00}, /* ASCII 0x2D */
    {0x00, 0x00, 0x60, 0x60, 0x00, 0x00}, /* ASCII 0x2E */
    {0x20, 0x10, 0x0C, 0x02, 0x01, 0x00}, /* ASCII 0x2F */
    {0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00}, /* ASCII 0x30 */
    {0x00, 0x42, 0x7F, 0x40, 0x00, 0x00}, /* ASCII 0x31 */
    {0x42, 0x61, 0x51, 0x49, 0x46, 0x00}, /* ASCII 0x32 */
    {0x41, 0x49, 0x49, 0x49, 0x36, 0x00}, /* ASCII 0x33 */
    {0x18, 0x14, 0x12, 0x7F, 0x10, 0x00}, /* ASCII 0x34 */
    {0x4F, 0x49, 0x49, 0x49, 0x31, 0x00}, /* ASCII 0x35 */
    {0x3E, 0x49, 0x49, 0x49, 0x30, 0x00}, /* ASCII 0x36 */
    {0x01, 0x71, 0x09, 0x05, 0x03, 0x00}, /* ASCII 0x37 */
    {0x36, 0x49, 0x49, 0x49, 0x36, 0x00}, /* ASCII 0x38 */
    {0x06, 0x49, 0x49, 0x49, 0x3E, 0x00}, /* ASCII 0x39 */
    {0x00, 0x00, 0x36, 0x36, 0x00, 0x00}, /* ASCII 0x3A */
    {0x00, 0x40, 0x36, 0x16, 0x00, 0x00}, /* ASCII 0x3B */
    {0x08, 0x14, 0x22, 0x41, 0x00, 0x00}, /* ASCII 0x3C */
    {0x14, 0x14, 0x14, 0x14, 0x14, 0x00}, /* ASCII 0x3D */
    {0x00, 0x41, 0x22, 0x14, 0x08, 0x00}, /* ASCII 0x3E */
    {0x02, 0x01, 0x51, 0x09, 0x06, 0x00}, /* ASCII 0x3F */
    {0x3E, 0x41, 0x5D, 0x55, 0x1E, 0x00}, /* ASCII 0x40 */
    {0x7E, 0x09, 0x09, 0x09, 0x7E, 0x00}, /* ASCII 0x41 */
    {0x7F, 0x49, 0x49, 0x49, 0x36, 0x00}, /* ASCII 0x42 */
    {0x3E, 0x41, 0x41, 0x41, 0x41, 0x00}, /* ASCII 0x43 */
    {0x7F, 0x41, 0x41, 0x41, 0x3E, 0x00}, /* ASCII 0x44 */
    {0x7F, 0x49, 0x49, 0x49, 0x41, 0x00}, /* ASCII 0x45 */
    {0x7F, 0x09, 0x09, 0x09, 0x01, 0x00}, /* ASCII 0x46 */
    {0x3E, 0x41, 0x49, 0x49, 0x3A, 0x00}, /* ASCII 0x47 */
    {0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00}, /* ASCII 0x48 */
    {0x00, 0x41, 0x7F, 0x41, 0x00, 0x00}, /* ASCII 0x49 */
    {0x30, 0x40, 0x41, 0x3F, 0x01, 0x00}, /* ASCII 0x4A */
    {0x7F, 0x08, 0x14, 0x22, 0x41, 0x00}, /* ASCII 0x4B */
    {0x7F, 0x40, 0x40, 0x40, 0x40, 0x00}, /* ASCII 0x4C */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00}, /* ASCII 0x4D */
    {0x7F, 0x02, 0x04, 0x08, 0x7F, 0x00}, /* ASCII 0x4E */
    {0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00}, /* ASCII 0x4F */
    {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00}, /* ASCII 0x50 */
    {0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00}, /* ASCII 0x51 */
    {0x7F, 0x09, 0x19, 0x29, 0x46, 0x00}, /* ASCII 0x52 */
    {0x46, 0x49, 0x49, 0x49, 0x31, 0x00}, /* ASCII 0x53 */
    {0x01, 0x01, 0x7F, 0x01, 0x01, 0x00}, /* ASCII 0x54 */
    {0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00}, /* ASCII 0x55 */
    {0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00}, /* ASCII 0x56 */
    {0x7F, 0x20, 0x18, 0x20, 0x7F, 0x00}, /* ASCII 0x57 */
    {0x63, 0x14, 0x08, 0x14, 0x63, 0x00}, /* ASCII 0x58 */
    {0x03, 0x04, 0x78, 0x04, 0x03, 0x00}, /* ASCII 0x59 */
    {0x61, 0x51, 0x49, 0x45, 0x43, 0x00}, /* ASCII 0x5A */
    {0x00, 0x7F, 0x41, 0x41, 0x00, 0x00}, /* ASCII 0x5B */
    {0x01, 0x02, 0x0C, 0x10, 0x20, 0x00}, /* ASCII 0x5C */
    {0x00, 0x41, 0x41, 0x7F, 0x00, 0x00}, /* ASCII 0x5D */
    {0x04, 0x02, 0x01, 0x02, 0x04, 0x00}, /* ASCII 0x5E */
    {0x40, 0x40, 0x40, 0x40, 0x40, 0x00}, /* ASCII 0x5F */
    {0x00, 0x01, 0x02, 0x04, 0x00, 0x00}, /* ASCII 0x60 */
    {0x20, 0x54, 0x54, 0x54, 0x78, 0x00}, /* ASCII 0x61 */
    {0x7F, 0x48, 0x44, 0x44, 0x38, 0x00}, /* ASCII 0x62 */
    {0x38, 0x44, 0x44, 0x44, 0x28, 0x00}, /* ASCII 0x63 */
    {0x38, 0x44, 0x44, 0x48, 0x7F, 0x00}, /* ASCII 0x64 */
    {0x38, 0x54, 0x54, 0x54, 0x18, 0x00}, /* ASCII 0x65 */
    {0x08, 0x7E, 0x09, 0x01, 0x02, 0x00}, /* ASCII 0x66 */
    {0x0C, 0x52, 0x52, 0x52, 0x3E, 0x00}, /* ASCII 0x67 */
    {0x7F, 0x08, 0x04, 0x04, 0x78, 0x00}, /* ASCII 0x68 */
    {0x00, 0x44, 0x7D, 0x40, 0x00, 0x00}, /* ASCII 0x69 */
    {0x20, 0x40, 0x44, 0x3D, 0x00, 0x00}, /* ASCII 0x6A */
    {0x7F, 0x10, 0x28, 0x44, 0x00, 0x00}, /* ASCII 0x6B */
    {0x00, 0x41, 0x7F, 0x40, 0x00, 0x00}, /* ASCII 0x6C */
    {0x7C, 0x04, 0x78, 0x04, 0x78, 0x00}, /* ASCII 0x6D */
    {0x7C, 0x08, 0x04, 0x04, 0x78, 0x00}, /* ASCII 0x6E */
    {0x38, 0x44, 0x44, 0x44, 0x38, 0x00}, /* ASCII 0x6F */
    {0x7E, 0x12, 0x12, 0x12, 0x0C, 0x00}, /* ASCII 0x70 */
    {0x0C, 0x12, 0x12, 0x12, 0x7E, 0x00}, /* ASCII 0x71 */
    {0x7C, 0x08, 0x04, 0x04, 0x08, 0x00}, /* ASCII 0x72 */
    {0x48, 0x54, 0x54, 0x54, 0x24, 0x00}, /* ASCII 0x73 */
    {0x04, 0x3F, 0x44, 0x40, 0x20, 0x00}, /* ASCII 0x74 */
    {0x3C, 0x40, 0x40, 0x20, 0x7C, 0x00}, /* ASCII 0x75 */
    {0x1C, 0x20, 0x40, 0x20, 0x1C, 0x00}, /* ASCII 0x76 */
    {0x3C, 0x40, 0x30, 0x40, 0x3C, 0x00}, /* ASCII 0x77 */
    {0x44, 0x28, 0x10, 0x28, 0x44, 0x00}, /* ASCII 0x78 */
    {0x0E, 0x50, 0x50, 0x50, 0x3E, 0x00}, /* ASCII 0x79 */
    {0x44, 0x64, 0x54, 0x4C, 0x44, 0x00}, /* ASCII 0x7A */
    {0x00, 0x08, 0x36, 0x41, 0x00, 0x00}, /* ASCII 0x7B */
    {0x00, 0x00, 0x7F, 0x00, 0x00, 0x00}, /* ASCII 0x7C */
    {0x00, 0x41, 0x36, 0x08, 0x00, 0x00}, /* ASCII 0x7D */
    {0x08, 0x04, 0x08, 0x08, 0x04, 0x00}, /* ASCII 0x7E */
};


/**
 * @brief 向 SSD1315 发送一条命令：控制字节 0x00 加一个命令字节。
 * 调用时机：I2C1 初始化成功后，由 OLED 初始化和寻址接口调用。
 * @param cmd SSD1315 命令或紧接前一命令的参数字节。
 * @return 原样返回 HAL I2C 发送状态。
 * 使用示例：
 *   OLED_WriteCommand(0xAE); 
 * @note 易错点：命令顺序有意义，不要在任务中随意改地址模式。OLED 的 7 位地址为 0x3C，HAL 参数已左移为 0x78，不能再移一次。
 */
HAL_StatusTypeDef OLED_WriteCommand(uint8_t cmd)
{
    uint8_t buffer[2];

    buffer[0] = OLED_CONTROL_CMD;
    buffer[1] = cmd;

    return HAL_I2C_Master_Transmit(
        BSP_I2C1_GetHandle(),
        OLED_I2C_ADDR,
        buffer,
        2,
        100
    );
}
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
HAL_StatusTypeDef OLED_WriteData(uint8_t *data, uint16_t size)
{
    uint8_t buffer[129];
    uint16_t i;

    if (size > 128)
    {
        return HAL_ERROR;
    }

    buffer[0] = OLED_CONTROL_DATA;

    for (i = 0; i < size; i++)
    {
        buffer[i + 1] = data[i];
    }

    return HAL_I2C_Master_Transmit(
        BSP_I2C1_GetHandle(),
        OLED_I2C_ADDR,
        buffer,
        size + 1,
        100
    );
}
/**
 * @brief 按当前 SSD1315 面板参数发送显示初始化序列并开启显示。
 * 调用时机：BSP_I2C1_Init 完成后，在清屏与绘制前调用一次。
 * @return 全部命令发送成功返回 HAL_OK；任一命令失败返回 HAL_ERROR。
 * 使用示例：
 *   BSP_I2C1_Init(); if (OLED_Init() != HAL_OK) { Error_Handler(); }
 * @note 易错点：仅初始化控制器，不清空 oled_buffer 或保证屏幕初始内容。通信失败应先查供电、上拉、地址和接线，不应随意修改已验证的初始化常数。
 */
HAL_StatusTypeDef OLED_Init(void)
{
    if (OLED_WriteCommand(0xAE) != HAL_OK) return HAL_ERROR;  // 关闭显示

    if (OLED_WriteCommand(0xD5) != HAL_OK) return HAL_ERROR;  // 设置显示时钟
    if (OLED_WriteCommand(0x80) != HAL_OK) return HAL_ERROR;

    if (OLED_WriteCommand(0xA8) != HAL_OK) return HAL_ERROR;  // 设置复用扫描行数
    if (OLED_WriteCommand(0x3F) != HAL_OK) return HAL_ERROR;  // 64 行显示

    if (OLED_WriteCommand(0xD3) != HAL_OK) return HAL_ERROR;  // 设置显示偏移
    if (OLED_WriteCommand(0x00) != HAL_OK) return HAL_ERROR;

    if (OLED_WriteCommand(0x40) != HAL_OK) return HAL_ERROR;  // 显示起始行为 0

    if (OLED_WriteCommand(0x8D) != HAL_OK) return HAL_ERROR;  // 配置电荷泵
    if (OLED_WriteCommand(0x14) != HAL_OK) return HAL_ERROR;

    if (OLED_WriteCommand(0x20) != HAL_OK) return HAL_ERROR;  // 设置显存寻址模式
    if (OLED_WriteCommand(0x00) != HAL_OK) return HAL_ERROR;  // 水平寻址模式

    if (OLED_WriteCommand(0xA1) != HAL_OK) return HAL_ERROR;  // 列地址映射

    if (OLED_WriteCommand(0xC8) != HAL_OK) return HAL_ERROR;  // COM 扫描方向

    if (OLED_WriteCommand(0xDA) != HAL_OK) return HAL_ERROR;  // COM 引脚配置
    if (OLED_WriteCommand(0x12) != HAL_OK) return HAL_ERROR;

    if (OLED_WriteCommand(0x81) != HAL_OK) return HAL_ERROR;  // 设置对比度
    if (OLED_WriteCommand(0x7F) != HAL_OK) return HAL_ERROR;

    if (OLED_WriteCommand(0xD9) != HAL_OK) return HAL_ERROR;  // 设置预充电周期
    if (OLED_WriteCommand(0xF1) != HAL_OK) return HAL_ERROR;

    if (OLED_WriteCommand(0xDB) != HAL_OK) return HAL_ERROR;  // 设置 VCOMH 电平
    if (OLED_WriteCommand(0x40) != HAL_OK) return HAL_ERROR;

    if (OLED_WriteCommand(0xA4) != HAL_OK) return HAL_ERROR;  // 显示显存内容
    if (OLED_WriteCommand(0xA6) != HAL_OK) return HAL_ERROR;  // 正常显示，非反色

    if (OLED_WriteCommand(0xAF) != HAL_OK) return HAL_ERROR;  // 开启显示

    return HAL_OK;
}

/**
 * @brief 把 RAM 的 oled_buffer 清零，并立即通过 I2C 清除整块屏幕。
 * 调用时机：OLED 初始化成功后，首次显示或需要完整清屏时调用。
 * @return 清屏命令和页数据全部发送成功返回 HAL_OK，否则 HAL_ERROR。
 * 使用示例：
 *   OLED_Clear(); OLED_ShowString(0, 0, "Ready"); OLED_UpdateScreen();
 * @note 易错点：此接口有实际 I2C 发送，不是纯缓冲清空。失败时 RAM 已清零，但屏幕可能只清了一部分；当前多任务使用没有保护。
 */
HAL_StatusTypeDef OLED_Clear(void)
{
    uint8_t clear_data[128] = {0};
    memset(oled_buffer,0,sizeof(oled_buffer));
    // for(uint8_t i=0;i<128;i++)
    // {
    //     clear_data[i]=0x00+i;
    // }
    uint8_t page;

    /* 列地址范围：0～127 */
    if (OLED_WriteCommand(0x21) != HAL_OK) return HAL_ERROR;
    if (OLED_WriteCommand(0x00) != HAL_OK) return HAL_ERROR;
    if (OLED_WriteCommand(0x7F) != HAL_OK) return HAL_ERROR;

    /* 页地址范围：0～7 */
    if (OLED_WriteCommand(0x22) != HAL_OK) return HAL_ERROR;
    if (OLED_WriteCommand(0x00) != HAL_OK) return HAL_ERROR;
    if (OLED_WriteCommand(0x07) != HAL_OK) return HAL_ERROR;

    for (page = 0; page < 8; page++)
    {
        if (OLED_WriteData(clear_data, 128) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}

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
void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on)
{
    uint16_t index;
    uint8_t bit;

    if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
    {
        return;
    }

    /* 除以 8 选页，取余数选该字节的位；坐标检查必须先于数组访问。 */
    index = x+(y/8)* OLED_WIDTH;
    bit = y%8;

    if (on)
    {
        oled_buffer[index] |=(1U << bit);
    }
    else
    {
        oled_buffer[index] &=~(1U << bit);
    }
}

/* x、y 为左上角像素坐标，可跨 OLED 页；超出屏幕的部分由 SetPixel 裁剪。
 * 同时写入亮点和暗点，使新字符覆盖原来的 6×8 区域，不在这里刷新屏幕。
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
void OLED_ShowChar(uint8_t x, uint8_t y, char ch)
{
    uint8_t code = (uint8_t)ch;
    uint8_t column;
    uint8_t row;

    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        return;
    }
    /* 非可打印 ASCII 显示问号，避免负 char 或非法索引访问字库。 */
    if (code < 0x20U || code > 0x7EU) {
        code = (uint8_t)'?';
    }
    for (column = 0; column < OLED_FONT_WIDTH; column++) {
        uint8_t bits = oled_font6x8[code - 0x20U][column];
        for (row = 0; row < OLED_FONT_HEIGHT; row++) {
            OLED_SetPixel((uint8_t)(x + column), (uint8_t)(y + row),
                          (uint8_t)((bits >> row) & 1U));
        }
    }
}

/* 固定每字符前进 6 像素；遇到结束符或右边界停止，不自动换行。 */
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
void OLED_ShowString(uint8_t x, uint8_t y, const char *str)
{
    /* 使用 16 位游标，右移超出 127 后停止，避免 8 位游标回绕到屏幕左侧。 */
    uint16_t cursor_x = x;

    if (str == NULL || y >= OLED_HEIGHT) {
        return;
    }
    while (*str != '\0' && cursor_x < OLED_WIDTH) {
        OLED_ShowChar((uint8_t)cursor_x, y, *str);
        cursor_x += OLED_FONT_WIDTH;
        str++;
    }
}

/**
 * @brief 设置完整显存范围，把当前 1024 字节 RAM 缓冲区按 8 页发送到屏幕。
 * 调用时机：一帧中所有 SetPixel/ShowChar/ShowString 完成后调用一次。
 * @return 全部命令和页数据发送成功返回 HAL_OK，否则返回 HAL_ERROR。
 * 使用示例：
 *   OLED_ShowString(0, 0, "Ready"); if (OLED_UpdateScreen() != HAL_OK) {  }
 * @note 易错点：同步阻塞刷新，不是原子帧切换；中途通信失败或其他任务修改缓冲时，屏幕可能部分更新或画面混杂。刷新不会清空缓冲区。
 */
HAL_StatusTypeDef OLED_UpdateScreen(void)
{
    uint8_t page;

    /* 设置列地址 0～127 */
    if (OLED_WriteCommand(0x21) != HAL_OK) return HAL_ERROR;
    if (OLED_WriteCommand(0x00) != HAL_OK) return HAL_ERROR;
    if (OLED_WriteCommand(0x7F) != HAL_OK) return HAL_ERROR;

    /* 设置页地址 0～7 */
    if (OLED_WriteCommand(0x22) != HAL_OK) return HAL_ERROR;
    if (OLED_WriteCommand(0x00) != HAL_OK) return HAL_ERROR;
    if (OLED_WriteCommand(0x07) != HAL_OK) return HAL_ERROR;

    for (page = 0; page < OLED_PAGES; page++)
    {
        if (OLED_WriteData(
                &oled_buffer[page * OLED_WIDTH],
                OLED_WIDTH) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}
