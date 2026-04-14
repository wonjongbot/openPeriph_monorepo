#include "app_master.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static PacketType_t g_last_type;
static uint8_t g_last_payload[32];
static uint16_t g_last_payload_len;
static uint8_t g_last_ack_id;
static uint8_t g_last_nack_id;
static uint8_t g_last_nack_reason;
static uint16_t g_usb_rx_available;
static uint8_t g_radio_state;
static uint8_t g_chip_partnum;
static uint8_t g_chip_version;
static bool g_chip_info_result;
static bool g_local_hello_called;
static bool g_local_hello_result;
static RfLinkPingResult_t g_ping_result;
static uint8_t g_last_ping_dst;
static uint8_t g_last_ping_seq;

void OpenPeriph_SendUsbAck(uint8_t packet_id)
{
    g_last_ack_id = packet_id;
}

void OpenPeriph_SendUsbNack(uint8_t packet_id, uint8_t reason)
{
    g_last_nack_id = packet_id;
    g_last_nack_reason = reason;
}

void OpenPeriph_SendUsbPacket(PacketType_t type, const uint8_t *payload, uint16_t len)
{
    g_last_type = type;
    g_last_payload_len = len;
    if ((payload != NULL) && (len > 0U)) {
        memcpy(g_last_payload, payload, len);
    }
}

uint16_t OpenPeriph_GetUsbRxAvailable(void)
{
    return g_usb_rx_available;
}

void OpenPeriph_ResetSystem(void)
{
}

uint8_t Cc1101Radio_GetMarcState(void)
{
    return g_radio_state;
}

bool Cc1101Radio_ReadChipInfo(uint8_t *partnum, uint8_t *version)
{
    if ((partnum == NULL) || (version == NULL)) {
        return false;
    }
    if (!g_chip_info_result) {
        return false;
    }

    *partnum = g_chip_partnum;
    *version = g_chip_version;
    return true;
}

bool Cc1101Radio_RecoverRx(void)
{
    g_radio_state = CC1101_RADIO_STATE_RX;
    return true;
}

bool OpenPeriph_RenderLocalHello(void)
{
    g_local_hello_called = true;
    return g_local_hello_result;
}

bool AppProtocol_DecodeDrawText(const uint8_t *buf, size_t len, AppDrawTextCommand_t *out_cmd)
{
    (void)buf;
    (void)len;
    (void)out_cmd;
    return false;
}

bool RfLink_SendFrame(const RfFrame_t *frame)
{
    (void)frame;
    return false;
}

RfLinkPingResult_t RfLink_SendPingAndWaitForPong(uint8_t dst_addr,
                                                 uint8_t seq,
                                                 RfLinkExchangeStats_t *out_stats)
{
    (void)out_stats;
    g_last_ping_dst = dst_addr;
    g_last_ping_seq = seq;
    return g_ping_result;
}

static void ResetCaptures(void)
{
    g_last_type = (PacketType_t)0;
    g_last_payload_len = 0U;
    memset(g_last_payload, 0, sizeof(g_last_payload));
    g_last_ack_id = 0U;
    g_last_nack_id = 0U;
    g_last_nack_reason = 0U;
    g_chip_partnum = 0x12U;
    g_chip_version = 0x34U;
    g_chip_info_result = true;
    g_local_hello_called = false;
    g_local_hello_result = true;
    g_ping_result = RF_LINK_PING_RESULT_OK;
    g_last_ping_dst = 0U;
    g_last_ping_seq = 0U;
}

int main(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_COMMAND,
        .id = 0x33U,
        .payload_len = 1U,
        .payload = { CMD_GET_STATUS },
    };

    ResetCaptures();
    g_usb_rx_available = 0x1234U;
    g_radio_state = CC1101_RADIO_STATE_RX;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_type == PKT_TYPE_STATUS);
    assert(g_last_payload_len == 10U);
    assert(g_last_payload[0] == 1U);
    assert(g_last_payload[1] == 0U);
    assert(g_last_payload[2] == CC1101_RADIO_STATE_RX);
    assert(g_last_payload[3] == 0x34U);
    assert(g_last_payload[4] == 0x12U);
    assert(g_last_payload[8] == 0x12U);
    assert(g_last_payload[9] == 0x34U);

    ResetCaptures();
    g_chip_info_result = false;
    g_usb_rx_available = 0x1234U;
    g_radio_state = CC1101_RADIO_STATE_RX;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_type == PKT_TYPE_STATUS);
    assert(g_last_payload_len == 10U);
    assert(g_last_payload[8] == 0xFFU);
    assert(g_last_payload[9] == 0xFFU);

    ResetCaptures();
    pkt.id = 0x44U;
    pkt.payload[0] = CMD_LOCAL_HELLO;
    g_local_hello_result = true;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_local_hello_called);
    assert(g_last_ack_id == 0x44U);
    assert(g_last_nack_id == 0U);

    ResetCaptures();
    pkt.id = 0x45U;
    pkt.payload[0] = CMD_LOCAL_HELLO;
    g_local_hello_result = false;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_local_hello_called);
    assert(g_last_nack_id == 0x45U);
    assert(g_last_nack_reason == 0x05U);

    ResetCaptures();
    pkt.id = 0x46U;
    pkt.payload_len = 2U;
    pkt.payload[0] = CMD_RF_PING;
    pkt.payload[1] = 0x22U;
    g_ping_result = RF_LINK_PING_RESULT_OK;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_ack_id == 0x46U);
    assert(g_last_nack_id == 0U);
    assert(g_last_ping_dst == 0x22U);
    assert(g_last_ping_seq == 0x46U);

    ResetCaptures();
    pkt.id = 0x47U;
    pkt.payload_len = 2U;
    pkt.payload[0] = CMD_RF_PING;
    pkt.payload[1] = 0x22U;
    g_ping_result = RF_LINK_PING_RESULT_TIMEOUT;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_ack_id == 0U);
    assert(g_last_nack_id == 0x47U);
    assert(g_last_nack_reason == 0x06U);

    ResetCaptures();
    pkt.id = 0x48U;
    pkt.payload_len = 3U;
    pkt.payload[0] = CMD_RF_PING;
    pkt.payload[1] = 0x22U;
    pkt.payload[2] = 0x99U;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_ack_id == 0U);
    assert(g_last_nack_id == 0x48U);
    assert(g_last_nack_reason == 0x03U);

    ResetCaptures();
    pkt.id = 0x49U;
    pkt.payload_len = 2U;
    pkt.payload[0] = CMD_RF_PING;
    pkt.payload[1] = 0x22U;
    g_ping_result = RF_LINK_PING_RESULT_SEND_FAIL;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_ack_id == 0U);
    assert(g_last_nack_id == 0x49U);
    assert(g_last_nack_reason == 0x05U);

    return 0;
}
