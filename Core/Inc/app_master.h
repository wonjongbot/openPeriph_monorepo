#ifndef APP_MASTER_H
#define APP_MASTER_H

#include "app_commands.h"
#include "app_protocol.h"
#include "cc1101_radio.h"
#include "openperiph_config.h"
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

static inline void AppMaster_Init(void)
{
}

static inline bool AppMaster_SendDrawText(const Packet_t *pkt)
{
    AppDrawTextCommand_t cmd;
    RfFrame_t frame = {0};

    if ((pkt == NULL) || !AppProtocol_DecodeDrawText(pkt->payload, pkt->payload_len, &cmd)) {
        return false;
    }

    frame.version = RF_FRAME_VERSION;
    frame.msg_type = RF_MSG_DRAW_TEXT;
    frame.dst_addr = cmd.dst_addr;
    frame.src_addr = OPENPERIPH_NODE_ADDR;
    frame.seq = pkt->id;
    frame.payload_len = (uint8_t)pkt->payload_len;
    if (frame.payload_len > RF_FRAME_MAX_PAYLOAD) {
        return false;
    }

    for (uint8_t i = 0; i < frame.payload_len; ++i) {
        frame.payload[i] = pkt->payload[i];
    }

    return RfLink_SendFrame(&frame);
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

    result = RfLink_SendPingAndWaitForPong(dst_addr, pkt->id);
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
    case PKT_TYPE_DRAW_TEXT:
        if (AppMaster_SendDrawText(pkt)) {
            OpenPeriph_SendUsbAck(pkt->id);
        } else {
            OpenPeriph_SendUsbNack(pkt->id, 0x05U);
        }
        break;

    case PKT_TYPE_COMMAND:
        AppMaster_HandleCommand(pkt);
        break;

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
