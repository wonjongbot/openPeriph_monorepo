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
static bool g_local_hello_called;
static bool g_local_hello_result;

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

static void ResetCaptures(void)
{
    g_last_type = (PacketType_t)0;
    g_last_payload_len = 0U;
    memset(g_last_payload, 0, sizeof(g_last_payload));
    g_last_ack_id = 0U;
    g_last_nack_id = 0U;
    g_last_nack_reason = 0U;
    g_local_hello_called = false;
    g_local_hello_result = true;
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
    g_radio_state = 0x16U;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_type == PKT_TYPE_STATUS);
    assert(g_last_payload_len == 8U);
    assert(g_last_payload[0] == 1U);
    assert(g_last_payload[1] == 0U);
    assert(g_last_payload[2] == 0x16U);
    assert(g_last_payload[3] == 0x34U);
    assert(g_last_payload[4] == 0x12U);

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

    return 0;
}
