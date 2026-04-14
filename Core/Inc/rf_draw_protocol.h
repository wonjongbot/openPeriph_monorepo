#ifndef RF_DRAW_PROTOCOL_H
#define RF_DRAW_PROTOCOL_H

#include "app_protocol.h"
#include "rf_frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RF_DRAW_START_PAYLOAD_LEN 7U
#define RF_DRAW_CHUNK_HEADER_LEN 2U
#define RF_DRAW_CHUNK_MAX_DATA (RF_FRAME_MAX_PAYLOAD - RF_DRAW_CHUNK_HEADER_LEN)
#define RF_DRAW_ACK_PAYLOAD_LEN 2U
#define RF_DRAW_ERROR_PAYLOAD_LEN 2U

typedef enum {
    RF_DRAW_PHASE_START = 1U,
    RF_DRAW_PHASE_CHUNK = 2U,
    RF_DRAW_PHASE_COMMIT = 3U,
} RfDrawPhase_t;

typedef enum {
    RF_DRAW_ERROR_REASON_BAD_STATE = 1U,
    RF_DRAW_ERROR_REASON_BAD_CHUNK = 2U,
    RF_DRAW_ERROR_REASON_LENGTH = 3U,
    RF_DRAW_ERROR_REASON_RENDER = 4U,
} RfDrawErrorReason_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t font_id;
    uint8_t flags;
    uint8_t total_text_len;
} RfDrawStart_t;

typedef struct {
    uint8_t chunk_index;
    uint8_t chunk_len;
    uint8_t data[RF_DRAW_CHUNK_MAX_DATA];
} RfDrawChunk_t;

typedef struct {
    uint8_t phase;
    uint8_t value;
} RfDrawAck_t;

typedef struct {
    uint8_t phase;
    uint8_t reason;
} RfDrawError_t;

size_t RfDrawProtocol_EncodeStart(const RfDrawStart_t *start, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeStart(const uint8_t *buf, size_t len, RfDrawStart_t *out_start);
size_t RfDrawProtocol_EncodeChunk(const RfDrawChunk_t *chunk, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeChunk(const uint8_t *buf, size_t len, RfDrawChunk_t *out_chunk);
size_t RfDrawProtocol_EncodeAck(const RfDrawAck_t *ack, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeAck(const uint8_t *buf, size_t len, RfDrawAck_t *out_ack);
size_t RfDrawProtocol_EncodeError(const RfDrawError_t *error, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeError(const uint8_t *buf, size_t len, RfDrawError_t *out_error);

#endif /* RF_DRAW_PROTOCOL_H */
