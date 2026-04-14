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
static uint32_t g_send_tick_advance;
static uint8_t g_last_tx[64];
static uint8_t g_last_tx_len;

typedef struct {
    bool available;
    uint32_t available_at_tick;
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
    g_tick += g_send_tick_advance;
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
        if (g_tick < entry->available_at_tick) {
            --g_rx_script_index;
            ++g_tick;
            return false;
        }
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
    g_send_tick_advance = 0U;
    g_last_tx_len = 0U;
    memset(g_last_tx, 0, sizeof(g_last_tx));
    memset(g_rx_script, 0, sizeof(g_rx_script));
    g_rx_script_len = 0U;
    g_rx_script_index = 0U;
}

static void ScriptRxFrameAt(uint32_t available_at_tick,
                            uint8_t msg_type,
                            uint8_t dst_addr,
                            uint8_t src_addr,
                            uint8_t seq,
                            const uint8_t *payload,
                            uint8_t payload_len);

static void ScriptRxRawAt(uint32_t available_at_tick, const uint8_t *buf, uint8_t len);

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
    g_rx_script[g_rx_script_len].available_at_tick = 0U;
    g_rx_script[g_rx_script_len].len = (uint8_t)encoded_len;
    ++g_rx_script_len;
}

static void ScriptRxFrameAt(uint32_t available_at_tick,
                            uint8_t msg_type,
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
    g_rx_script[g_rx_script_len].available_at_tick = available_at_tick;
    g_rx_script[g_rx_script_len].len = (uint8_t)encoded_len;
    ++g_rx_script_len;
}

static void ScriptRxRawAt(uint32_t available_at_tick, const uint8_t *buf, uint8_t len)
{
    assert(g_rx_script_len < (sizeof(g_rx_script) / sizeof(g_rx_script[0])));
    assert((buf != NULL) || (len == 0U));
    if ((buf != NULL) && (len > 0U)) {
        memcpy(g_rx_script[g_rx_script_len].data, buf, len);
    }

    g_rx_script[g_rx_script_len].available = true;
    g_rx_script[g_rx_script_len].available_at_tick = available_at_tick;
    g_rx_script[g_rx_script_len].len = len;
    ++g_rx_script_len;
}

int main(void)
{
    RfLinkExchangeStats_t stats;
    RfLinkPingResult_t result;

    ResetFakes();
    g_send_result = false;
    result = RfLink_SendPingAndWaitForPong(0x22U, 0x33U, &stats);
    assert(result == RF_LINK_PING_RESULT_SEND_FAIL);
    assert(g_send_calls == 1U);
    assert(g_rx_script_index == 0U);
    assert(stats.attempts_used == 0U);
    assert(stats.retries_used == 0U);
    assert(stats.elapsed_ms == 0U);
    assert(stats.remote_phase == 0U);
    assert(stats.remote_reason == 0U);

    ResetFakes();
    ScriptRxFrame(RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x44U, 0x33U, NULL, 0U);
    ScriptRxFrameAt(80U, RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x22U, 0x33U, NULL, 0U);
    result = RfLink_SendPingAndWaitForPong(0x22U, 0x33U, &stats);
    assert(result == RF_LINK_PING_RESULT_OK);
    assert(g_send_calls == 2U);
    assert(g_rx_script_index == 2U);
    assert(g_last_tx_len == 6U);
    assert(stats.attempts_used == 2U);
    assert(stats.retries_used == 1U);
    assert(stats.elapsed_ms >= RF_LINK_ATTEMPT_TIMEOUT_MS);
    assert(stats.remote_phase == 0U);
    assert(stats.remote_reason == 0U);

    ResetFakes();
    ScriptRxFrame(RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x22U, 0x34U, NULL, 0U);
    ScriptRxFrameAt(80U, RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x22U, 0x33U, NULL, 0U);
    result = RfLink_SendPingAndWaitForPong(0x22U, 0x33U, &stats);
    assert(result == RF_LINK_PING_RESULT_OK);
    assert(g_send_calls == 2U);
    assert(g_rx_script_index == 2U);
    assert(stats.attempts_used == 2U);
    assert(stats.retries_used == 1U);
    assert(stats.elapsed_ms >= RF_LINK_ATTEMPT_TIMEOUT_MS);

    ResetFakes();
    {
        uint8_t malformed_pong[] = { 0x00U, RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x22U, 0x33U, 0x00U };
        ScriptRxRawAt(0U, malformed_pong, (uint8_t)sizeof(malformed_pong));
    }
    ScriptRxFrameAt(80U, RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x22U, 0x33U, NULL, 0U);
    result = RfLink_SendPingAndWaitForPong(0x22U, 0x33U, &stats);
    assert(result == RF_LINK_PING_RESULT_OK);
    assert(g_send_calls == 2U);
    assert(g_rx_script_index == 2U);
    assert(stats.attempts_used == 2U);
    assert(stats.retries_used == 1U);
    assert(stats.elapsed_ms >= RF_LINK_ATTEMPT_TIMEOUT_MS);

    ResetFakes();
    g_send_tick_advance = 700U;
    ScriptRxFrameAt(0U, RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x22U, 0x33U, NULL, 0U);
    result = RfLink_SendPingAndWaitForPong(0x22U, 0x33U, &stats);
    assert(result == RF_LINK_PING_RESULT_TIMEOUT);
    assert(g_send_calls == 1U);
    assert(g_rx_script_index == 0U);
    assert(stats.attempts_used == 1U);
    assert(stats.retries_used == 0U);
    assert(stats.elapsed_ms >= RF_LINK_PING_TOTAL_TIMEOUT_MS);
    assert(stats.remote_phase == 0U);
    assert(stats.remote_reason == 0U);

    return 0;
}
