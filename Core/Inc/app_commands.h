#ifndef APP_COMMANDS_H
#define APP_COMMANDS_H

#include "cc1101_radio.h"
#include "usb_protocol.h"

#include <stdbool.h>
#include <stdint.h>

void OpenPeriph_SendUsbAck(uint8_t packet_id);
void OpenPeriph_SendUsbNack(uint8_t packet_id, uint8_t reason);
void OpenPeriph_SendUsbPacket(PacketType_t type, const uint8_t *payload, uint16_t len);
uint16_t OpenPeriph_GetUsbRxAvailable(void);
void OpenPeriph_ResetSystem(void);
bool OpenPeriph_RenderLocalHello(void);

static inline bool AppCommands_HandleLocalCommand(const Packet_t *pkt)
{
    uint8_t status_payload[10];

    if ((pkt == NULL) || (pkt->payload_len < 1U)) {
        OpenPeriph_SendUsbNack(pkt != NULL ? pkt->id : 0U, 0x03U);
        return true;
    }

    switch ((CommandID_t)pkt->payload[0]) {
    case CMD_PING:
        OpenPeriph_SendUsbAck(pkt->id);
        return true;

    case CMD_RESET:
        OpenPeriph_SendUsbAck(pkt->id);
        OpenPeriph_ResetSystem();
        return true;

    case CMD_GET_STATUS:
        status_payload[7] = 0U;
        {
            uint8_t marc_state = Cc1101Radio_GetMarcState();
            if (marc_state == CC1101_RADIO_STATE_RXFIFO_OVERFLOW ||
                marc_state == CC1101_RADIO_STATE_TXFIFO_UNDERFLOW) {
                if (Cc1101Radio_RecoverRx()) {
                    status_payload[7] = 1U;
                    marc_state = Cc1101Radio_GetMarcState();
                } else {
                    status_payload[7] = 2U;
                }
            }
            status_payload[2] = marc_state;
        }
        status_payload[0] = 1U;
        status_payload[1] = 0U;
        status_payload[3] = (uint8_t)(OpenPeriph_GetUsbRxAvailable() & 0xFFU);
        status_payload[4] = (uint8_t)(OpenPeriph_GetUsbRxAvailable() >> 8);
        status_payload[5] = 0U;
        status_payload[6] = 0U;
        {
            uint8_t partnum = 0xFFU;
            uint8_t version = 0xFFU;

            if (Cc1101Radio_ReadChipInfo(&partnum, &version)) {
                status_payload[8] = partnum;
                status_payload[9] = version;
            } else {
                status_payload[8] = 0xFFU;
                status_payload[9] = 0xFFU;
            }
        }
        OpenPeriph_SendUsbPacket(PKT_TYPE_STATUS, status_payload, sizeof(status_payload));
        return true;

    case CMD_LOCAL_HELLO:
        if (OpenPeriph_RenderLocalHello()) {
            OpenPeriph_SendUsbAck(pkt->id);
        } else {
            OpenPeriph_SendUsbNack(pkt->id, 0x05U);
        }
        return true;

    case CMD_SET_RF_CHANNEL:
    case CMD_SET_RF_POWER:
    case CMD_SET_RF_ADDR:
        if (pkt->payload_len >= 2U) {
            OpenPeriph_SendUsbAck(pkt->id);
        } else {
            OpenPeriph_SendUsbNack(pkt->id, 0x03U);
        }
        return true;

    default:
        return false;
    }
}

#endif /* APP_COMMANDS_H */
