#include "rc522.h"
#include "bsp_spi.h"

/*
 * ============================================================
 * RC522 SPI 地址格式说明
 * ============================================================
 *
 * RC522 通过 SPI 访问寄存器时，
 * 不能直接发送寄存器地址。
 *
 * SPI 地址字节格式大致为：
 *
 *   bit7      bit6 ... bit1      bit0
 *   R/W       Register Address    0
 *
 *
 * 写寄存器：
 *
 *   bit7 = 0
 *
 * 所以：
 *
 *   address = (reg << 1) & 0x7E
 *
 *
 * 读寄存器：
 *
 *   bit7 = 1
 *
 * 所以：
 *
 *   address = ((reg << 1) & 0x7E) | 0x80
 *
 *
 * 例如 VersionReg：
 *
 *   reg = 0x37
 *
 *   0x37 << 1 = 0x6E
 *
 * 读取时：
 *
 *   0x6E | 0x80 = 0xEE
 *
 * 因此读取 VersionReg 时，
 * STM32 实际首先向 RC522 发送的是 0xEE，
 * 而不是直接发送 0x37。
 */


/**
 * @brief 向 RC522 写一个寄存器
 *
 * @通信过程
 *
 *   CS = LOW
 *      ↓
 *   发送寄存器地址
 *      ↓
 *   发送寄存器数据
 *      ↓
 *   CS = HIGH
 *
 * @注意
 *   RC522 的 CS 是低电平有效。
 *
 *   因为我们的 SPI2 还与 W25Q128 共用，
 *   BSP 层必须保证 W25Q128 的 CS 保持高电平。
 */
HAL_StatusTypeDef RC522_WriteRegister(uint8_t reg, uint8_t value)
{
    HAL_StatusTypeDef status;

    uint8_t tx_data[2];

    /*
     * 生成 RC522 的“写寄存器地址”。
     *
     * reg << 1：
     *   RC522 的寄存器地址需要左移一位。
     *
     * & 0x7E：
     *   保留 bit1~bit6，
     *   同时保证：
     *
     *     bit7 = 0  -> 写操作
     *     bit0 = 0
     */
    tx_data[0] = (uint8_t)((reg << 1U) & 0x7EU);

    /*
     * 第二个字节是真正要写入寄存器的数据。
     */
    tx_data[1] = value;


    /*
     * 拉低 PC0：
     * 选中 RC522。
     */
    BSP_RC522_Select();


    /*
     * 连续发送：
     *
     *   [寄存器地址][数据]
     *
     * timeout = 100ms
     *
     * 正常 SPI 只需要很短时间，
     * 100ms 在这里已经非常宽裕。
     */
    status = HAL_SPI_Transmit(
        BSP_SPI2_GetHandle(),
        tx_data,
        2U,
        100U
    );


    /*
     * 不管通信成功还是失败，
     * 最后都必须把 CS 拉高。
     *
     * 否则 RC522 会一直处于被选中状态，
     * 后面 SPI 总线容易出问题。
     */
    BSP_RC522_Deselect();


    return status;
}


/**
 * @brief 从 RC522 读取一个寄存器
 *
 * @通信过程
 *
 *   CS = LOW
 *      ↓
 *   发送“读寄存器地址”
 *      ↓
 *   再发送一个 Dummy Byte
 *      ↓
 *   RC522 在 MISO 上返回寄存器数据
 *      ↓
 *   CS = HIGH
 *
 *
 * @为什么需要 Dummy Byte？
 *
 * SPI 是全双工通信：
 *
 *   STM32 每发送 1 bit，
 *   同时也会接收 1 bit。
 *
 * 发送第一个 Byte 时，
 * STM32 只是在告诉 RC522：
 *
 *   “我要读取哪个寄存器”
 *
 * 真正的数据要到第二个 Byte 才返回。
 *
 * 所以第二个 Byte 我们发送：
 *
 *   0x00
 *
 * 它本身没有实际意义，
 * 只是为了产生 SPI 时钟，让 RC522 把数据送回来。
 */
