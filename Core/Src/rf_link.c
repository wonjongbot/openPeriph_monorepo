#include "rf_link.h"

#include "cc1101_radio.h"
#include "openperiph_config.h"

void RfLink_Init(void)
{
    (void)Cc1101Radio_Init();
}

bool RfLink_SendFrame(const RfFrame_t *frame)
{
    uint8_t encoded[CC1101_RADIO_MAX_PACKET_LEN];
    size_t encoded_len;

    if (frame == NULL) {
        return false;
    }

    encoded_len = RfFrame_Encode(frame, encoded, sizeof(encoded));
    if (encoded_len == 0U || encoded_len > UINT8_MAX) {
        return false;
    }

    return Cc1101Radio_Send(encoded, (uint8_t)encoded_len);
}

bool RfLink_TryReceiveFrame(RfFrame_t *out_frame)
{
    uint8_t encoded[CC1101_RADIO_MAX_PACKET_LEN];
    uint8_t encoded_len = (uint8_t)sizeof(encoded);

    if (out_frame == NULL) {
        return false;
    }
    if (!Cc1101Radio_Receive(encoded, &encoded_len)) {
        return false;
    }
    if (!RfFrame_Decode(encoded, encoded_len, out_frame)) {
        return false;
    }

    return RfLink_IsForLocalNode(out_frame);
}

bool RfLink_IsForLocalNode(const RfFrame_t *frame)
{
    if (frame == NULL) {
        return false;
    }

    return frame->dst_addr == OPENPERIPH_NODE_ADDR;
}
