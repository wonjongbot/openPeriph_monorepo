#ifndef APP_MASTER_H
#define APP_MASTER_H

#include "app_commands.h"
#include "app_protocol.h"
#include "cc1101_radio.h"
#include "openperiph_config.h"
#include "rf_draw_protocol.h"
#include "rf_link.h"
#include "usb_protocol.h"

#include <stddef.h>
#include <stdint.h>

void OpenPeriph_SendUsbAck(uint8_t packet_id);
void OpenPeriph_SendUsbNack(uint8_t packet_id, uint8_t reason);
void OpenPeriph_SendUsbPacket(PacketType_t type, const uint8_t *payload, uint16_t len);
uint16_t OpenPeriph_GetUsbRxAvailable(void);
void OpenPeriph_ResetSystem(void);
bool OpenPeriph_RenderLocalHello(void);
extern uint32_t HAL_GetTick(void);

static inline void AppMaster_Init(void)
{
}

static inline bool AppMaster_WaitForDrawResponse(uint8_t dst_addr,
                                                 uint8_t seq,
                                                 uint8_t expected_phase,
                                                 uint8_t expected_value,
                                                 uint32_t absolute_start_tick,
                                                 uint32_t attempt_start_tick,
                                                 uint8_t *out_nack_reason)
{
    RfFrame_t response;

    if (out_nack_reason != NULL) {
        *out_nack_reason = 0x05U;
    }

    while (((HAL_GetTick() - attempt_start_tick) < RF_LINK_ATTEMPT_TIMEOUT_MS) &&
           ((HAL_GetTick() - absolute_start_tick) < RF_LINK_DRAW_TOTAL_TIMEOUT_MS)) {
        RfDrawAck_t ack;
        RfDrawError_t error;

        if (!RfLink_TryReceiveFrame(&response)) {
            continue;
        }
        if ((response.version != RF_FRAME_VERSION) ||
            (response.src_addr != dst_addr) ||
            (response.dst_addr != OPENPERIPH_NODE_ADDR) ||
            (response.seq != seq)) {
            continue;
        }

        if (response.msg_type == RF_MSG_DRAW_ACK) {
            if (!RfDrawProtocol_DecodeAck(response.payload, response.payload_len, &ack)) {
                continue;
            }
            if ((ack.phase == expected_phase) && (ack.value == expected_value)) {
                return true;
            }
            continue;
        }

        if (response.msg_type == RF_MSG_DRAW_ERROR) {
            if ((out_nack_reason != NULL) &&
                RfDrawProtocol_DecodeError(response.payload, response.payload_len, &error)) {
                *out_nack_reason = error.reason;
                return false;
            }
            continue;
        }
    }

    return false;
}

static inline bool AppMaster_ExchangeDrawFrame(const RfFrame_t *frame,
                                               uint8_t expected_phase,
                                               uint8_t expected_value,
                                               uint8_t *out_nack_reason)
{
    const uint32_t absolute_start_tick = HAL_GetTick();

    if (frame == NULL) {
        return false;
    }

    if (out_nack_reason != NULL) {
        *out_nack_reason = 0x05U;
    }

    for (uint8_t attempt = 0U;
         (attempt < RF_LINK_MAX_RETRIES) &&
         ((HAL_GetTick() - absolute_start_tick) < RF_LINK_DRAW_TOTAL_TIMEOUT_MS);
         ++attempt) {
        uint32_t attempt_start_tick;

        if (!RfLink_SendFrame(frame)) {
            (void)Cc1101Radio_RecoverRx();
            continue;
        }

        attempt_start_tick = HAL_GetTick();
        if (AppMaster_WaitForDrawResponse(frame->dst_addr,
                                          frame->seq,
                                          expected_phase,
                                          expected_value,
                                          absolute_start_tick,
                                          attempt_start_tick,
                                          out_nack_reason)) {
            return true;
        }

        if ((out_nack_reason != NULL) && (*out_nack_reason != 0x05U)) {
            return false;
        }

        (void)Cc1101Radio_RecoverRx();
    }

    return false;
}

static inline bool AppMaster_ExchangeDrawBegin(const AppDrawBeginCommand_t *cmd,
                                               uint8_t seq,
                                               uint8_t *out_nack_reason)
{
    RfDrawBegin_t begin;
    RfFrame_t frame = {0};

    if (cmd == NULL) {
        return false;
    }

    begin.session_id = cmd->session_id;
    begin.flags = cmd->flags;

    frame.version = RF_FRAME_VERSION;
    frame.msg_type = RF_MSG_DRAW_BEGIN;
    frame.dst_addr = cmd->dst_addr;
    frame.src_addr = OPENPERIPH_NODE_ADDR;
    frame.seq = seq;
    frame.payload_len = (uint8_t)RfDrawProtocol_EncodeBegin(&begin, frame.payload, sizeof(frame.payload));
    if (frame.payload_len != RF_DRAW_BEGIN_PAYLOAD_LEN) {
        return false;
    }

    return AppMaster_ExchangeDrawFrame(&frame, RF_DRAW_PHASE_BEGIN, 0U, out_nack_reason);
}

