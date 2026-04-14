#include "rf_frame.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    const size_t header_len = 6U;
    uint8_t encoded[64];
    uint8_t start_payload[] = {0x0CU, 0x00U, 0x22U, 0x00U, 0x02U, 0x01U, 0x05U};
    uint8_t chunk_payload[] = {0x01U, 0x03U, 'a', 'b', 'c'};
    RfFrame_t frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_START,
        .dst_addr = 0x22,
        .src_addr = 0x01,
        .seq = 0x05,
        .payload_len = sizeof(start_payload),
    };
    memcpy(frame.payload, start_payload, sizeof(start_payload));

    size_t used = RfFrame_Encode(&frame, encoded, sizeof(encoded));
    assert(used == header_len + sizeof(start_payload));

    RfFrame_t decoded;
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.version == RF_FRAME_VERSION);
    assert(decoded.msg_type == RF_MSG_DRAW_START);
    assert(decoded.dst_addr == 0x22);
    assert(decoded.src_addr == 0x01);
    assert(decoded.seq == 0x05);
    assert(decoded.payload_len == sizeof(start_payload));
    assert(memcmp(decoded.payload, start_payload, sizeof(start_payload)) == 0);

    RfFrame_t oversized_frame = frame;
    oversized_frame.payload_len = RF_FRAME_MAX_PAYLOAD + 1U;
    assert(RfFrame_Encode(&oversized_frame, encoded, sizeof(encoded)) == 0U);

    assert(RfFrame_Encode(&frame, encoded, header_len + sizeof(start_payload) - 1U) == 0U);

    RfFrame_t chunk_frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_CHUNK,
        .dst_addr = 0x22U,
        .src_addr = 0x01U,
        .seq = 0x06U,
        .payload_len = sizeof(chunk_payload),
    };
    memcpy(chunk_frame.payload, chunk_payload, sizeof(chunk_payload));

    used = RfFrame_Encode(&chunk_frame, encoded, sizeof(encoded));
    assert(used == header_len + sizeof(chunk_payload));

    memset(&decoded, 0, sizeof(decoded));
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.msg_type == RF_MSG_DRAW_CHUNK);
    assert(decoded.payload_len == sizeof(chunk_payload));
    assert(memcmp(decoded.payload, chunk_payload, sizeof(chunk_payload)) == 0);

    assert(!RfFrame_Decode(encoded, used - 1U, &decoded));
    assert(!RfFrame_Decode(encoded, used + 1U, &decoded));

    uint8_t tampered[64];
    memcpy(tampered, encoded, used);
    tampered[5] = (uint8_t)(tampered[5] + 1U);
    assert(!RfFrame_Decode(tampered, used, &decoded));

    RfFrame_t ack_frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_ACK,
        .dst_addr = 0x22U,
        .src_addr = 0x01U,
        .seq = 0x07U,
        .payload_len = 2U,
    };
    ack_frame.payload[0] = 0x01U;
    ack_frame.payload[1] = 0x00U;

    used = RfFrame_Encode(&ack_frame, encoded, sizeof(encoded));
    assert(used == header_len + 2U);
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.msg_type == RF_MSG_DRAW_ACK);
    assert(decoded.payload_len == 2U);

    RfFrame_t error_frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_ERROR,
        .dst_addr = 0x22U,
        .src_addr = 0x01U,
        .seq = 0x08U,
        .payload_len = 2U,
    };
    error_frame.payload[0] = 0x03U;
    error_frame.payload[1] = 0x04U;

    used = RfFrame_Encode(&error_frame, encoded, sizeof(encoded));
    assert(used == header_len + 2U);
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.msg_type == RF_MSG_DRAW_ERROR);
    assert(decoded.payload_len == 2U);

    RfFrame_t commit_frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_COMMIT,
        .dst_addr = 0x22U,
        .src_addr = 0x01U,
        .seq = 0x09U,
        .payload_len = 0U,
    };

    used = RfFrame_Encode(&commit_frame, encoded, sizeof(encoded));
    assert(used == header_len);
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.msg_type == RF_MSG_DRAW_COMMIT);
    assert(decoded.payload_len == 0U);

    RfFrame_t ping_frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_PING,
        .dst_addr = 0x22U,
        .src_addr = 0x01U,
        .seq = 0x0AU,
        .payload_len = 0U,
    };

    used = RfFrame_Encode(&ping_frame, encoded, sizeof(encoded));
    assert(used == header_len);
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.msg_type == RF_MSG_PING);
    assert(decoded.payload_len == 0U);

    RfFrame_t pong_frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_PONG,
        .dst_addr = 0x22U,
        .src_addr = 0x01U,
        .seq = 0x0BU,
        .payload_len = 0U,
    };

    used = RfFrame_Encode(&pong_frame, encoded, sizeof(encoded));
    assert(used == header_len);
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.msg_type == RF_MSG_PONG);
    assert(decoded.payload_len == 0U);

    RfFrame_t max_payload_frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_CHUNK,
        .dst_addr = 0x22U,
        .src_addr = 0x01U,
        .seq = 0x0CU,
        .payload_len = RF_FRAME_MAX_PAYLOAD,
    };
    memset(max_payload_frame.payload, 0xA5, RF_FRAME_MAX_PAYLOAD);

    used = RfFrame_Encode(&max_payload_frame, encoded, sizeof(encoded));
    assert(used == header_len + RF_FRAME_MAX_PAYLOAD);
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.payload_len == RF_FRAME_MAX_PAYLOAD);
    assert(memcmp(decoded.payload, max_payload_frame.payload, RF_FRAME_MAX_PAYLOAD) == 0);

    return 0;
}
