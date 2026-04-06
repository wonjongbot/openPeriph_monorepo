#ifndef RF_FRAME_H
#define RF_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RF_FRAME_VERSION 1U
#define RF_FRAME_MAX_PAYLOAD 48U

typedef enum {
    RF_MSG_DRAW_TEXT = 0x01,
    RF_MSG_ACK = 0x80,
    RF_MSG_ERROR = 0x81,
} RfMessageType_t;

typedef struct {
    uint8_t version;
    uint8_t msg_type;
    uint8_t dst_addr;
    uint8_t src_addr;
    uint8_t seq;
    uint8_t payload_len;
    uint8_t payload[RF_FRAME_MAX_PAYLOAD];
} RfFrame_t;

size_t RfFrame_Encode(const RfFrame_t *frame, uint8_t *out_buf, size_t out_capacity);
bool RfFrame_Decode(const uint8_t *buf, size_t len, RfFrame_t *out_frame);

#endif /* RF_FRAME_H */
