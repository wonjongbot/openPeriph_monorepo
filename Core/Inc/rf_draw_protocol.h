#ifndef RF_DRAW_PROTOCOL_H
#define RF_DRAW_PROTOCOL_H

#include "app_protocol.h"
#include "rf_frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RF_DRAW_BEGIN_PAYLOAD_LEN 2U
#define RF_DRAW_TEXT_FIXED_LEN 8U
#define RF_DRAW_TEXT_MAX_LEN 24U
#define RF_DRAW_COMMIT_PAYLOAD_LEN 1U
#define RF_DRAW_ACK_PAYLOAD_LEN 2U
#define RF_DRAW_ERROR_PAYLOAD_LEN 2U

typedef enum {
    RF_DRAW_PHASE_BEGIN = 1U,
    RF_DRAW_PHASE_TEXT = 2U,
    RF_DRAW_PHASE_COMMIT = 3U,
    RF_DRAW_PHASE_FLUSH = 4U,
} RfDrawPhase_t;

typedef enum {
    RF_DRAW_ERROR_REASON_BAD_STATE = 1U,
    RF_DRAW_ERROR_REASON_BAD_CHUNK = 2U,
    RF_DRAW_ERROR_REASON_LENGTH = 3U,
    RF_DRAW_ERROR_REASON_RENDER = 4U,
} RfDrawErrorReason_t;

typedef struct {
    uint8_t session_id;
    uint8_t flags;
} RfDrawBegin_t;

typedef struct {
    uint8_t session_id;
    uint8_t op_index;
    uint16_t x;
    uint16_t y;
    uint8_t font_id;
    uint8_t text_len;
    uint8_t text[RF_DRAW_TEXT_MAX_LEN];
} RfDrawText_t;

typedef struct {
    uint8_t session_id;
} RfDrawCommit_t;

typedef struct {
    uint8_t phase;
    uint8_t value;
} RfDrawAck_t;

typedef struct {
    uint8_t phase;
    uint8_t reason;
} RfDrawError_t;

size_t RfDrawProtocol_EncodeBegin(const RfDrawBegin_t *begin, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeBegin(const uint8_t *buf, size_t len, RfDrawBegin_t *out_begin);
size_t RfDrawProtocol_EncodeText(const RfDrawText_t *text, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeText(const uint8_t *buf, size_t len, RfDrawText_t *out_text);
size_t RfDrawProtocol_EncodeCommit(const RfDrawCommit_t *commit, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeCommit(const uint8_t *buf, size_t len, RfDrawCommit_t *out_commit);
size_t RfDrawProtocol_EncodeAck(const RfDrawAck_t *ack, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeAck(const uint8_t *buf, size_t len, RfDrawAck_t *out_ack);
size_t RfDrawProtocol_EncodeError(const RfDrawError_t *error, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeError(const uint8_t *buf, size_t len, RfDrawError_t *out_error);

#endif /* RF_DRAW_PROTOCOL_H */