HAL_StatusTypeDef RC522_ReadRegister(uint8_t reg, uint8_t *value)
{
    HAL_StatusTypeDef status;

    uint8_t tx_data[2];
    uint8_t rx_data[2];


    /*
     * 参数保护。
     *
     * 如果调用者传进来的指针是 NULL，
     * 就不能往里面写读取结果。
     */
    if (value == NULL)
    {
        return HAL_ERROR;
    }


    /*
     * 生成“读寄存器地址”。
     *
     * ((reg << 1) & 0x7E)
     *   -> 得到寄存器地址部分
     *
     * | 0x80
     *   -> 将 bit7 置1
     *   -> 表示 Read
     */
    tx_data[0] =
        (uint8_t)(((reg << 1U) & 0x7EU) | 0x80U);


    /*
     * Dummy Byte。
     *
     * 第二次SPI传输的目的主要是提供时钟，
     * 让RC522把寄存器值通过MISO发送回来。
     */
    tx_data[1] = 0x00U;


    /*
     * 先把接收缓冲区清零。
     *
     * 不是必须，
     * 但调试阶段这样更容易观察。
     */
    rx_data[0] = 0x00U;
    rx_data[1] = 0x00U;


    /*
     * 选中RC522。
     */
    BSP_RC522_Select();


    /*
     * 一次完成2 Byte全双工传输：
     *
     * TX:
     *
     *   [Read Address] [Dummy]
     *
     * RX:
     *
     *   [无用数据]     [Register Value]
     *
     * 所以真正需要的是：
     *
     *   rx_data[1]
     */
    status = HAL_SPI_TransmitReceive(
        BSP_SPI2_GetHandle(),
        tx_data,
        rx_data,
        2U,
        100U
    );


    /*
     * 通信结束，释放RC522。
     */
    BSP_RC522_Deselect();


    /*
     * 只有HAL真正报告通信成功，
     * 才把读取值交给调用者。
     */
    if (status == HAL_OK)
    {
        *value = rx_data[1];
    }


    return status;
}
/**
 * @brief 将RC522某个寄存器中的指定bit置1
 *
 * @param reg
 *        寄存器地址
 *
 * @param mask
 *        要置1的bit掩码
 *
 * @原理
 *   原值：
 *       value
 *
 *   修改：
 *       value | mask
 *
 *   这样不会破坏其他bit。
 */
static void RC522_SetBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t value = 0U;

    if (RC522_ReadRegister(reg, &value) == HAL_OK)
    {
        RC522_WriteRegister(reg, value | mask);
    }
}
/**
 * @brief 将RC522某个寄存器中的指定bit清0
 *
 * @原理
 *
 *   value & (~mask)
 *
 *   只清除mask指定的bit，
 *   其他bit保持原状态。
 */
static void RC522_ClearBitMask(uint8_t reg, uint8_t mask)
{
    uint8_t value = 0U;

    if (RC522_ReadRegister(reg, &value) == HAL_OK)
    {
        RC522_WriteRegister(reg, value & (~mask));
    }
}
/**
 * @brief 初始化MFRC522
 *
 * @用途
 *   软件复位RC522，并配置内部定时器和13.56MHz射频发送部分。
 *
 * @调用位置
 *
 *   BSP_SPI2_Init();
 *   RC522_Init();
 *
 * @注意
 *   必须先初始化SPI2。
 */
void RC522_Init(void)
{
    /*
     * 1. 软件复位RC522。
     */
    RC522_WriteRegister(
        RC522_COMMAND_REG,
        RC522_CMD_SOFT_RESET
    );

    /*
     * 软件复位后给芯片一点恢复时间。
     *
     * RC522_Init()属于上电初始化阶段，
     * 当前使用HAL_Delay即可。
     *
     * 当前HAL Tick已经由TIM2提供。
     */
    HAL_Delay(10U);


    /*
     * 2. 配置RC522内部Timer。
     *
     * 这些参数主要用于后面的无线通信超时检测。
     */
    RC522_WriteRegister(RC522_TMODE_REG,      0x8DU);
    RC522_WriteRegister(RC522_TPRESCALER_REG, 0x3EU);

    RC522_WriteRegister(RC522_TRELOAD_REG_L, 30U);
    RC522_WriteRegister(RC522_TRELOAD_REG_H, 0U);


    /*
     * 3. 设置ASK调制。
     */
    RC522_WriteRegister(RC522_TX_ASK_REG, 0x40U);


    /*
     * 4. ISO14443A常用工作模式配置。
     */
    RC522_WriteRegister(RC522_MODE_REG, 0x3DU);


    /*
     * 5. 开启射频天线驱动。
     *
     * TxControlReg bit0/bit1控制天线输出。
     */
    RC522_SetBitMask(
        RC522_TX_CONTROL_REG,
        0x03U
    );
}
/**
 * @brief 通过RC522向RFID卡发送数据并接收返回数据
 *
 * @param command
 *        RC522内部命令。
 *        当前主要使用 RC522_CMD_TRANSCEIVE。
 *
 * @param tx_data
 *        要发送给卡片的数据。
 *
 * @param tx_len
 *        发送数据长度，单位Byte。
 *
 * @param rx_data
 *        接收缓冲区。
 *
 * @param rx_len
 *        返回实际接收到的数据长度。
 *
 * @return 1：通信成功
 *         0：失败或超时
 *
 * @注意
 *   这是Driver内部函数，
 *   App层不应该直接调用。
 */
