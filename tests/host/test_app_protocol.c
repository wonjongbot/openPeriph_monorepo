#include "app_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t encoded[64];
    AppDrawTextCommand_t cmd = {
        .dst_addr = 0x22U,
        .x = 10U,
        .y = 20U,
        .font_id = APP_FONT_16,
        .flags = (uint8_t)(APP_DRAW_FLAG_CLEAR_FIRST | APP_DRAW_FLAG_FULL_REFRESH),
        .text_len = 5U,
    };
    memcpy(cmd.text, "Hello", 5U);

    size_t used = AppProtocol_EncodeDrawText(&cmd, encoded, sizeof(encoded));
    assert(used == 13U);

    AppDrawTextCommand_t decoded;
    assert(AppProtocol_DecodeDrawText(encoded, used, &decoded));
    assert(decoded.dst_addr == 0x22U);
    assert(decoded.x == 10U);
    assert(decoded.y == 20U);
    assert(decoded.font_id == APP_FONT_16);
    assert(decoded.flags == (APP_DRAW_FLAG_CLEAR_FIRST | APP_DRAW_FLAG_FULL_REFRESH));
    assert(decoded.text_len == 5U);
    assert(memcmp(decoded.text, "Hello", 5U) == 0);

    return 0;
}
