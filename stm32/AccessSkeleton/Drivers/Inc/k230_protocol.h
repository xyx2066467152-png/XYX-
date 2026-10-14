#ifndef K230_PROTOCOL_H
#define K230_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 与 K230D 的 uart_protocol.py / PROTOCOL.md V1 保持一致。 */
#define K230_PROTOCOL_VERSION           0x01U
#define K230_HEADER_FIRST               0xAAU
#define K230_HEADER_SECOND              0x55U
#define K230_MAX_DATA_LENGTH            32U
#define K230_FRAME_OVERHEAD             9U
#define K230_MAX_FRAME_LENGTH           (K230_FRAME_OVERHEAD + K230_MAX_DATA_LENGTH)
#define K230_PARTIAL_TIMEOUT_MS         200U
#define K230_SIMILARITY_SCALE           10000U

#define K230_CMD_MODULE_READY           0x01U
#define K230_CMD_AUTH_SUCCESS           0x10U
#define K230_CMD_UNKNOWN                0x11U
#define K230_CMD_AUTH_FAIL              K230_CMD_UNKNOWN
#define K230_CMD_NO_FACE                0x12U
#define K230_CMD_ERROR                  0x7FU
#define K230_CMD_PING                   0x80U
#define K230_CMD_GET_STATUS             0x81U
#define K230_CMD_PONG                   0x82U
#define K230_CMD_STATUS                 0x83U

#define K230_REASON_UNMATCHED           1U
#define K230_REASON_MULTIPLE_FACES      2U
#define K230_REASON_UNMAPPED_IDENTITY   3U

#define K230_ERROR_AI                   0x0001U
#define K230_ERROR_UART                 0x0002U
#define K230_ERROR_DATABASE             0x0003U
#define K230_ERROR_CONFIG               0x0004U
#define K230_ERROR_CAMERA               0x0005U
#define K230_ERROR_MODEL                0x0006U
#define K230_ERROR_CLEANUP              0x0007U
#define K230_ERROR_PROTOCOL             0x0010U
#define K230_ERROR_UNKNOWN_CMD          0x0011U
#define K230_ERROR_BAD_PAYLOAD          0x0012U
#define K230_ERROR_INTERNAL             0x00FFU

#define K230_STATE_STARTING             0U
#define K230_STATE_READY                1U
#define K230_STATE_RUNNING              2U
#define K230_STATE_ERROR                3U

#define K230_EVENT_NONE                 0U
#define K230_EVENT_NO_FACE              1U
#define K230_EVENT_UNKNOWN              2U
#define K230_EVENT_AUTH_SUCCESS         3U
#define K230_EVENT_ERROR                4U

typedef struct
{
    uint8_t version;
    uint8_t cmd;
    uint16_t seq;
    uint8_t len;
    uint8_t data[K230_MAX_DATA_LENGTH];
} K230_Frame_t;

/*
 * CRC 正确的完整帧均回调，包括未知 CMD 和非法应用字段。
 * frame 仅在回调执行期间有效；需要保留时复制整个结构体。
 * 回调内不得递归调用同一个 parser；串口 ISR 与任务也不得并发操作它。
 */
typedef void (*K230_FrameCallback_t)(const K230_Frame_t *frame, void *context);

typedef struct
{
    uint32_t frames;
    uint32_t crc_errors;
    uint32_t invalid_lengths;
    uint32_t unsupported_versions;
    uint32_t noise_bytes;
    uint32_t partial_timeouts;
    uint32_t unknown_commands;
    uint32_t max_buffer_seen;
} K230_ParserStats_t;

typedef struct
{
    uint8_t buffer[K230_MAX_FRAME_LENGTH];
    uint8_t buffered_bytes;
    uint8_t partial_active;
    uint32_t partial_started_ms;
    K230_ParserStats_t stats;
    K230_FrameCallback_t callback;
    void *context;
} K230_Parser_t;

typedef enum
{
    K230_PAYLOAD_VALID = 0,
    K230_PAYLOAD_UNKNOWN_CMD,
    K230_PAYLOAD_BAD_LENGTH,
    K230_PAYLOAD_BAD_FIELD,
    K230_PAYLOAD_UNSUPPORTED_VERSION
} K230_PayloadResult_t;

/** 初始化纯协议解析器；不初始化 UART、HAL 或 RTOS。callback 可以为 NULL。 */
void K230_ParserInit(K230_Parser_t *parser,
                     K230_FrameCallback_t callback,
                     void *context);

/** 清空未完成候选帧，保留 callback、context 和累计统计。 */
void K230_ParserReset(K230_Parser_t *parser);

/*
 * 逐字节、分包和粘包均使用同一入口。返回此次解析出的完整帧数量。
 * now_ms 为单调 uint32 毫秒计数；无符号减法支持一次自然回绕。
 * 200ms 从当前候选首字节计时，后续零星字节不延长超时。
 * 应在接收处理与周期任务中调用 Expire，且两次调用不要相隔 2^32ms。
 * feed/expire 可能回调多帧，绝不能只保留最后一帧。
 */
size_t K230_ParserFeed(K230_Parser_t *parser,
                       const uint8_t *data,
                       size_t length,
                       uint32_t now_ms);
size_t K230_ParserFeedByte(K230_Parser_t *parser,
                           uint8_t byte,
                           uint32_t now_ms);
size_t K230_ParserExpire(K230_Parser_t *parser, uint32_t now_ms);

/** 精确复用 Python 的 payload 检查；UNKNOWN CMD 保留原值交给上层处理。 */
K230_PayloadResult_t K230_ValidatePayload(const K230_Frame_t *frame);
uint8_t K230_IsKnownCommand(uint8_t cmd);

/** 不使用未对齐指针强转，读取小端 u16；data 必须指向至少两个有效字节。 */
uint16_t K230_ReadLE16(const uint8_t *data);

/** CRC-16/CCITT-FALSE；ASCII "123456789" 得 0x29B1。 */
uint16_t K230_CRC16(const uint8_t *data, size_t length);

/*
 * 纯编码 helper；不会发送串口。成功返回帧长，非法长度/指针/容量返回 0。
 * data 可在 length=0 时为 NULL；输出与 DATA 不应重叠。
 * SEQ 用于关联，V1 没有 ACK 或端到端送达保证。
 */
size_t K230_EncodeFrame(uint8_t cmd,
                        uint16_t seq,
                        const uint8_t *data,
                        uint8_t length,
                        uint8_t *output,
                        size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
