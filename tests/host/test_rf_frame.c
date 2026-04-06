#include "rf_frame.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t encoded[64];
    uint8_t payload[] = {'H', 'i'};
    RfFrame_t frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_TEXT,
        .dst_addr = 0x22,
        .src_addr = 0x01,
        .seq = 0x05,
        .payload_len = sizeof(payload),
    };
    memcpy(frame.payload, payload, sizeof(payload));

    size_t used = RfFrame_Encode(&frame, encoded, sizeof(encoded));
    assert(used == 8U);

    RfFrame_t decoded;
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.version == RF_FRAME_VERSION);
    assert(decoded.msg_type == RF_MSG_DRAW_TEXT);
    assert(decoded.dst_addr == 0x22);
    assert(decoded.src_addr == 0x01);
    assert(decoded.seq == 0x05);
    assert(decoded.payload_len == 2U);
    assert(decoded.payload[0] == 'H');
    assert(decoded.payload[1] == 'i');

    return 0;
}