static uint8_t RC522_Transceive(
    uint8_t command,
    uint8_t *tx_data,
    uint8_t tx_len,
    uint8_t *rx_data,
    uint8_t *rx_len)
{
    uint8_t irq;
    uint8_t error;
    uint8_t fifo_level;
    uint16_t timeout;
    uint8_t i;


    /*
     * 1. 先让RC522停止当前命令。
     */
    RC522_WriteRegister(
        RC522_COMMAND_REG,
        RC522_CMD_IDLE
    );


    /*
     * 2. 清空FIFO。
     *
     * FIFOLevelReg bit7 = 1 时，
     * RC522会刷新FIFO缓冲区。
     */
    RC522_SetBitMask(
        RC522_FIFO_LEVEL_REG,
        0x80U
    );


    /*
     * 3. 清除之前的中断标志。
     */
    RC522_WriteRegister(
        RC522_COM_IRQ_REG,
        0x7FU
    );


    /*
     * 4. 将要发送的数据写进RC522 FIFO。
     */
    for (i = 0U; i < tx_len; i++)
    {
        RC522_WriteRegister(
            RC522_FIFO_DATA_REG,
            tx_data[i]
        );
    }


    /*
     * 5. 告诉RC522开始执行命令。
     */
    RC522_WriteRegister(
        RC522_COMMAND_REG,
        command
    );


    /*
     * Transceive命令需要设置StartSend位。
     *
     * BitFramingReg bit7 = StartSend。
     */
    if (command == RC522_CMD_TRANSCEIVE)
    {
        RC522_SetBitMask(
            RC522_BIT_FRAMING_REG,
            0x80U
        );
    }


    /*
     * 6. 等待通信结束。
     *
     * 这里不能无限等待，
     * 否则卡片不存在时程序可能卡死。
     */
    timeout = 2000U;

    do
    {
        RC522_ReadRegister(
            RC522_COM_IRQ_REG,
            &irq
        );

        timeout--;

    } while ((timeout != 0U) &&
             ((irq & 0x30U) == 0U));


    /*
     * 通信结束后清除StartSend。
     */
    RC522_ClearBitMask(
        RC522_BIT_FRAMING_REG,
        0x80U
    );


    /*
     * 超时：
     * 一般代表当前没有卡响应。
     */
    if (timeout == 0U)
    {
        return 0U;
    }


    /*
     * 7. 检查RC522通信错误状态。
     */
    RC522_ReadRegister(
        RC522_ERROR_REG,
        &error
    );

    /*
     * 0x1B涵盖：
     * Buffer overflow
     * Collision
     * CRC error
     * Protocol error 等。
     */
    if ((error & 0x1BU) != 0U)
    {
        return 0U;
    }


    /*
     * 8. 查看FIFO里收到多少字节。
     */
    RC522_ReadRegister(
        RC522_FIFO_LEVEL_REG,
        &fifo_level
    );

    /*
     * 防止调用者提供的缓冲区被写爆。
     *
     * 当前我们的临时接收缓冲区最多16字节。
     */
    if (fifo_level > 16U)
    {
        fifo_level = 16U;
    }


    /*
     * 9. 从FIFO读取卡片返回的数据。
     */
    for (i = 0U; i < fifo_level; i++)
    {
        RC522_ReadRegister(
            RC522_FIFO_DATA_REG,
            &rx_data[i]
        );
    }

    *rx_len = fifo_level;

    return 1U;
}
/**
 * @brief 检测RC522附近是否存在卡片
 *
 * @return
 *   1：检测到卡
 *   0：没有卡或通信失败
 *
 * @说明
 *   PICC_REQIDL = 0x26
 *
 *   这是ISO14443A的REQA命令。
 */
