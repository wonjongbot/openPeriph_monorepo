#include "rf_link.h"

#include "cc1101_radio.h"
#include "openperiph_config.h"

extern uint32_t HAL_GetTick(void);

static void RfLink_ResetStats(RfLinkExchangeStats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    stats->attempts_used = 0U;
    stats->retries_used = 0U;
    stats->elapsed_ms = 0U;
    stats->remote_phase = 0U;
    stats->remote_reason = 0U;
}

static void RfLink_FinalizeStats(RfLinkExchangeStats_t *stats,
                                 uint32_t start_tick,
                                 uint8_t attempts_used)
{
    if (stats == NULL) {
        return;
    }

    stats->attempts_used = attempts_used;
    stats->retries_used = (attempts_used > 0U) ? (uint8_t)(attempts_used - 1U) : 0U;
    stats->elapsed_ms = (uint16_t)(HAL_GetTick() - start_tick);
}

bool RfLink_Init(void)
{
    return Cc1101Radio_Init();
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

    return true;
}

bool RfLink_IsForLocalNode(const RfFrame_t *frame)
{
    if (frame == NULL) {
        return false;
    }

    return frame->dst_addr == OPENPERIPH_NODE_ADDR;
}

RfLinkPingResult_t RfLink_SendPingAndWaitForPong(uint8_t dst_addr,
                                                 uint8_t seq,
                                                 RfLinkExchangeStats_t *out_stats)
{
    RfFrame_t ping_frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_PING,
        .dst_addr = dst_addr,
        .src_addr = OPENPERIPH_NODE_ADDR,
        .seq = seq,
        .payload_len = 0U,
    };
    const uint32_t absolute_start = HAL_GetTick();
    uint8_t attempts_used = 0U;
    RfLinkPingResult_t result = RF_LINK_PING_RESULT_TIMEOUT;

    RfLink_ResetStats(out_stats);

    for (uint8_t attempt = 0U; attempt < RF_LINK_MAX_RETRIES; ++attempt) {
        RfFrame_t rx_frame;
        uint32_t attempt_start;

        if ((HAL_GetTick() - absolute_start) >= RF_LINK_PING_TOTAL_TIMEOUT_MS) {
            break;
        }

        if (!RfLink_SendFrame(&ping_frame)) {
            RfLink_FinalizeStats(out_stats, absolute_start, attempts_used);
            return RF_LINK_PING_RESULT_SEND_FAIL;
        }

        ++attempts_used;
        if ((HAL_GetTick() - absolute_start) >= RF_LINK_PING_TOTAL_TIMEOUT_MS) {
            break;
        }
        attempt_start = HAL_GetTick();
        while (((HAL_GetTick() - attempt_start) < RF_LINK_ATTEMPT_TIMEOUT_MS) &&
               ((HAL_GetTick() - absolute_start) < RF_LINK_PING_TOTAL_TIMEOUT_MS)) {
            if (!RfLink_TryReceiveFrame(&rx_frame)) {
                continue;
            }
            if ((rx_frame.msg_type == RF_MSG_PONG) &&
                (rx_frame.src_addr == dst_addr) &&
                (rx_frame.dst_addr == OPENPERIPH_NODE_ADDR) &&
                (rx_frame.seq == seq) &&
                (rx_frame.payload_len == 0U)) {
                RfLink_FinalizeStats(out_stats, absolute_start, (uint8_t)(attempt + 1U));
                return RF_LINK_PING_RESULT_OK;
            }
        }

        if ((HAL_GetTick() - absolute_start) >= RF_LINK_PING_TOTAL_TIMEOUT_MS) {
            break;
        }

        result = RF_LINK_PING_RESULT_TIMEOUT;
    }

    RfLink_FinalizeStats(out_stats, absolute_start, attempts_used);
    return result;
}
