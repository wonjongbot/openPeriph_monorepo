#include "app_slave.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool g_display_init_called;
static bool g_draw_text_called;
static bool g_try_receive_result;
static bool g_is_for_local_result;
static bool g_send_frame_result;
static bool g_decode_draw_text_result;
static RfFrame_t g_received_frame;
static RfFrame_t g_sent_frame;

bool DisplayService_Init(void)
{
    g_display_init_called = true;
    return true;
}

bool DisplayService_DrawText(const AppDrawTextCommand_t *cmd)
{
    (void)cmd;
    g_draw_text_called = true;
    return true;
}

bool AppProtocol_DecodeDrawText(const uint8_t *buf,
                                size_t len,
                                AppDrawTextCommand_t *out_cmd)
{
    (void)buf;
    (void)len;

    if ((out_cmd == NULL) || !g_decode_draw_text_result) {
        return false;
    }

    memset(out_cmd, 0, sizeof(*out_cmd));
    return true;
}

bool RfLink_TryReceiveFrame(RfFrame_t *out_frame)
{
    if ((out_frame == NULL) || !g_try_receive_result) {
        return false;
    }

    *out_frame = g_received_frame;
    return true;
}

bool RfLink_IsForLocalNode(const RfFrame_t *frame)
{
    (void)frame;
    return g_is_for_local_result;
}

bool RfLink_SendFrame(const RfFrame_t *frame)
{
    if (frame == NULL) {
        return false;
    }

    g_sent_frame = *frame;
    return g_send_frame_result;
}

static void ResetFakes(void)
{
    g_display_init_called = false;
    g_draw_text_called = false;
    g_try_receive_result = false;
    g_is_for_local_result = false;
    g_send_frame_result = false;
    g_decode_draw_text_result = false;
    memset(&g_received_frame, 0, sizeof(g_received_frame));
    memset(&g_sent_frame, 0, sizeof(g_sent_frame));
}

int main(void)
{
    ResetFakes();
    g_try_receive_result = true;
    g_is_for_local_result = true;
    g_send_frame_result = true;
    g_received_frame.version = RF_FRAME_VERSION;
    g_received_frame.msg_type = RF_MSG_PING;
    g_received_frame.dst_addr = OPENPERIPH_NODE_ADDR;
    g_received_frame.src_addr = 0x22U;
    g_received_frame.seq = 0x5AU;
    g_received_frame.payload_len = 0U;

    AppSlave_Poll();

    assert(!g_draw_text_called);
    assert(g_sent_frame.version == RF_FRAME_VERSION);
    assert(g_sent_frame.msg_type == RF_MSG_PONG);
    assert(g_sent_frame.dst_addr == 0x22U);
    assert(g_sent_frame.src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frame.seq == 0x5AU);
    assert(g_sent_frame.payload_len == 0U);

    ResetFakes();
    g_try_receive_result = true;
    g_is_for_local_result = true;
    g_decode_draw_text_result = true;
    g_received_frame.version = RF_FRAME_VERSION;
    g_received_frame.msg_type = RF_MSG_DRAW_START;
    g_received_frame.dst_addr = OPENPERIPH_NODE_ADDR;
    g_received_frame.src_addr = 0x22U;
    g_received_frame.seq = 0x5BU;
    g_received_frame.payload_len = 1U;
    g_received_frame.payload[0] = 0x20U;

    AppSlave_Poll();

    assert(g_draw_text_called);
    assert(g_sent_frame.msg_type == 0U);

    ResetFakes();
    g_try_receive_result = true;
    g_is_for_local_result = true;
    g_decode_draw_text_result = true;
    g_received_frame.version = RF_FRAME_VERSION;
    g_received_frame.msg_type = RF_MSG_DRAW_START;
    g_received_frame.dst_addr = OPENPERIPH_NODE_ADDR;
    g_received_frame.src_addr = 0x22U;
    g_received_frame.seq = 0x5CU;
    g_received_frame.payload_len = 1U;
    g_received_frame.payload[0] = 0x20U;

    AppSlave_Poll();

    assert(g_draw_text_called);

    return 0;
}
