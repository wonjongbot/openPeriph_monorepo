#include "rf_draw_protocol.h"

#include <string.h>

static void RfDrawProtocol_WriteU16LE(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFFU);
    buf[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static uint16_t RfDrawProtocol_ReadU16LE(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

size_t RfDrawProtocol_EncodeStart(const RfDrawStart_t *start, uint8_t *out_buf, size_t out_capacity)
{
    if ((start == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_START_PAYLOAD_LEN)) {
        return 0U;
    }

    RfDrawProtocol_WriteU16LE(&out_buf[0], start->x);
    RfDrawProtocol_WriteU16LE(&out_buf[2], start->y);
    out_buf[4] = start->font_id;
    out_buf[5] = start->flags;
    out_buf[6] = start->total_text_len;
    return RF_DRAW_START_PAYLOAD_LEN;
}

bool RfDrawProtocol_DecodeStart(const uint8_t *buf, size_t len, RfDrawStart_t *out_start)
{
    if ((buf == NULL) || (out_start == NULL) || (len != RF_DRAW_START_PAYLOAD_LEN)) {
        return false;
    }

    out_start->x = RfDrawProtocol_ReadU16LE(&buf[0]);
    out_start->y = RfDrawProtocol_ReadU16LE(&buf[2]);
    out_start->font_id = buf[4];
    out_start->flags = buf[5];
    out_start->total_text_len = buf[6];
    return true;
}

size_t RfDrawProtocol_EncodeChunk(const RfDrawChunk_t *chunk, uint8_t *out_buf, size_t out_capacity)
{
    size_t total_len;

    if ((chunk == NULL) || (out_buf == NULL) || (chunk->chunk_len > RF_DRAW_CHUNK_MAX_DATA)) {
        return 0U;
    }

    total_len = RF_DRAW_CHUNK_HEADER_LEN + (size_t)chunk->chunk_len;
    if (out_capacity < total_len) {
        return 0U;
    }

    out_buf[0] = chunk->chunk_index;
    out_buf[1] = chunk->chunk_len;
    if (chunk->chunk_len > 0U) {
        memcpy(&out_buf[2], chunk->data, chunk->chunk_len);
    }
    return total_len;
}

bool RfDrawProtocol_DecodeChunk(const uint8_t *buf, size_t len, RfDrawChunk_t *out_chunk)
{
    size_t expected_len;

    if ((buf == NULL) || (out_chunk == NULL) || (len < RF_DRAW_CHUNK_HEADER_LEN)) {
        return false;
    }

    expected_len = RF_DRAW_CHUNK_HEADER_LEN + (size_t)buf[1];
    if ((buf[1] > RF_DRAW_CHUNK_MAX_DATA) || (len != expected_len)) {
        return false;
    }

    out_chunk->chunk_index = buf[0];
    out_chunk->chunk_len = buf[1];
    if (out_chunk->chunk_len > 0U) {
        memcpy(out_chunk->data, &buf[2], out_chunk->chunk_len);
    }
    return true;
}

size_t RfDrawProtocol_EncodeAck(const RfDrawAck_t *ack, uint8_t *out_buf, size_t out_capacity)
{
    if ((ack == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_ACK_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = ack->phase;
    out_buf[1] = ack->value;
    return RF_DRAW_ACK_PAYLOAD_LEN;
}

bool RfDrawProtocol_DecodeAck(const uint8_t *buf, size_t len, RfDrawAck_t *out_ack)
{
    if ((buf == NULL) || (out_ack == NULL) || (len != RF_DRAW_ACK_PAYLOAD_LEN)) {
        return false;
    }

    out_ack->phase = buf[0];
    out_ack->value = buf[1];
    return true;
}

size_t RfDrawProtocol_EncodeError(const RfDrawError_t *error, uint8_t *out_buf, size_t out_capacity)
{
    if ((error == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_ERROR_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = error->phase;
    out_buf[1] = error->reason;
    return RF_DRAW_ERROR_PAYLOAD_LEN;
}

bool RfDrawProtocol_DecodeError(const uint8_t *buf, size_t len, RfDrawError_t *out_error)
{
    if ((buf == NULL) || (out_error == NULL) || (len != RF_DRAW_ERROR_PAYLOAD_LEN)) {
        return false;
    }

    out_error->phase = buf[0];
    out_error->reason = buf[1];
    return true;
}
