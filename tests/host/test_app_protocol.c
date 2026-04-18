#include "app_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t encoded[64];
    AppDrawBeginCommand_t begin = {
        .dst_addr = 0x22U,
        .session_id = 0x31U,
        .flags = APP_DRAW_FLAG_CLEAR_FIRST,
    };
    AppDrawBeginCommand_t decoded_begin;

    size_t used = AppProtocol_EncodeDrawBegin(&begin, encoded, sizeof(encoded));
    assert(used == 3U);
    assert(AppProtocol_DecodeDrawBegin(encoded, used, &decoded_begin));
    assert(decoded_begin.dst_addr == 0x22U);
    assert(decoded_begin.session_id == 0x31U);
    assert(decoded_begin.flags == APP_DRAW_FLAG_CLEAR_FIRST);

    AppDrawTextCommand_t cmd = {
        .dst_addr = 0x22U,
        .session_id = 0x31U,
        .op_index = 0x07U,
        .x = 10U,
        .y = 20U,
        .font_id = APP_FONT_16,
        .text_len = 5U,
    };
    memcpy(cmd.text, "Hello", 5U);

    used = AppProtocol_EncodeDrawText(&cmd, encoded, sizeof(encoded));
    assert(used == 14U);

    AppDrawTextCommand_t decoded;
    assert(AppProtocol_DecodeDrawText(encoded, used, &decoded));
    assert(decoded.dst_addr == 0x22U);
    assert(decoded.session_id == 0x31U);
    assert(decoded.op_index == 0x07U);
    assert(decoded.x == 10U);
    assert(decoded.y == 20U);
    assert(decoded.font_id == APP_FONT_16);
    assert(decoded.text_len == 5U);
    assert(memcmp(decoded.text, "Hello", 5U) == 0);

    uint8_t long_encoded[128];
    AppDrawTextCommand_t long_cmd = {
        .dst_addr = 0x22U,
        .session_id = 0x31U,
        .op_index = 0x08U,
        .x = 1U,
        .y = 2U,
        .font_id = APP_FONT_12,
        .text_len = APP_TEXT_MAX_LEN,
    };
    memset(long_cmd.text, 'A', APP_TEXT_MAX_LEN);

    size_t long_used = AppProtocol_EncodeDrawText(&long_cmd, long_encoded, sizeof(long_encoded));
    assert(long_used == (size_t)(9U + APP_TEXT_MAX_LEN));

    AppDrawTextCommand_t long_decoded;
    assert(AppProtocol_DecodeDrawText(long_encoded, long_used, &long_decoded));
    assert(long_decoded.session_id == 0x31U);
    assert(long_decoded.op_index == 0x08U);
    assert(long_decoded.text_len == APP_TEXT_MAX_LEN);
    assert(memcmp(long_decoded.text, long_cmd.text, APP_TEXT_MAX_LEN) == 0);

    AppDrawCommitCommand_t commit = {
        .dst_addr = 0x22U,
        .session_id = 0x31U,
    };
    AppDrawCommitCommand_t decoded_commit;

    used = AppProtocol_EncodeDrawCommit(&commit, encoded, sizeof(encoded));
    assert(used == 2U);
    assert(AppProtocol_DecodeDrawCommit(encoded, used, &decoded_commit));
    assert(decoded_commit.dst_addr == 0x22U);
    assert(decoded_commit.session_id == 0x31U);

    AppDisplayFlushCommand_t flush = {
        .dst_addr = 0x22U,
        .session_id = 0x31U,
        .full_refresh = true,
    };
    AppDisplayFlushCommand_t decoded_flush;

    used = AppProtocol_EncodeDisplayFlush(&flush, encoded, sizeof(encoded));
    assert(used == 3U);
    assert(AppProtocol_DecodeDisplayFlush(encoded, used, &decoded_flush));
    assert(decoded_flush.dst_addr == 0x22U);
    assert(decoded_flush.session_id == 0x31U);
    assert(decoded_flush.full_refresh);

    return 0;
}
