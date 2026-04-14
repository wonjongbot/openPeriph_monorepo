#include "rf_draw_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t buf[RF_FRAME_MAX_PAYLOAD];
    size_t used;

    RfDrawStart_t start = {
        .x = 12U,
        .y = 34U,
        .font_id = APP_FONT_16,
        .flags = APP_DRAW_FLAG_CLEAR_FIRST,
        .total_text_len = 5U,
    };
    RfDrawStart_t decoded_start;

    assert(RfDrawProtocol_EncodeStart(&start, buf, RF_DRAW_START_PAYLOAD_LEN - 1U) == 0U);
    used = RfDrawProtocol_EncodeStart(&start, buf, sizeof(buf));
    assert(used == RF_DRAW_START_PAYLOAD_LEN);
    assert(RfDrawProtocol_DecodeStart(buf, used, &decoded_start));
    assert(decoded_start.x == 12U);
    assert(decoded_start.y == 34U);
    assert(decoded_start.font_id == APP_FONT_16);
    assert(decoded_start.flags == APP_DRAW_FLAG_CLEAR_FIRST);
    assert(decoded_start.total_text_len == 5U);

    RfDrawChunk_t chunk = {
        .chunk_index = 1U,
        .chunk_len = 3U,
        .data = {'a', 'b', 'c'},
    };
    RfDrawChunk_t decoded_chunk;
    uint8_t chunk_buf[RF_DRAW_CHUNK_HEADER_LEN + 3U + 1U];

    assert(RfDrawProtocol_EncodeChunk(&chunk, buf, RF_DRAW_CHUNK_HEADER_LEN + 2U) == 0U);
    used = RfDrawProtocol_EncodeChunk(&chunk, buf, sizeof(buf));
    assert(used == RF_DRAW_CHUNK_HEADER_LEN + 3U);
    assert(RfDrawProtocol_DecodeChunk(buf, used, &decoded_chunk));
    assert(decoded_chunk.chunk_index == 1U);
    assert(decoded_chunk.chunk_len == 3U);
    assert(memcmp(decoded_chunk.data, "abc", 3U) == 0);

    assert(!RfDrawProtocol_DecodeChunk(buf, 2U, &decoded_chunk));
    memcpy(chunk_buf, buf, used);
    chunk_buf[used] = 0xEEU;
    assert(!RfDrawProtocol_DecodeChunk(chunk_buf, used + 1U, &decoded_chunk));

    RfDrawChunk_t max_chunk = {
        .chunk_index = 2U,
        .chunk_len = RF_DRAW_CHUNK_MAX_DATA,
    };
    memset(max_chunk.data, 0x5AU, RF_DRAW_CHUNK_MAX_DATA);

    assert(RfDrawProtocol_EncodeChunk(&max_chunk, buf, RF_DRAW_CHUNK_HEADER_LEN + RF_DRAW_CHUNK_MAX_DATA - 1U) == 0U);
    used = RfDrawProtocol_EncodeChunk(&max_chunk, buf, sizeof(buf));
    assert(used == RF_DRAW_CHUNK_HEADER_LEN + RF_DRAW_CHUNK_MAX_DATA);
    assert(RfDrawProtocol_DecodeChunk(buf, used, &decoded_chunk));
    assert(decoded_chunk.chunk_index == 2U);
    assert(decoded_chunk.chunk_len == RF_DRAW_CHUNK_MAX_DATA);
    assert(memcmp(decoded_chunk.data, max_chunk.data, RF_DRAW_CHUNK_MAX_DATA) == 0);

    RfDrawChunk_t too_large_chunk = max_chunk;
    too_large_chunk.chunk_len = (uint8_t)(RF_DRAW_CHUNK_MAX_DATA + 1U);
    assert(RfDrawProtocol_EncodeChunk(&too_large_chunk, buf, sizeof(buf)) == 0U);

    RfDrawAck_t ack = {
        .phase = RF_DRAW_PHASE_START,
        .value = 0U,
    };
    RfDrawAck_t decoded_ack;

    assert(RfDrawProtocol_EncodeAck(&ack, buf, RF_DRAW_ACK_PAYLOAD_LEN - 1U) == 0U);
    used = RfDrawProtocol_EncodeAck(&ack, buf, sizeof(buf));
    assert(used == RF_DRAW_ACK_PAYLOAD_LEN);
    assert(RfDrawProtocol_DecodeAck(buf, used, &decoded_ack));
    assert(decoded_ack.phase == RF_DRAW_PHASE_START);
    assert(decoded_ack.value == 0U);

    RfDrawError_t error = {
        .phase = RF_DRAW_PHASE_COMMIT,
        .reason = RF_DRAW_ERROR_REASON_RENDER,
    };
    RfDrawError_t decoded_error;

    assert(RfDrawProtocol_EncodeError(&error, buf, RF_DRAW_ERROR_PAYLOAD_LEN - 1U) == 0U);
    used = RfDrawProtocol_EncodeError(&error, buf, sizeof(buf));
    assert(used == RF_DRAW_ERROR_PAYLOAD_LEN);
    assert(RfDrawProtocol_DecodeError(buf, used, &decoded_error));
    assert(decoded_error.phase == RF_DRAW_PHASE_COMMIT);
    assert(decoded_error.reason == RF_DRAW_ERROR_REASON_RENDER);

    return 0;
}
