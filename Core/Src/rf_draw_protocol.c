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

size_t RfDrawProtocol_EncodeBegin(const RfDrawBegin_t *begin, uint8_t *out_buf, size_t out_capacity)
{
    if ((begin == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_BEGIN_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = begin->session_id;
    out_buf[1] = begin->flags;
    return RF_DRAW_BEGIN_PAYLOAD_LEN;
}

bool RfDrawProtocol_DecodeBegin(const uint8_t *buf, size_t len, RfDrawBegin_t *out_begin)
{
    if ((buf == NULL) || (out_begin == NULL) || (len != RF_DRAW_BEGIN_PAYLOAD_LEN)) {
        return false;
    }

    out_begin->session_id = buf[0];
    out_begin->flags = buf[1];
    return true;
}

size_t RfDrawProtocol_EncodeText(const RfDrawText_t *text, uint8_t *out_buf, size_t out_capacity)
{
    size_t total_len;

    if ((text == NULL) || (out_buf == NULL) || (text->text_len > RF_DRAW_TEXT_MAX_LEN)) {
        return 0U;
    }

    total_len = RF_DRAW_TEXT_FIXED_LEN + (size_t)text->text_len;
    if (out_capacity < total_len) {
        return 0U;
    }

    out_buf[0] = text->session_id;
    out_buf[1] = text->op_index;
    RfDrawProtocol_WriteU16LE(&out_buf[2], text->x);
    RfDrawProtocol_WriteU16LE(&out_buf[4], text->y);
    out_buf[6] = text->font_id;
    out_buf[7] = text->text_len;
    if (text->text_len > 0U) {
        memcpy(&out_buf[RF_DRAW_TEXT_FIXED_LEN], text->text, text->text_len);
    }
    return total_len;
}

bool RfDrawProtocol_DecodeText(const uint8_t *buf, size_t len, RfDrawText_t *out_text)
{
    size_t expected_len;

    if ((buf == NULL) || (out_text == NULL) || (len < RF_DRAW_TEXT_FIXED_LEN)) {
        return false;
    }

    expected_len = RF_DRAW_TEXT_FIXED_LEN + (size_t)buf[7];
    if ((buf[7] > RF_DRAW_TEXT_MAX_LEN) || (len != expected_len)) {
        return false;
    }

    out_text->session_id = buf[0];
    out_text->op_index = buf[1];
    out_text->x = RfDrawProtocol_ReadU16LE(&buf[2]);
    out_text->y = RfDrawProtocol_ReadU16LE(&buf[4]);
    out_text->font_id = buf[6];
    out_text->text_len = buf[7];
    if (out_text->text_len > 0U) {
        memcpy(out_text->text, &buf[RF_DRAW_TEXT_FIXED_LEN], out_text->text_len);
    }
    return true;
}

size_t RfDrawProtocol_EncodeTilemap(const RfDrawTilemap_t *tilemap, uint8_t *out_buf, size_t out_capacity)
{
    size_t total_len;

    if ((tilemap == NULL) || (out_buf == NULL)) {
        return 0U;
    }
    if (tilemap->byte_count == 0U || tilemap->byte_count > RF_DRAW_TILEMAP_MAX_BYTES) {
        return 0U;
    }
    total_len = RF_DRAW_TILEMAP_FIXED_LEN + (size_t)tilemap->byte_count;
    if (out_capacity < total_len) {
        return 0U;
    }

    out_buf[0] = tilemap->session_id;
    RfDrawProtocol_WriteU16LE(&out_buf[1], tilemap->tile_offset);
    out_buf[3] = tilemap->byte_count;
    memcpy(&out_buf[RF_DRAW_TILEMAP_FIXED_LEN], tilemap->packed_ids, tilemap->byte_count);
    return total_len;
}

bool RfDrawProtocol_DecodeTilemap(const uint8_t *buf, size_t len, RfDrawTilemap_t *out_tilemap)
{
    uint8_t byte_count;
    size_t expected_len;

    if ((buf == NULL) || (out_tilemap == NULL) || (len < RF_DRAW_TILEMAP_FIXED_LEN)) {
        return false;
    }
    byte_count = buf[3];
    expected_len = RF_DRAW_TILEMAP_FIXED_LEN + (size_t)byte_count;
    if (byte_count == 0U || byte_count > RF_DRAW_TILEMAP_MAX_BYTES || len != expected_len) {
        return false;
    }

    out_tilemap->session_id = buf[0];
    out_tilemap->tile_offset = RfDrawProtocol_ReadU16LE(&buf[1]);
    out_tilemap->byte_count = byte_count;
    memcpy(out_tilemap->packed_ids, &buf[RF_DRAW_TILEMAP_FIXED_LEN], byte_count);
    return true;
}

size_t RfDrawProtocol_EncodeCommit(const RfDrawCommit_t *commit, uint8_t *out_buf, size_t out_capacity)
{
    if ((commit == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_COMMIT_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = commit->session_id;
    return RF_DRAW_COMMIT_PAYLOAD_LEN;
}

bool RfDrawProtocol_DecodeCommit(const uint8_t *buf, size_t len, RfDrawCommit_t *out_commit)
{
    if ((buf == NULL) || (out_commit == NULL) || (len != RF_DRAW_COMMIT_PAYLOAD_LEN)) {
        return false;
    }

    out_commit->session_id = buf[0];
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
