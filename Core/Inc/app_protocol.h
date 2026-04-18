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
#define APP_DRAW_FLAG_DEFER_REFRESH 0x04U

typedef struct {
    uint8_t dst_addr;
    uint8_t session_id;
    uint8_t flags;
} AppDrawBeginCommand_t;

/* text is length-delimited payload data; it is not guaranteed to be NUL-terminated */
typedef struct {
    uint8_t dst_addr;
    uint8_t session_id;
    uint8_t op_index;
    uint16_t x;
    uint16_t y;
    uint8_t font_id;
    uint8_t text_len;
    uint8_t text[APP_TEXT_MAX_LEN];
} AppDrawTextCommand_t;

typedef struct {
    uint8_t dst_addr;
    uint8_t session_id;
} AppDrawCommitCommand_t;

typedef struct {
    uint8_t dst_addr;
    uint8_t session_id;
    bool full_refresh;
} AppDisplayFlushCommand_t;

size_t AppProtocol_EncodeDrawBegin(const AppDrawBeginCommand_t *cmd,
                                   uint8_t *out_buf,
                                   size_t out_capacity);
bool AppProtocol_DecodeDrawBegin(const uint8_t *buf,
                                 size_t len,
                                 AppDrawBeginCommand_t *out_cmd);

size_t AppProtocol_EncodeDrawText(const AppDrawTextCommand_t *cmd,
                                  uint8_t *out_buf,
                                  size_t out_capacity);

bool AppProtocol_DecodeDrawText(const uint8_t *buf,
                                size_t len,
                                AppDrawTextCommand_t *out_cmd);

size_t AppProtocol_EncodeDrawCommit(const AppDrawCommitCommand_t *cmd,
                                    uint8_t *out_buf,
                                    size_t out_capacity);
bool AppProtocol_DecodeDrawCommit(const uint8_t *buf,
                                  size_t len,
                                  AppDrawCommitCommand_t *out_cmd);

size_t AppProtocol_EncodeDisplayFlush(const AppDisplayFlushCommand_t *cmd,
                                      uint8_t *out_buf,
                                      size_t out_capacity);
bool AppProtocol_DecodeDisplayFlush(const uint8_t *buf,
                                    size_t len,
                                    AppDisplayFlushCommand_t *out_cmd);

#endif /* APP_PROTOCOL_H */
