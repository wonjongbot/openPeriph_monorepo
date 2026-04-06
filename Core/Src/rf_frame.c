#include "rf_frame.h"

#include <string.h>

#define RF_FRAME_HEADER_SIZE 6U

size_t RfFrame_Encode(const RfFrame_t *frame, uint8_t *out_buf, size_t out_capacity)
{
    if (frame == NULL || out_buf == NULL) {
        return 0U;
    }
    if (frame->version != RF_FRAME_VERSION) {
        return 0U;
    }
    if (frame->payload_len > RF_FRAME_MAX_PAYLOAD) {
        return 0U;
    }

    const size_t total_len = RF_FRAME_HEADER_SIZE + (size_t)frame->payload_len;
    if (out_capacity < total_len) {
        return 0U;
    }

    out_buf[0] = frame->version;
    out_buf[1] = frame->msg_type;
    out_buf[2] = frame->dst_addr;
    out_buf[3] = frame->src_addr;
    out_buf[4] = frame->seq;
    out_buf[5] = frame->payload_len;

    if (frame->payload_len > 0U) {
        memcpy(&out_buf[RF_FRAME_HEADER_SIZE], frame->payload, frame->payload_len);
    }

    return total_len;
}

bool RfFrame_Decode(const uint8_t *buf, size_t len, RfFrame_t *out_frame)
{
    if (buf == NULL || out_frame == NULL) {
        return false;
    }
    if (len < RF_FRAME_HEADER_SIZE) {
        return false;
    }

    const uint8_t version = buf[0];
    const uint8_t payload_len = buf[5];
    const size_t total_len = RF_FRAME_HEADER_SIZE + (size_t)payload_len;

    if (version != RF_FRAME_VERSION) {
        return false;
    }
    if (payload_len > RF_FRAME_MAX_PAYLOAD) {
        return false;
    }
    if (len != total_len) {
        return false;
    }

    out_frame->version = version;
    out_frame->msg_type = buf[1];
    out_frame->dst_addr = buf[2];
    out_frame->src_addr = buf[3];
    out_frame->seq = buf[4];
    out_frame->payload_len = payload_len;

    if (payload_len > 0U) {
        memcpy(out_frame->payload, &buf[RF_FRAME_HEADER_SIZE], payload_len);
    }

    return true;
}
