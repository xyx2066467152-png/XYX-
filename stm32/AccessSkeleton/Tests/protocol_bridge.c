/* Software-only bridge: execute the production parser and face policy on ARM. */
#include "k230_protocol.h"
#include "face_auth.h"
#include <string.h>

typedef struct {
    uint32_t before[4];
    K230_Parser_t parser;
    uint32_t after[4];
} GuardedParser;
static GuardedParser guarded;
uint8_t test_frames[256][37];
uint32_t test_frame_count;
uint8_t test_encoded[64];
uint8_t test_event[16];
static FaceAuth_t policy;

static void capture(const K230_Frame_t *frame, void *context) {
    uint8_t *target;
    (void)context;
    if (test_frame_count >= 256U) { test_frame_count++; return; }
    target = test_frames[test_frame_count++];
    target[0] = frame->version;
    target[1] = frame->cmd;
    target[2] = (uint8_t)frame->seq;
    target[3] = (uint8_t)(frame->seq >> 8U);
    target[4] = frame->len;
    memcpy(target + 5U, frame->data, 32U);
}
void Test_Init(void) {
    unsigned int index;
    for (index = 0U; index < 4U; index++) {
        guarded.before[index] = guarded.after[index] = 0xA5A5A5A5U;
    }
    test_frame_count = 0U;
    memset(test_frames, 0, sizeof(test_frames));
    K230_ParserInit(&guarded.parser, capture, 0);
}
uint32_t Test_Feed(const uint8_t *data, uint32_t length, uint32_t now_ms) {
    return (uint32_t)K230_ParserFeed(&guarded.parser, data, length, now_ms);
}
uint32_t Test_FeedByte(uint32_t value, uint32_t now_ms) {
    return (uint32_t)K230_ParserFeedByte(&guarded.parser, (uint8_t)value, now_ms);
}
uint32_t Test_Expire(uint32_t now_ms) {
    return (uint32_t)K230_ParserExpire(&guarded.parser, now_ms);
}
uint32_t Test_Stat(uint32_t index) {
    switch (index) {
        case 0: return guarded.parser.stats.frames;
        case 1: return guarded.parser.stats.crc_errors;
        case 2: return guarded.parser.stats.invalid_lengths;
        case 3: return guarded.parser.stats.unsupported_versions;
        case 4: return guarded.parser.stats.noise_bytes;
        case 5: return guarded.parser.stats.partial_timeouts;
        case 6: return guarded.parser.stats.unknown_commands;
        case 7: return guarded.parser.stats.max_buffer_seen;
        case 8: return guarded.parser.buffered_bytes;
        default: return 0U;
    }
}
uint32_t Test_Guards(void) {
    unsigned int index;
    for (index = 0U; index < 4U; index++) {
        if (guarded.before[index] != 0xA5A5A5A5U ||
            guarded.after[index] != 0xA5A5A5A5U) { return 0U; }
    }
    return 1U;
}
void Test_Reset(void) { K230_ParserReset(&guarded.parser); }
uint32_t Test_Encode(uint32_t cmd, uint32_t seq, const uint8_t *data,
                     uint32_t length, uint32_t capacity) {
    memset(test_encoded, 0xA5, sizeof(test_encoded));
    return (uint32_t)K230_EncodeFrame((uint8_t)cmd, (uint16_t)seq, data,
                                    (uint8_t)length, test_encoded, capacity);
}
static K230_Frame_t make_frame(const uint8_t *packed) {
    K230_Frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.version = packed[0]; frame.cmd = packed[1];
    frame.seq = K230_ReadLE16(packed + 2U); frame.len = packed[4];
    memcpy(frame.data, packed + 5U, 32U);
    return frame;
}
uint32_t Test_Validate(const uint8_t *packed) {
    K230_Frame_t frame = make_frame(packed);
    return (uint32_t)K230_ValidatePayload(&frame);
}
void Test_PolicyInit(void) { FaceAuth_Init(&policy); }
void Test_PolicyLoss(void) { FaceAuth_OnTransportLoss(&policy); }
uint32_t Test_PolicyProcess(const uint8_t *packed) {
    K230_Frame_t frame = make_frame(packed);
    AuthEvent_t event;
    FaceAction_t action;
    memset(&event, 0xA5, sizeof(event));
    action = FaceAuth_Process(&policy, &frame, &event);
    /* Expose fields separately, without relying on enum/structure packing. */
    test_event[0] = (uint8_t)event.source; test_event[1] = (uint8_t)event.result;
    memcpy(test_event + 2U, event.uid, 4U);
    test_event[6] = (uint8_t)event.user_id;
    test_event[7] = (uint8_t)(event.user_id >> 8U);
    test_event[8] = (uint8_t)event.similarity_q;
    test_event[9] = (uint8_t)(event.similarity_q >> 8U);
    test_event[10] = policy.latched;
    test_event[11] = policy.have_seq;
    test_event[12] = (uint8_t)policy.last_seq;
    test_event[13] = (uint8_t)(policy.last_seq >> 8U);
    return (uint32_t)action;
}

/* Freestanding test image: minimal byte routines, no HAL or libc startup. */
static void CopyBytes(void *dest, const void *src, size_t size) {
    volatile unsigned char *d = dest; const volatile unsigned char *s = src;
    while (size--) { *d++ = *s++; }
}
static void SetBytes(void *dest, int value, size_t size) {
    volatile unsigned char *d = dest;
    while (size--) { *d++ = (unsigned char)value; }
}
static void MoveBytes(void *dest, const void *src, size_t size) {
    volatile unsigned char *d = dest; const volatile unsigned char *s = src;
    if (d < s) { while (size--) { *d++ = *s++; } }
    else if (d > s) { d += size; s += size; while (size--) { *--d = *--s; } }
}
void *memcpy(void *d, const void *s, size_t n) { CopyBytes(d,s,n); return d; }
void *memset(void *d, int c, size_t n) { SetBytes(d,c,n); return d; }
void *memmove(void *d, const void *s, size_t n) { MoveBytes(d,s,n); return d; }
void __aeabi_memcpy(void *d, const void *s, size_t n) { CopyBytes(d,s,n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n) { CopyBytes(d,s,n); }
void __aeabi_memclr(void *d, size_t n) { SetBytes(d,0,n); }
void __aeabi_memclr4(void *d, size_t n) { SetBytes(d,0,n); }
void __aeabi_memset(void *d, size_t n, int c) { SetBytes(d,c,n); }
void __aeabi_memset4(void *d, size_t n, int c) { SetBytes(d,c,n); }
void __aeabi_memmove(void *d, const void *s, size_t n) { MoveBytes(d,s,n); }
