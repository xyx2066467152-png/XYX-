#include "k230_protocol.h"
#include <string.h>

#define K230_LENGTH_OFFSET       6U
#define K230_DATA_OFFSET         7U
#define K230_SEQUENCE_OFFSET     4U
#define K230_CRC_POLYNOMIAL      0x1021U
#define K230_CRC_INITIAL         0xFFFFU

/* 长时间运行时统计饱和，避免计数回绕后误以为故障减少。 */
static void StatsAdd(uint32_t *value, uint32_t amount)
{
    if (*value > UINT32_MAX - amount)
    {
        *value = UINT32_MAX;
    }
    else
    {
        *value += amount;
    }
}

uint16_t K230_ReadLE16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

uint16_t K230_CRC16(const uint8_t *data, size_t length)
{
    uint16_t crc = K230_CRC_INITIAL;
    size_t index;
    uint8_t bit;

    if (data == NULL && length != 0U)
    {
        return 0U;
    }

    for (index = 0U; index < length; ++index)
    {
        crc ^= (uint16_t)((uint16_t)data[index] << 8U);
        for (bit = 0U; bit < 8U; ++bit)
        {
            if ((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1U) ^ K230_CRC_POLYNOMIAL);
            }
            else
            {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }
    return crc;
}

uint8_t K230_IsKnownCommand(uint8_t cmd)
{
    switch (cmd)
    {
        case K230_CMD_MODULE_READY:
        case K230_CMD_AUTH_SUCCESS:
        case K230_CMD_UNKNOWN:
        case K230_CMD_NO_FACE:
        case K230_CMD_ERROR:
        case K230_CMD_PING:
        case K230_CMD_GET_STATUS:
        case K230_CMD_PONG:
        case K230_CMD_STATUS:
            return 1U;
        default:
            return 0U;
    }
}

K230_PayloadResult_t K230_ValidatePayload(const K230_Frame_t *frame)
{
    uint8_t expected_length;

    if (frame == NULL)
    {
        return K230_PAYLOAD_BAD_FIELD;
    }
    if (frame->version != K230_PROTOCOL_VERSION)
    {
        return K230_PAYLOAD_UNSUPPORTED_VERSION;
    }
    if (frame->len > K230_MAX_DATA_LENGTH)
    {
        return K230_PAYLOAD_BAD_LENGTH;
    }
    switch (frame->cmd)
    {
        case K230_CMD_MODULE_READY: expected_length = 6U; break;
        case K230_CMD_AUTH_SUCCESS: expected_length = 4U; break;
        case K230_CMD_UNKNOWN:      expected_length = 1U; break;
        case K230_CMD_ERROR:        expected_length = 4U; break;
        case K230_CMD_STATUS:       expected_length = 8U; break;
        case K230_CMD_NO_FACE:
        case K230_CMD_PING:
        case K230_CMD_GET_STATUS:
        case K230_CMD_PONG:         expected_length = 0U; break;
        default: return K230_PAYLOAD_UNKNOWN_CMD;
    }
    if (frame->len != expected_length)
    {
        return K230_PAYLOAD_BAD_LENGTH;
    }

    switch (frame->cmd)
    {
        case K230_CMD_MODULE_READY:
            if (K230_ReadLE16(&frame->data[2]) == 0U ||
                K230_ReadLE16(&frame->data[4]) > K230_SIMILARITY_SCALE)
            {
                return K230_PAYLOAD_BAD_FIELD;
            }
            break;
        case K230_CMD_AUTH_SUCCESS:
            if (K230_ReadLE16(&frame->data[0]) == 0U ||
                K230_ReadLE16(&frame->data[2]) > K230_SIMILARITY_SCALE)
            {
                return K230_PAYLOAD_BAD_FIELD;
            }
            break;
        case K230_CMD_UNKNOWN:
            if (frame->data[0] < K230_REASON_UNMATCHED ||
                frame->data[0] > K230_REASON_UNMAPPED_IDENTITY)
            {
                return K230_PAYLOAD_BAD_FIELD;
            }
            break;
        case K230_CMD_ERROR:
            if (K230_ReadLE16(&frame->data[0]) == 0U)
            {
                return K230_PAYLOAD_BAD_FIELD;
            }
            break;
        case K230_CMD_STATUS:
            /* Python V1 保留 state/last_event 原始 u8，只校验 Q 的范围。 */
            if (K230_ReadLE16(&frame->data[4]) > K230_SIMILARITY_SCALE)
            {
                return K230_PAYLOAD_BAD_FIELD;
            }
            break;
        default:
            break;
    }
    return K230_PAYLOAD_VALID;
}

size_t K230_EncodeFrame(uint8_t cmd,
                        uint16_t seq,
                        const uint8_t *data,
                        uint8_t length,
                        uint8_t *output,
                        size_t capacity)
{
    size_t frame_length = K230_FRAME_OVERHEAD + (size_t)length;
    uint16_t crc;

    if (output == NULL || length > K230_MAX_DATA_LENGTH ||
        capacity < frame_length || (data == NULL && length != 0U))
    {
        return 0U;
    }
    output[0] = K230_HEADER_FIRST;
    output[1] = K230_HEADER_SECOND;
    output[2] = K230_PROTOCOL_VERSION;
    output[3] = cmd;
    output[K230_SEQUENCE_OFFSET] = (uint8_t)(seq & 0xFFU);
    output[K230_SEQUENCE_OFFSET + 1U] = (uint8_t)(seq >> 8U);
    output[K230_LENGTH_OFFSET] = length;
    if (length != 0U)
    {
        memcpy(&output[K230_DATA_OFFSET], data, length);
    }
    crc = K230_CRC16(&output[2], 5U + (size_t)length);
    output[frame_length - 2U] = (uint8_t)(crc & 0xFFU);
    output[frame_length - 1U] = (uint8_t)(crc >> 8U);
    return frame_length;
}

void K230_ParserInit(K230_Parser_t *parser,
                     K230_FrameCallback_t callback,
                     void *context)
{
    if (parser != NULL)
    {
        memset(parser, 0, sizeof(*parser));
        parser->callback = callback;
        parser->context = context;
    }
}

void K230_ParserReset(K230_Parser_t *parser)
{
    if (parser != NULL)
    {
        parser->buffered_bytes = 0U;
        parser->partial_active = 0U;
        parser->partial_started_ms = 0U;
        memset(parser->buffer, 0, sizeof(parser->buffer));
    }
}

static void DropPrefix(K230_Parser_t *parser, uint8_t count)
{
    uint8_t remaining = (uint8_t)(parser->buffered_bytes - count);
    if (remaining != 0U)
    {
        memmove(parser->buffer, &parser->buffer[count], remaining);
    }
    parser->buffered_bytes = remaining;
    parser->partial_active = 0U;
}

/* 每次重扫最多 41 字节。DATA 内 AA55 不截断；先等 LEN 与 CRC 确定边界。 */
static size_t Scan(K230_Parser_t *parser, uint32_t now_ms)
{
    size_t frames = 0U;
    uint8_t index;
    uint8_t header;
    uint8_t keep;
    uint8_t discard;
    uint8_t payload_length;
    uint8_t frame_length;
    uint16_t received_crc;
    uint16_t calculated_crc;
    K230_Frame_t frame;

    while (parser->buffered_bytes != 0U)
    {
        header = parser->buffered_bytes;
        for (index = 0U; (uint16_t)index + 1U < parser->buffered_bytes; ++index)
        {
            if (parser->buffer[index] == K230_HEADER_FIRST &&
                parser->buffer[index + 1U] == K230_HEADER_SECOND)
            {
                header = index;
                break;
            }
        }
        if (header == parser->buffered_bytes)
        {
            keep = parser->buffer[parser->buffered_bytes - 1U] == K230_HEADER_FIRST ? 1U : 0U;
            discard = (uint8_t)(parser->buffered_bytes - keep);
            if (discard != 0U)
            {
                StatsAdd(&parser->stats.noise_bytes, discard);
                DropPrefix(parser, discard);
            }
            break;
        }
        if (header != 0U)
        {
            StatsAdd(&parser->stats.noise_bytes, header);
            DropPrefix(parser, header);
        }
        if (parser->buffered_bytes < 3U)
        {
            break;
        }
        if (parser->buffer[2] != K230_PROTOCOL_VERSION)
        {
            StatsAdd(&parser->stats.unsupported_versions, 1U);
            DropPrefix(parser, 1U);
            continue;
        }
        if (parser->buffered_bytes < K230_DATA_OFFSET)
        {
            break;
        }
        payload_length = parser->buffer[K230_LENGTH_OFFSET];
        if (payload_length > K230_MAX_DATA_LENGTH)
        {
            StatsAdd(&parser->stats.invalid_lengths, 1U);
            DropPrefix(parser, 1U);
            continue;
        }
        frame_length = (uint8_t)(K230_FRAME_OVERHEAD + payload_length);
        if (parser->buffered_bytes < frame_length)
        {
            break;
        }
        received_crc = K230_ReadLE16(&parser->buffer[frame_length - 2U]);
        calculated_crc = K230_CRC16(&parser->buffer[2], (size_t)frame_length - 4U);
        if (received_crc != calculated_crc)
        {
            StatsAdd(&parser->stats.crc_errors, 1U);
            DropPrefix(parser, 1U);
            continue;
        }

        /* 在移走原始字节前复制；回调不会引用 parser 内部缓冲。 */
        memset(&frame, 0, sizeof(frame));
        frame.version = parser->buffer[2];
        frame.cmd = parser->buffer[3];
        frame.seq = K230_ReadLE16(&parser->buffer[K230_SEQUENCE_OFFSET]);
        frame.len = payload_length;
        if (payload_length != 0U)
        {
            memcpy(frame.data, &parser->buffer[K230_DATA_OFFSET], payload_length);
        }
        StatsAdd(&parser->stats.frames, 1U);
        if (K230_IsKnownCommand(frame.cmd) == 0U)
        {
            StatsAdd(&parser->stats.unknown_commands, 1U);
        }
        DropPrefix(parser, frame_length);
        ++frames;
        if (parser->callback != NULL)
        {
            parser->callback(&frame, parser->context);
        }
    }

    if (parser->buffered_bytes != 0U && parser->partial_active == 0U)
    {
        parser->partial_started_ms = now_ms;
        parser->partial_active = 1U;
    }
    else if (parser->buffered_bytes == 0U)
    {
        parser->partial_active = 0U;
    }
    return frames;
}

size_t K230_ParserExpire(K230_Parser_t *parser, uint32_t now_ms)
{
    if (parser == NULL || parser->partial_active == 0U)
    {
        return 0U;
    }
    /* uint32 无符号减法：例如 FFFFFFF0 -> 000000B8 正好 200ms。 */
    if ((uint32_t)(now_ms - parser->partial_started_ms) < K230_PARTIAL_TIMEOUT_MS)
    {
        return 0U;
    }
    StatsAdd(&parser->stats.partial_timeouts, 1U);
    DropPrefix(parser, 1U);
    return Scan(parser, now_ms);
}

size_t K230_ParserFeed(K230_Parser_t *parser,
                       const uint8_t *data,
                       size_t length,
                       uint32_t now_ms)
{
    size_t frames;
    size_t index;

    if (parser == NULL || (data == NULL && length != 0U))
    {
        return 0U;
    }
    frames = K230_ParserExpire(parser, now_ms);
    for (index = 0U; index < length; ++index)
    {
        /* 正常扫描后最多保留 40 字节；防御性分支避免内部状态损坏后越界。 */
        if (parser->buffered_bytes >= K230_MAX_FRAME_LENGTH)
        {
            K230_ParserReset(parser);
        }
        parser->buffer[parser->buffered_bytes] = data[index];
        ++parser->buffered_bytes;
        if (parser->stats.max_buffer_seen < parser->buffered_bytes)
        {
            parser->stats.max_buffer_seen = parser->buffered_bytes;
        }
        frames += Scan(parser, now_ms);
    }
    return frames;
}

size_t K230_ParserFeedByte(K230_Parser_t *parser, uint8_t byte, uint32_t now_ms)
{
    return K230_ParserFeed(parser, &byte, 1U, now_ms);
}
