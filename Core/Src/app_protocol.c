#include "app_protocol.h"

#include <string.h>

#define APP_DRAW_BEGIN_PAYLOAD_LEN 3U
#define APP_DRAW_TEXT_FIXED_SIZE 9U
#define APP_DRAW_TILEMAP_FIXED_SIZE 5U
#define APP_DRAW_COMMIT_PAYLOAD_LEN 2U
#define APP_DISPLAY_FLUSH_PAYLOAD_LEN 3U

size_t AppProtocol_EncodeDrawBegin(const AppDrawBeginCommand_t *cmd,
                                   uint8_t *out_buf,
                                   size_t out_capacity)
{
    if ((cmd == NULL) || (out_buf == NULL) || (out_capacity < APP_DRAW_BEGIN_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = cmd->dst_addr;
    out_buf[1] = cmd->session_id;
    out_buf[2] = cmd->flags;
    return APP_DRAW_BEGIN_PAYLOAD_LEN;
}

bool AppProtocol_DecodeDrawBegin(const uint8_t *buf,
                                 size_t len,
                                 AppDrawBeginCommand_t *out_cmd)
{
    if ((buf == NULL) || (out_cmd == NULL) || (len != APP_DRAW_BEGIN_PAYLOAD_LEN)) {
        return false;
    }

    out_cmd->dst_addr = buf[0];
    out_cmd->session_id = buf[1];
    out_cmd->flags = buf[2];
    return true;
}

size_t AppProtocol_EncodeDrawText(const AppDrawTextCommand_t *cmd,
                                  uint8_t *out_buf,
                                  size_t out_capacity)
{
    if ((cmd == NULL) || (out_buf == NULL)) {
        return 0U;
    }
    if (cmd->text_len > APP_TEXT_MAX_LEN) {
        return 0U;
    }

    const size_t total_len = APP_DRAW_TEXT_FIXED_SIZE + (size_t)cmd->text_len;
    if (out_capacity < total_len) {
        return 0U;
    }

    out_buf[0] = cmd->dst_addr;
    out_buf[1] = cmd->session_id;
    out_buf[2] = cmd->op_index;
    out_buf[3] = (uint8_t)(cmd->x & 0xFFU);
    out_buf[4] = (uint8_t)(cmd->x >> 8);
    out_buf[5] = (uint8_t)(cmd->y & 0xFFU);
    out_buf[6] = (uint8_t)(cmd->y >> 8);
    out_buf[7] = cmd->font_id;
    out_buf[8] = cmd->text_len;
    if (cmd->text_len > 0U) {
        memcpy(&out_buf[APP_DRAW_TEXT_FIXED_SIZE], cmd->text, cmd->text_len);
    }

    return total_len;
}

bool AppProtocol_DecodeDrawText(const uint8_t *buf,
                                size_t len,
                                AppDrawTextCommand_t *out_cmd)
{
    if ((buf == NULL) || (out_cmd == NULL)) {
        return false;
    }
    if (len < APP_DRAW_TEXT_FIXED_SIZE) {
        return false;
    }

    const uint8_t text_len = buf[8];
    const size_t total_len = APP_DRAW_TEXT_FIXED_SIZE + (size_t)text_len;
    if (text_len > APP_TEXT_MAX_LEN) {
        return false;
    }
    if (len != total_len) {
        return false;
    }

    out_cmd->dst_addr = buf[0];
    out_cmd->session_id = buf[1];
    out_cmd->op_index = buf[2];
    out_cmd->x = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
    out_cmd->y = (uint16_t)buf[5] | ((uint16_t)buf[6] << 8);
    out_cmd->font_id = buf[7];
    out_cmd->text_len = text_len;

    if (text_len > 0U) {
        memcpy(out_cmd->text, &buf[APP_DRAW_TEXT_FIXED_SIZE], text_len);
    }

    return true;
}

size_t AppProtocol_EncodeDrawTilemap(const AppDrawTilemapCommand_t *cmd,
                                     uint8_t *out_buf,
                                     size_t out_capacity)
{
    size_t total_len;

    if ((cmd == NULL) || (out_buf == NULL)) {
        return 0U;
    }
    if (cmd->byte_count == 0U || cmd->byte_count > APP_DRAW_TILEMAP_MAX_BYTES) {
        return 0U;
    }
    total_len = APP_DRAW_TILEMAP_FIXED_SIZE + (size_t)cmd->byte_count;
    if (out_capacity < total_len) {
        return 0U;
    }

    out_buf[0] = cmd->dst_addr;
    out_buf[1] = cmd->session_id;
    out_buf[2] = (uint8_t)(cmd->tile_offset & 0xFFU);
    out_buf[3] = (uint8_t)(cmd->tile_offset >> 8);
    out_buf[4] = cmd->byte_count;
    memcpy(&out_buf[APP_DRAW_TILEMAP_FIXED_SIZE], cmd->packed_ids, cmd->byte_count);
    return total_len;
}

bool AppProtocol_DecodeDrawTilemap(const uint8_t *buf,
                                   size_t len,
                                   AppDrawTilemapCommand_t *out_cmd)
{
    uint8_t byte_count;
    size_t total_len;

    if ((buf == NULL) || (out_cmd == NULL) || (len < APP_DRAW_TILEMAP_FIXED_SIZE)) {
        return false;
    }
    byte_count = buf[4];
    total_len = APP_DRAW_TILEMAP_FIXED_SIZE + (size_t)byte_count;
    if (byte_count == 0U || byte_count > APP_DRAW_TILEMAP_MAX_BYTES || len != total_len) {
        return false;
    }

    out_cmd->dst_addr = buf[0];
    out_cmd->session_id = buf[1];
    out_cmd->tile_offset = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    out_cmd->byte_count = byte_count;
    memcpy(out_cmd->packed_ids, &buf[APP_DRAW_TILEMAP_FIXED_SIZE], byte_count);
    return true;
}

size_t AppProtocol_EncodeDrawCommit(const AppDrawCommitCommand_t *cmd,
                                    uint8_t *out_buf,
                                    size_t out_capacity)
{
    if ((cmd == NULL) || (out_buf == NULL) || (out_capacity < APP_DRAW_COMMIT_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = cmd->dst_addr;
    out_buf[1] = cmd->session_id;
    return APP_DRAW_COMMIT_PAYLOAD_LEN;
}

bool AppProtocol_DecodeDrawCommit(const uint8_t *buf,
                                  size_t len,
                                  AppDrawCommitCommand_t *out_cmd)
{
    if ((buf == NULL) || (out_cmd == NULL) || (len != APP_DRAW_COMMIT_PAYLOAD_LEN)) {
        return false;
    }

    out_cmd->dst_addr = buf[0];
    out_cmd->session_id = buf[1];
    return true;
}

size_t AppProtocol_EncodeDisplayFlush(const AppDisplayFlushCommand_t *cmd,
                                      uint8_t *out_buf,
                                      size_t out_capacity)
{
    if ((cmd == NULL) || (out_buf == NULL) || (out_capacity < APP_DISPLAY_FLUSH_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = cmd->dst_addr;
    out_buf[1] = cmd->session_id;
    out_buf[2] = cmd->full_refresh ? 1U : 0U;
    return APP_DISPLAY_FLUSH_PAYLOAD_LEN;
}

bool AppProtocol_DecodeDisplayFlush(const uint8_t *buf,
                                    size_t len,
                                    AppDisplayFlushCommand_t *out_cmd)
{
    if ((buf == NULL) || (out_cmd == NULL) || (len != APP_DISPLAY_FLUSH_PAYLOAD_LEN)) {
        return false;
    }

    out_cmd->dst_addr = buf[0];
    out_cmd->session_id = buf[1];
    out_cmd->full_refresh = (buf[2] != 0U);
    return true;
}
