#include "face_auth.h"
#include "app_config.h"
#include <string.h>

/* 授权表归 STM32 所有。新增身份时在 app_config.h 中维护。 */
static const uint16_t authorized_users[] = APP_FACE_AUTHORIZED_USER_IDS;

static AuthResult_t FaceAuth_CheckPermission(uint16_t user_id)
{
    unsigned int i;
    for (i = 0U; i < sizeof(authorized_users) / sizeof(authorized_users[0]); ++i)
    {
        if (authorized_users[i] == user_id)
        {
            return AUTH_GRANTED;
        }
    }
    return AUTH_DENIED;
}

void FaceAuth_Init(FaceAuth_t *state)
{
    memset(state, 0, sizeof(*state));
}

void FaceAuth_OnTransportLoss(FaceAuth_t *state)
{
    /* 丢字节后不能依靠 UNKNOWN、STATUS 或超时重新允许开门。 */
    state->latched = 1U;
    /* 上层同时丢弃接收缓冲/parser；READY 可能已丢，允许新流重建序号。 */
    state->have_seq = 0U;
}

FaceAction_t FaceAuth_Process(FaceAuth_t *state,
                             const K230_Frame_t *frame,
                             AuthEvent_t *event)
{
    uint16_t delta;

    if (K230_ValidatePayload(frame) != K230_PAYLOAD_VALID)
    {
        return FACE_ACTION_BAD_PAYLOAD;
    }
    /* 回复 SEQ 回显主控请求，与异步事件序号分属两条序列。 */
    if (frame->cmd == K230_CMD_STATUS) { return FACE_ACTION_STATUS; }
    if (frame->cmd == K230_CMD_PONG) { return FACE_ACTION_PONG; }
    if (frame->cmd == K230_CMD_ERROR)
    {
        state->latched = 1U;
        return FACE_ACTION_ERROR;
    }
    if (frame->cmd == K230_CMD_MODULE_READY)
    {
        if (state->have_seq && state->last_async_cmd == K230_CMD_MODULE_READY &&
            state->last_seq == frame->seq)
        {
            return FACE_ACTION_IGNORED;
        }
        /* 模块重启可重置 SEQ，但不能让仍站着的人立即再开门。 */
        if (state->seen_ready || state->have_seq) { state->latched = 1U; }
        state->seen_ready = 1U;
        state->have_seq = 1U;
        state->last_seq = frame->seq;
        state->last_async_cmd = frame->cmd;
        return FACE_ACTION_READY;
    }
    if (frame->cmd != K230_CMD_AUTH_SUCCESS && frame->cmd != K230_CMD_NO_FACE &&
        frame->cmd != K230_CMD_UNKNOWN)
    {
        return FACE_ACTION_IGNORED;
    }
    if (state->have_seq)
    {
        /* 模 65536 前进窗口：拒绝重复/倒退；65535 -> 0 仍是新帧。 */
        delta = (uint16_t)(frame->seq - state->last_seq);
        if (delta == 0U || delta >= 0x8000U) { return FACE_ACTION_IGNORED; }
    }
    state->have_seq = 1U;
    state->last_seq = frame->seq;
    state->last_async_cmd = frame->cmd;

    if (frame->cmd == K230_CMD_NO_FACE)
    {
        state->latched = 0U;
        state->unknown_reported = 0U;
        return FACE_ACTION_REMOVED;
    }
    if (frame->cmd == K230_CMD_UNKNOWN)
    {
        if (state->unknown_reported) { return FACE_ACTION_IGNORED; }
        state->unknown_reported = 1U;
        /* 仅作诊断：陌生脸不会开门，也不会妨碍后来形成有效身份。 */
        return FACE_ACTION_UNKNOWN;
    }
    if (state->latched) { return FACE_ACTION_IGNORED; }

    /* 无论授权或拒绝，都处理一次后等待 NO_FACE；队列满也不重放。 */
    state->latched = 1U;
    memset(event, 0, sizeof(*event));
    event->source = AUTH_SOURCE_FACE;
    event->user_id = K230_ReadLE16(frame->data);
    event->similarity_q = K230_ReadLE16(frame->data + 2U);
    event->result = FaceAuth_CheckPermission(event->user_id);
    return FACE_ACTION_AUTH;
}
