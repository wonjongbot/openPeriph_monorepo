#include "rf_draw_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t buf[RF_FRAME_MAX_PAYLOAD];
    size_t used;

    RfDrawBegin_t begin = {
        .session_id = 0x31U,
        .flags = APP_DRAW_FLAG_CLEAR_FIRST,
    };
    RfDrawBegin_t decoded_begin;

    assert(RfDrawProtocol_EncodeBegin(&begin, buf, RF_DRAW_BEGIN_PAYLOAD_LEN - 1U) == 0U);
    used = RfDrawProtocol_EncodeBegin(&begin, buf, sizeof(buf));
    assert(used == RF_DRAW_BEGIN_PAYLOAD_LEN);
    assert(RfDrawProtocol_DecodeBegin(buf, used, &decoded_begin));
    assert(decoded_begin.session_id == 0x31U);
    assert(decoded_begin.flags == APP_DRAW_FLAG_CLEAR_FIRST);

    RfDrawText_t text = {
        .session_id = 0x31U,
        .op_index = 0x07U,
        .x = 12U,
        .y = 34U,
        .font_id = APP_FONT_16,
        .text_len = 5U,
    };
    RfDrawText_t decoded_text;
    uint8_t text_buf[RF_DRAW_TEXT_FIXED_LEN + 5U + 1U];
    memcpy(text.text, "Hello", 5U);

    assert(RfDrawProtocol_EncodeText(&text, buf, RF_DRAW_TEXT_FIXED_LEN + 4U) == 0U);
    used = RfDrawProtocol_EncodeText(&text, buf, sizeof(buf));
    assert(used == RF_DRAW_TEXT_FIXED_LEN + 5U);
    assert(RfDrawProtocol_DecodeText(buf, used, &decoded_text));
    assert(decoded_text.session_id == 0x31U);
    assert(decoded_text.op_index == 0x07U);
    assert(decoded_text.x == 12U);
    assert(decoded_text.y == 34U);
    assert(decoded_text.font_id == APP_FONT_16);
    assert(decoded_text.text_len == 5U);
    assert(memcmp(decoded_text.text, "Hello", 5U) == 0);

    assert(!RfDrawProtocol_DecodeText(buf, RF_DRAW_TEXT_FIXED_LEN - 1U, &decoded_text));
    memcpy(text_buf, buf, used);
    text_buf[used] = 0xEEU;
    assert(!RfDrawProtocol_DecodeText(text_buf, used + 1U, &decoded_text));

    RfDrawText_t max_text = {
        .session_id = 0x32U,
        .op_index = 0x08U,
        .x = 1U,
        .y = 2U,
        .font_id = APP_FONT_12,
        .text_len = RF_DRAW_TEXT_MAX_LEN,
    };
    memset(max_text.text, 0x5AU, RF_DRAW_TEXT_MAX_LEN);

    assert(RfDrawProtocol_EncodeText(&max_text, buf, RF_DRAW_TEXT_FIXED_LEN + RF_DRAW_TEXT_MAX_LEN - 1U) == 0U);
    used = RfDrawProtocol_EncodeText(&max_text, buf, sizeof(buf));
    assert(used == RF_DRAW_TEXT_FIXED_LEN + RF_DRAW_TEXT_MAX_LEN);
    assert(RfDrawProtocol_DecodeText(buf, used, &decoded_text));
    assert(decoded_text.session_id == 0x32U);
    assert(decoded_text.text_len == RF_DRAW_TEXT_MAX_LEN);
    assert(memcmp(decoded_text.text, max_text.text, RF_DRAW_TEXT_MAX_LEN) == 0);

    RfDrawText_t too_large_text = max_text;
    too_large_text.text_len = (uint8_t)(RF_DRAW_TEXT_MAX_LEN + 1U);
    assert(RfDrawProtocol_EncodeText(&too_large_text, buf, sizeof(buf)) == 0U);

    RfDrawTilemap_t tilemap = {
        .session_id = 0x31U,
        .tile_offset = 120U,
        .byte_count = 4U,
        .packed_ids = { 0x12U, 0x34U, 0x56U, 0x78U },
    };
    RfDrawTilemap_t decoded_tilemap;

    assert(RfDrawProtocol_EncodeTilemap(&tilemap, buf, RF_DRAW_TILEMAP_FIXED_LEN + 3U) == 0U);
    used = RfDrawProtocol_EncodeTilemap(&tilemap, buf, sizeof(buf));
    assert(used == RF_DRAW_TILEMAP_FIXED_LEN + 4U);
    assert(RfDrawProtocol_DecodeTilemap(buf, used, &decoded_tilemap));
    assert(decoded_tilemap.session_id == 0x31U);
    assert(decoded_tilemap.tile_offset == 120U);
    assert(decoded_tilemap.byte_count == 4U);
    assert(memcmp(decoded_tilemap.packed_ids, tilemap.packed_ids, 4U) == 0);

    RfDrawCommit_t commit = {
        .session_id = 0x31U,
    };
    RfDrawCommit_t decoded_commit;

    assert(RfDrawProtocol_EncodeCommit(&commit, buf, RF_DRAW_COMMIT_PAYLOAD_LEN - 1U) == 0U);
    used = RfDrawProtocol_EncodeCommit(&commit, buf, sizeof(buf));
    assert(used == RF_DRAW_COMMIT_PAYLOAD_LEN);
    assert(RfDrawProtocol_DecodeCommit(buf, used, &decoded_commit));
    assert(decoded_commit.session_id == 0x31U);

    RfDrawAck_t ack = {
        .phase = RF_DRAW_PHASE_BEGIN,
        .value = 0U,
    };
    RfDrawAck_t decoded_ack;

    assert(RfDrawProtocol_EncodeAck(&ack, buf, RF_DRAW_ACK_PAYLOAD_LEN - 1U) == 0U);
    used = RfDrawProtocol_EncodeAck(&ack, buf, sizeof(buf));
    assert(used == RF_DRAW_ACK_PAYLOAD_LEN);
    assert(RfDrawProtocol_DecodeAck(buf, used, &decoded_ack));
    assert(decoded_ack.phase == RF_DRAW_PHASE_BEGIN);
    assert(decoded_ack.value == 0U);

    RfDrawError_t error = {
        .phase = RF_DRAW_PHASE_FLUSH,
        .reason = RF_DRAW_ERROR_REASON_RENDER,
    };
    RfDrawError_t decoded_error;

    assert(RfDrawProtocol_EncodeError(&error, buf, RF_DRAW_ERROR_PAYLOAD_LEN - 1U) == 0U);
    used = RfDrawProtocol_EncodeError(&error, buf, sizeof(buf));
    assert(used == RF_DRAW_ERROR_PAYLOAD_LEN);
    assert(RfDrawProtocol_DecodeError(buf, used, &decoded_error));
    assert(decoded_error.phase == RF_DRAW_PHASE_FLUSH);
    assert(decoded_error.reason == RF_DRAW_ERROR_REASON_RENDER);

    return 0;
}
