#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_TEXT_MAX_LEN 40U

#define APP_CMD_DRAW_TEXT 0x20U

#define APP_FONT_12 0x01U
#define APP_FONT_16 0x02U

#define APP_DRAW_FLAG_CLEAR_FIRST 0x01U
#define APP_DRAW_FLAG_FULL_REFRESH 0x02U

typedef struct {
    uint8_t dst_addr;
    uint16_t x;
    uint16_t y;
    uint8_t font_id;
    uint8_t flags;
    uint8_t text_len;
    uint8_t text[APP_TEXT_MAX_LEN];
} AppDrawTextCommand_t;

size_t AppProtocol_EncodeDrawText(const AppDrawTextCommand_t *cmd,
                                  uint8_t *out_buf,
                                  size_t out_capacity);

bool AppProtocol_DecodeDrawText(const uint8_t *buf,
                                size_t len,
                                AppDrawTextCommand_t *out_cmd);

#endif /* APP_PROTOCOL_H */
