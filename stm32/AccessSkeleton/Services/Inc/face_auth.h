#ifndef FACE_AUTH_H
#define FACE_AUTH_H

#include "app_events.h"
#include "k230_protocol.h"

/* 仅保存本次串口会话状态，不保存 RFID 状态或控制执行器。 */
typedef struct
{
    uint16_t last_seq;
    uint8_t have_seq;
    uint8_t seen_ready;
    uint8_t latched;
    uint8_t unknown_reported;
    uint8_t last_async_cmd;
} FaceAuth_t;

typedef enum
{
    FACE_ACTION_IGNORED = 0,
    FACE_ACTION_AUTH,
    FACE_ACTION_REMOVED,
    FACE_ACTION_UNKNOWN,
    FACE_ACTION_READY,
    FACE_ACTION_ERROR,
    FACE_ACTION_STATUS,
    FACE_ACTION_PONG,
    FACE_ACTION_BAD_PAYLOAD
} FaceAction_t;

/** 初始化 STM32 人脸权限/去重状态；不修改 K230D 数据库。 */
void FaceAuth_Init(FaceAuth_t *state);
/** 接收溢出后锁定认证，须重新收到有效 NO_FACE 才允许下一次。 */
void FaceAuth_OnTransportLoss(FaceAuth_t *state);
/**
 * 处理已经通过 CRC 的完整帧。仅 FACE_ACTION_AUTH 时填写 event。
 * STATUS 始终只作诊断；UNKNOWN 不解除 latched。
 */
FaceAction_t FaceAuth_Process(FaceAuth_t *state,
                             const K230_Frame_t *frame,
                             AuthEvent_t *event);

#endif