static inline bool AppMaster_ExchangeDrawText(const AppDrawTextCommand_t *cmd,
                                              uint8_t seq,
                                              uint8_t *out_nack_reason)
{
    RfDrawText_t text = {0};
    RfFrame_t frame = {0};

    if ((cmd == NULL) || (cmd->text_len == 0U) || (cmd->text_len > RF_DRAW_TEXT_MAX_LEN)) {
        return false;
    }

    text.session_id = cmd->session_id;
    text.op_index = cmd->op_index;
    text.x = cmd->x;
    text.y = cmd->y;
    text.font_id = cmd->font_id;
    text.text_len = cmd->text_len;
    for (uint8_t i = 0U; i < cmd->text_len; ++i) {
        text.text[i] = cmd->text[i];
    }

    frame.version = RF_FRAME_VERSION;
    frame.msg_type = RF_MSG_DRAW_TEXT;
    frame.dst_addr = cmd->dst_addr;
    frame.src_addr = OPENPERIPH_NODE_ADDR;
    frame.seq = seq;
    frame.payload_len = (uint8_t)RfDrawProtocol_EncodeText(&text, frame.payload, sizeof(frame.payload));
    if (frame.payload_len != (uint8_t)(RF_DRAW_TEXT_FIXED_LEN + cmd->text_len)) {
        return false;
    }

    return AppMaster_ExchangeDrawFrame(&frame, RF_DRAW_PHASE_TEXT, cmd->op_index, out_nack_reason);
}

static inline bool AppMaster_ExchangeDrawCommit(const AppDrawCommitCommand_t *cmd,
                                                uint8_t seq,
                                                uint8_t *out_nack_reason)
{
    RfDrawCommit_t commit;
    RfFrame_t frame = {0};

    if (cmd == NULL) {
        return false;
    }

    commit.session_id = cmd->session_id;
    frame.version = RF_FRAME_VERSION;
    frame.msg_type = RF_MSG_DRAW_COMMIT;
    frame.dst_addr = cmd->dst_addr;
    frame.src_addr = OPENPERIPH_NODE_ADDR;
    frame.seq = seq;
    frame.payload_len = (uint8_t)RfDrawProtocol_EncodeCommit(&commit, frame.payload, sizeof(frame.payload));
    if (frame.payload_len != RF_DRAW_COMMIT_PAYLOAD_LEN) {
        return false;
    }

    return AppMaster_ExchangeDrawFrame(&frame, RF_DRAW_PHASE_COMMIT, 0U, out_nack_reason);
}

static inline bool AppMaster_SendDisplayFlush(const AppDisplayFlushCommand_t *cmd,
                                               uint8_t seq,
                                               uint8_t *out_nack_reason)
{
    RfFrame_t frame = {0};

    if (cmd == NULL) {
        return false;
    }

    frame.version = RF_FRAME_VERSION;
    frame.msg_type = RF_MSG_DISPLAY_FLUSH;
    frame.dst_addr = cmd->dst_addr;
    frame.src_addr = OPENPERIPH_NODE_ADDR;
    frame.seq = seq;
    frame.payload[0] = cmd->session_id;
    frame.payload[1] = cmd->full_refresh ? 1U : 0U;
    frame.payload_len = 2U;

    return AppMaster_ExchangeDrawFrame(&frame, RF_DRAW_PHASE_FLUSH, 0U, out_nack_reason);
}

static inline bool AppMaster_SendDrawBegin(const Packet_t *pkt, uint8_t *out_nack_reason)
{
    AppDrawBeginCommand_t cmd;

    if (out_nack_reason != NULL) {
        *out_nack_reason = 0x05U;
    }

    if ((pkt == NULL) || !AppProtocol_DecodeDrawBegin(pkt->payload, pkt->payload_len, &cmd)) {
        return false;
    }
    return AppMaster_ExchangeDrawBegin(&cmd, pkt->id, out_nack_reason);
}

static inline bool AppMaster_SendDrawText(const Packet_t *pkt, uint8_t *out_nack_reason)
{
    AppDrawTextCommand_t cmd;

    if (out_nack_reason != NULL) {
        *out_nack_reason = 0x05U;
    }

    if ((pkt == NULL) || !AppProtocol_DecodeDrawText(pkt->payload, pkt->payload_len, &cmd)) {
        return false;
    }

    return AppMaster_ExchangeDrawText(&cmd, pkt->id, out_nack_reason);
}

