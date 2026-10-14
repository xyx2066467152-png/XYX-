#ifndef __RC522_H
#define __RC522_H

#include "stm32f1xx_hal.h"

/* ================= RC522 寄存器 ================= */

/* 控制 RC522 当前执行什么命令 */
#define RC522_COMMAND_REG        0x01U

/* 通信中断状态寄存器 */
#define RC522_COM_IRQ_REG        0x04U

/* 通信错误状态寄存器 */
#define RC522_ERROR_REG          0x06U

/* FIFO 数据寄存器 */
#define RC522_FIFO_DATA_REG      0x09U

/* FIFO 当前数据长度 */
#define RC522_FIFO_LEVEL_REG     0x0AU

/* 控制最后一个字节使用多少 bit */
#define RC522_CONTROL_REG        0x0CU

/* 控制发送/接收时 bit 的排列方式 */
#define RC522_BIT_FRAMING_REG    0x0DU

/* 防冲突相关寄存器 */
#define RC522_COLL_REG           0x0EU

/* 工作模式配置 */
#define RC522_MODE_REG           0x11U

/* 射频天线控制 */
#define RC522_TX_CONTROL_REG     0x14U

/* ASK 调制配置 */
#define RC522_TX_ASK_REG         0x15U

/* 定时器配置 */
#define RC522_TMODE_REG          0x2AU
#define RC522_TPRESCALER_REG     0x2BU
#define RC522_TRELOAD_REG_H      0x2CU
#define RC522_TRELOAD_REG_L      0x2DU

/* 芯片版本寄存器 */
#define RC522_VERSION_REG        0x37U


/* ================= RC522 命令 ================= */

/* 空闲状态 */
#define RC522_CMD_IDLE           0x00U

/* RC522 软件复位 */
#define RC522_CMD_SOFT_RESET     0x0FU

/* 同时发送并接收数据 */
#define RC522_CMD_TRANSCEIVE     0x0CU


/* ================= ISO14443A 卡命令 ================= */

/* 寻找当前没有进入 HALT 状态的卡 */
#define PICC_REQIDL              0x26U
/*
 * WUPA（Wake-Up Type A）
 * 0x26 REQA：主要用于寻找新的 Type A 卡
 * 0x52 WUPA：也可以检测已经进入其他状态的卡
 * 本项目用 WUPA 判断“上一张卡是否还停留在感应区”。
 */
#define PICC_WUPA      0x52U
/* 防冲突 CL1 命令 */
#define PICC_ANTICOLL_CL1        0x93U


/* ================= 公共接口 ================= */

HAL_StatusTypeDef RC522_WriteRegister(uint8_t reg, uint8_t value);

HAL_StatusTypeDef RC522_ReadRegister(uint8_t reg, uint8_t *value);

/**
 * @brief 初始化 MFRC522
 *
 * @用途
 *   配置芯片内部定时器、调制方式以及射频天线。
 *
 * @使用
 *   BSP_SPI2_Init() 完成之后调用一次。
 */
void RC522_Init(void);


/**
 * @brief 检测附近是否存在 ISO14443A 卡片
 *
 * @return 1：检测到卡
 *         0：没有检测到卡
 */
uint8_t RC522_Request(void);


/**
 * @brief 读取卡片 UID（当前先支持最常见的4字节 UID）
 *
 * @param uid
 *        至少4字节的数组，用于保存 UID。
 *
 * @return 1：成功
 *         0：失败
 */
uint8_t RC522_ReadUID(uint8_t *uid);


/**
 * @brief 判断RC522感应区域内是否仍有卡片
 *
 * @return
 *   1：检测到卡
 *   0：未检测到卡
 *
 * @note
 * 该函数主要给 RFID_STATE_WAIT_REMOVE 使用，
 * 不负责读取UID。
 */
uint8_t RC522_IsCardPresent(void);
#endif
