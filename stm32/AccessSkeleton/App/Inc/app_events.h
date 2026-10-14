#ifndef __APP_EVENTS_H
#define __APP_EVENTS_H

#include <stdint.h>

/* =========================================================
 * 按键事件
 * ========================================================= */

/**
 * @brief 按键任务发送给其他任务的事件
 */
typedef enum
{
    KEY_EVENT_PRESSED = 1
} KeyEvent_t;


/* =========================================================
 * 身份认证事件
 * ========================================================= */

/**
 * @brief 身份认证来源
 *
 * 当前支持 RFID；
 * 后续 K230D 人脸识别也使用同一套认证事件。
 */
typedef enum
{
    AUTH_SOURCE_RFID = 1,
    AUTH_SOURCE_FACE
} AuthSource_t;


/**
 * @brief 身份认证结果
 */
typedef enum
{
    AUTH_DENIED = 0,     /* 认证失败，不允许开门 */
    AUTH_GRANTED         /* 认证成功，允许开门 */
} AuthResult_t;


/**
 * @brief 统一身份认证事件
 *
 * RfidTask / 后续 FaceTask：
 *      产生该事件
 *
 * AccessTask：
 *      接收该事件并决定是否开门
 */
typedef struct
{
    AuthSource_t source;     /* 认证来源 */
    AuthResult_t result;     /* 认证结果 */

    /*
     * 当前保存 RFID 的4字节 UID。
     * 人脸认证暂时可以不使用该成员。
     */
    uint8_t uid[4];

    /* 人脸身份独立存放；RFID 事件这两个字段置零，uid[4] 保持原用途。 */
    uint16_t user_id;
    uint16_t similarity_q;    /* 相似度乘 10000，避免 MCU 浮点格式化 */

} AuthEvent_t;

#endif