static inline bool AppMaster_SendDrawCommit(const Packet_t *pkt, uint8_t *out_nack_reason)
{
    AppDrawCommitCommand_t cmd;

    if (out_nack_reason != NULL) {
        *out_nack_reason = 0x05U;
    }

    if ((pkt == NULL) || !AppProtocol_DecodeDrawCommit(pkt->payload, pkt->payload_len, &cmd)) {
        return false;
    }

    return AppMaster_ExchangeDrawCommit(&cmd, pkt->id, out_nack_reason);
}

static inline bool AppMaster_HandleRfPingCommand(const Packet_t *pkt)
{
    RfLinkPingResult_t result;
    uint8_t dst_addr;

    if ((pkt == NULL) || (pkt->payload_len != 2U)) {
        OpenPeriph_SendUsbNack(pkt != NULL ? pkt->id : 0U, 0x03U);
        return true;
    }

    dst_addr = pkt->payload[1];
    if (dst_addr == OPENPERIPH_NODE_ADDR) {
        OpenPeriph_SendUsbNack(pkt->id, 0x07U);
        return true;
    }

    result = RfLink_SendPingAndWaitForPong(dst_addr, pkt->id, NULL);
    switch (result) {
    case RF_LINK_PING_RESULT_OK:
        OpenPeriph_SendUsbAck(pkt->id);
        break;

    case RF_LINK_PING_RESULT_TIMEOUT:
        OpenPeriph_SendUsbNack(pkt->id, 0x06U);
        break;

    case RF_LINK_PING_RESULT_SEND_FAIL:
    default:
        OpenPeriph_SendUsbNack(pkt->id, 0x05U);
        break;
    }

    return true;
}

static inline void AppMaster_HandleCommand(const Packet_t *pkt)
{
    if ((pkt != NULL) && (pkt->payload_len >= 1U) && (pkt->payload[0] == CMD_RF_PING)) {
        (void)AppMaster_HandleRfPingCommand(pkt);
        return;
    }

    if (!AppCommands_HandleLocalCommand(pkt)) {
        OpenPeriph_SendUsbNack(pkt->id, 0x04U);
    }
}

static inline void AppMaster_HandleUsbPacket(const Packet_t *pkt)
{
    if (pkt == NULL) {
        return;
    }

    switch (pkt->type) {
    case PKT_TYPE_DRAW_BEGIN:
    {
        uint8_t nack_reason = 0x05U;

        if (AppMaster_SendDrawBegin(pkt, &nack_reason)) {
            OpenPeriph_SendUsbAck(pkt->id);
        } else {
            OpenPeriph_SendUsbNack(pkt->id, nack_reason);
        }
        break;
    }

    case PKT_TYPE_DRAW_TEXT:
    {
        uint8_t nack_reason = 0x05U;

        if (AppMaster_SendDrawText(pkt, &nack_reason)) {
            OpenPeriph_SendUsbAck(pkt->id);
        } else {
            OpenPeriph_SendUsbNack(pkt->id, nack_reason);
        }
        break;
    }

    case PKT_TYPE_DRAW_COMMIT:
    {
        uint8_t nack_reason = 0x05U;

        if (AppMaster_SendDrawCommit(pkt, &nack_reason)) {
            OpenPeriph_SendUsbAck(pkt->id);
        } else {
            OpenPeriph_SendUsbNack(pkt->id, nack_reason);
        }
        break;
    }

    case PKT_TYPE_COMMAND:
        AppMaster_HandleCommand(pkt);
        break;

    case PKT_TYPE_DISPLAY_FLUSH:
    {
        AppDisplayFlushCommand_t cmd;
        uint8_t nack_reason = 0x05U;

        if (!AppProtocol_DecodeDisplayFlush(pkt->payload, pkt->payload_len, &cmd)) {
            OpenPeriph_SendUsbNack(pkt->id, 0x03U);
            break;
        }
        if (AppMaster_SendDisplayFlush(&cmd, pkt->id, &nack_reason)) {
            OpenPeriph_SendUsbAck(pkt->id);
        } else {
            OpenPeriph_SendUsbNack(pkt->id, nack_reason);
        }
        break;
    }

    case PKT_TYPE_IMAGE_DATA:
    case PKT_TYPE_EMAIL_DATA:
    case PKT_TYPE_TEXT_DATA:
    case PKT_TYPE_FILE_START:
    case PKT_TYPE_FILE_END:
        OpenPeriph_SendUsbAck(pkt->id);
        break;

    default:
        OpenPeriph_SendUsbNack(pkt->id, 0x02U);
        break;
    }
}

#endif /* APP_MASTER_H */
