#include "rf_link.h"
#include "openperiph_config.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool g_send_result;
static uint32_t g_send_calls;
static uint32_t g_tick;
static uint8_t g_last_tx[64];
static uint8_t g_last_tx_len;

typedef struct {
    bool available;
    uint8_t data[64];
    uint8_t len;
} RxScriptEntry_t;

static RxScriptEntry_t g_rx_script[8];
static size_t g_rx_script_len;
static size_t g_rx_script_index;

bool Cc1101Radio_Init(void)
{
    return true;
}

bool Cc1101Radio_Send(const uint8_t *payload, uint8_t length)
{
    ++g_send_calls;
    g_last_tx_len = length;
    if ((payload != NULL) && (length > 0U)) {
        memcpy(g_last_tx, payload, length);
    }
    return g_send_result;
}

bool Cc1101Radio_Receive(uint8_t *payload, uint8_t *in_out_length)
{
    if ((payload == NULL) || (in_out_length == NULL)) {
        return false;
    }

    if (g_rx_script_index < g_rx_script_len) {
        RxScriptEntry_t *entry = &g_rx_script[g_rx_script_index++];
        if (entry->available) {
            memcpy(payload, entry->data, entry->len);
            *in_out_length = entry->len;
            return true;
        }
    }

    ++g_tick;
    return false;
}

uint32_t HAL_GetTick(void)
{
    return g_tick;
}

static void ResetFakes(void)
{
    g_send_result = true;
    g_send_calls = 0U;
    g_tick = 0U;
    g_last_tx_len = 0U;
    memset(g_last_tx, 0, sizeof(g_last_tx));
    memset(g_rx_script, 0, sizeof(g_rx_script));
    g_rx_script_len = 0U;
    g_rx_script_index = 0U;
}

static void ScriptRxFrame(uint8_t msg_type,
                          uint8_t dst_addr,
                          uint8_t src_addr,
                          uint8_t seq,
                          const uint8_t *payload,
                          uint8_t payload_len)
{
    RfFrame_t frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = msg_type,
        .dst_addr = dst_addr,
        .src_addr = src_addr,
        .seq = seq,
        .payload_len = payload_len,
    };
    size_t encoded_len;

    assert(g_rx_script_len < (sizeof(g_rx_script) / sizeof(g_rx_script[0])));
    if ((payload != NULL) && (payload_len > 0U)) {
        memcpy(frame.payload, payload, payload_len);
    }

    encoded_len = RfFrame_Encode(&frame,
                                 g_rx_script[g_rx_script_len].data,
                                 sizeof(g_rx_script[g_rx_script_len].data));
    assert(encoded_len > 0U);

    g_rx_script[g_rx_script_len].available = true;
    g_rx_script[g_rx_script_len].len = (uint8_t)encoded_len;
    ++g_rx_script_len;
}

int main(void)
{
    uint8_t bad_payload[] = {0xAAU};
    RfLinkPingResult_t result;

    ResetFakes();
    g_send_result = false;
    result = RfLink_SendPingAndWaitForPong(0x22U, 0x33U);
    assert(result == RF_LINK_PING_RESULT_SEND_FAIL);
    assert(g_send_calls == 1U);

    ResetFakes();
    ScriptRxFrame(RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x44U, 0x33U, NULL, 0U);
    ScriptRxFrame(RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x22U, 0x33U, NULL, 0U);
    result = RfLink_SendPingAndWaitForPong(0x22U, 0x33U);
    assert(result == RF_LINK_PING_RESULT_OK);
    assert(g_send_calls == 1U);
    assert(g_last_tx_len == 6U);

    ResetFakes();
    ScriptRxFrame(RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x22U, 0x33U, bad_payload, sizeof(bad_payload));
    result = RfLink_SendPingAndWaitForPong(0x22U, 0x33U);
    assert(result == RF_LINK_PING_RESULT_TIMEOUT);
    assert(g_send_calls == 3U);

    return 0;
}
