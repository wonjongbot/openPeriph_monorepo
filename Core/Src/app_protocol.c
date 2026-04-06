#include "app_protocol.h"

#include <string.h>

#define APP_DRAW_TEXT_FIXED_SIZE 8U

size_t AppProtocol_EncodeDrawText(const AppDrawTextCommand_t *cmd,
                                  uint8_t *out_buf,
                                  size_t out_capacity)
{
    if (cmd == NULL || out_buf == NULL) {
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
    out_buf[1] = (uint8_t)(cmd->x & 0xFFU);
    out_buf[2] = (uint8_t)(cmd->x >> 8);
    out_buf[3] = (uint8_t)(cmd->y & 0xFFU);
    out_buf[4] = (uint8_t)(cmd->y >> 8);
    out_buf[5] = cmd->font_id;
    out_buf[6] = cmd->flags;
    out_buf[7] = cmd->text_len;
    if (cmd->text_len > 0U) {
        memcpy(&out_buf[APP_DRAW_TEXT_FIXED_SIZE], cmd->text, cmd->text_len);
    }

    return total_len;
}

bool AppProtocol_DecodeDrawText(const uint8_t *buf,
                                size_t len,
                                AppDrawTextCommand_t *out_cmd)
{
    if (buf == NULL || out_cmd == NULL) {
        return false;
    }
    if (len < APP_DRAW_TEXT_FIXED_SIZE) {
        return false;
    }

    const uint8_t text_len = buf[7];
    const size_t total_len = APP_DRAW_TEXT_FIXED_SIZE + (size_t)text_len;
    if (text_len > APP_TEXT_MAX_LEN) {
        return false;
    }
    if (len != total_len) {
        return false;
    }

    out_cmd->dst_addr = buf[0];
    out_cmd->x = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
    out_cmd->y = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
    out_cmd->font_id = buf[5];
    out_cmd->flags = buf[6];
    out_cmd->text_len = text_len;

    if (text_len > 0U) {
        memcpy(out_cmd->text, &buf[APP_DRAW_TEXT_FIXED_SIZE], text_len);
    }

    return true;
}
