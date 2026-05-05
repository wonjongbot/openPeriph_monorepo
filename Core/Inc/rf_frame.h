#ifndef RF_FRAME_H
#define RF_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RF_FRAME_VERSION 1U
#define RF_FRAME_MAX_PAYLOAD 48U

typedef enum {
    RF_MSG_DRAW_START = 0x01,
    RF_MSG_DRAW_CHUNK = 0x02,
    RF_MSG_DRAW_COMMIT = 0x03,
    RF_MSG_DRAW_ACK = 0x04,
    RF_MSG_DRAW_ERROR = 0x05,
    RF_MSG_PING = 0x06,
    RF_MSG_PONG = 0x07,
    RF_MSG_DISPLAY_FLUSH = 0x08,
    RF_MSG_DRAW_BEGIN = 0x09,
    RF_MSG_DRAW_TEXT = 0x0A,
    RF_MSG_AGENT_TRIGGER = 0x0B,
    RF_MSG_DRAW_TILEMAP = 0x0C,
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