uint8_t RC522_Request(void)
{
    uint8_t tx_data[1];
    uint8_t rx_data[16];
    uint8_t rx_len = 0U;

    /*
     * REQA命令只发送7 bit，
     * 所以 BitFramingReg 的低3位设置为7。
     */
    RC522_WriteRegister(
        RC522_BIT_FRAMING_REG,
        0x07U
    );

    tx_data[0] = PICC_REQIDL;


    /*
     * 发出寻卡命令。
     */
    if (RC522_Transceive(
            RC522_CMD_TRANSCEIVE,
            tx_data,
            1U,
            rx_data,
            &rx_len) == 0U)
    {
        return 0U;
    }


    /*
     * 正常情况下ATQA返回2 Byte。
     */
    if (rx_len != 2U)
    {
        return 0U;
    }

    return 1U;
}
/**
 * @brief 执行ISO14443A一级防冲突并读取4字节UID
 *
 * @param uid
 *        至少4Byte的数组。
 *
 * @return
 *        1：UID读取成功
 *        0：失败
 *
 * @说明
 *   这里先满足项目需求，
 *   支持常见的4Byte UID卡。
 *
 *   后续如果遇到7Byte UID卡，
 *   再扩展CL2即可。
 */
uint8_t RC522_ReadUID(uint8_t *uid)
{
    uint8_t tx_data[2];
    uint8_t rx_data[16];
    uint8_t rx_len = 0U;
    uint8_t bcc;


    if (uid == NULL)
    {
        return 0U;
    }


    /*
     * 防冲突阶段按完整Byte通信，
     * 所以 TxLastBits = 0。
     */
    RC522_WriteRegister(
        RC522_BIT_FRAMING_REG,
        0x00U
    );


    /*
     * 0x93 = Select CL1
     * 0x20 = Anticollision
     */
    tx_data[0] = PICC_ANTICOLL_CL1;
    tx_data[1] = 0x20U;


    if (RC522_Transceive(
            RC522_CMD_TRANSCEIVE,
            tx_data,
            2U,
            rx_data,
            &rx_len) == 0U)
    {
        return 0U;
    }


    /*
     * 防冲突成功后通常返回：
     *
     * UID0
     * UID1
     * UID2
     * UID3
     * BCC
     *
     * 一共5Byte。
     */
    if (rx_len < 5U)
    {
        return 0U;
    }


    /*
     * BCC是前4个UID字节的异或校验。
     */
    bcc =
        rx_data[0] ^
        rx_data[1] ^
        rx_data[2] ^
        rx_data[3];


    /*
     * 校验UID数据是否完整。
     */
    if (bcc != rx_data[4])
    {
        return 0U;
    }


    uid[0] = rx_data[0];
    uid[1] = rx_data[1];
    uid[2] = rx_data[2];
    uid[3] = rx_data[3];

    return 1U;
}
/**
 * @brief 判断RC522感应区域内是否仍存在ISO14443A卡片
 *
 * @说明
 * 使用 WUPA(0x52) 而不是普通 REQA(0x26)。
 *
 * RfidTask 已经处理完一张卡以后，只需要知道：
 *
 *      “卡还在不在？”
 *
 * 不需要再次读取UID，也不需要再次产生认证事件。
 */
uint8_t RC522_IsCardPresent(void)
{
    uint8_t command;
    uint8_t response[2];
    uint8_t response_len = 0U;

    command = PICC_WUPA;

    /*
     * REQA / WUPA 都是7位命令，
     * 因此 BitFramingReg 配置为发送7位。
     */
    if (RC522_WriteRegister(RC522_BIT_FRAMING_REG, 0x07U) != HAL_OK)
    {
        return 0U;
    }

    /*
     * 发送WUPA。
     *
     * 正常Type A卡应该返回2字节ATQA。
     */
    if (RC522_Transceive(
            RC522_CMD_TRANSCEIVE,
            &command,
            1U,
            response,
            &response_len) == 0U)
    {
        return 0U;
    }

    /*
     * 收到2字节ATQA，认为卡仍然存在。
     */
    return (response_len == 2U) ? 1U : 0U;
}
