#include "app_master.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    bool has_frame;
    RfFrame_t frame;
} ScriptedRxStep_t;

static uint8_t g_last_ack_id;
static uint8_t g_last_nack_id;
static uint8_t g_last_nack_reason;
static uint8_t g_recover_rx_calls;
static bool g_send_frame_result;
static uint8_t g_send_frame_calls;
static RfFrame_t g_sent_frames[16];
static ScriptedRxStep_t g_scripted_rx_steps[64];
static uint8_t g_scripted_rx_step_count;
static uint8_t g_scripted_rx_step_index;
static uint32_t g_tick;

static bool g_decode_begin_result;
static AppDrawBeginCommand_t g_begin_cmd;
static bool g_decode_text_result;
static AppDrawTextCommand_t g_text_cmd;
static bool g_decode_commit_result;
static AppDrawCommitCommand_t g_commit_cmd;
static bool g_decode_flush_result;
static AppDisplayFlushCommand_t g_flush_cmd;

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
    (void)type;
    (void)payload;
    (void)len;
}

uint16_t OpenPeriph_GetUsbRxAvailable(void)
{
    return 0U;
}

void OpenPeriph_ResetSystem(void)
{
}

bool OpenPeriph_RenderLocalHello(void)
{
    return true;
}

uint8_t Cc1101Radio_GetMarcState(void)
{
    return 0U;
}

bool Cc1101Radio_ReadChipInfo(uint8_t *partnum, uint8_t *version)
{
    if ((partnum == NULL) || (version == NULL)) {
        return false;
    }
    *partnum = 0x12U;
    *version = 0x34U;
    return true;
}

bool Cc1101Radio_RecoverRx(void)
{
    ++g_recover_rx_calls;
    return true;
}

bool AppProtocol_DecodeDrawBegin(const uint8_t *buf, size_t len, AppDrawBeginCommand_t *out_cmd)
{
    (void)buf;
    (void)len;

    if ((out_cmd == NULL) || !g_decode_begin_result) {
        return false;
    }
    *out_cmd = g_begin_cmd;
    return true;
}

bool AppProtocol_DecodeDrawText(const uint8_t *buf, size_t len, AppDrawTextCommand_t *out_cmd)
{
    (void)buf;
    (void)len;

    if ((out_cmd == NULL) || !g_decode_text_result) {
        return false;
    }
    *out_cmd = g_text_cmd;
    return true;
}

bool AppProtocol_DecodeDrawCommit(const uint8_t *buf, size_t len, AppDrawCommitCommand_t *out_cmd)
{
    (void)buf;
    (void)len;

    if ((out_cmd == NULL) || !g_decode_commit_result) {
        return false;
    }
    *out_cmd = g_commit_cmd;
    return true;
}

bool AppProtocol_DecodeDisplayFlush(const uint8_t *buf, size_t len, AppDisplayFlushCommand_t *out_cmd)
{
    (void)buf;
    (void)len;

    if ((out_cmd == NULL) || !g_decode_flush_result) {
        return false;
    }
    *out_cmd = g_flush_cmd;
    return true;
}

static void WriteU16LE(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFFU);
    buf[1] = (uint8_t)(value >> 8);
}

static uint16_t ReadU16LE(const uint8_t *buf)
{
    return (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
}

size_t RfDrawProtocol_EncodeBegin(const RfDrawBegin_t *begin, uint8_t *out_buf, size_t out_capacity)
{
    if ((begin == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_BEGIN_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = begin->session_id;
    out_buf[1] = begin->flags;
    return RF_DRAW_BEGIN_PAYLOAD_LEN;
}

size_t RfDrawProtocol_EncodeText(const RfDrawText_t *text, uint8_t *out_buf, size_t out_capacity)
{
    size_t total_len;

    if ((text == NULL) || (out_buf == NULL) || (text->text_len > RF_DRAW_TEXT_MAX_LEN)) {
        return 0U;
    }

    total_len = RF_DRAW_TEXT_FIXED_LEN + (size_t)text->text_len;
    if (out_capacity < total_len) {
        return 0U;
    }

    out_buf[0] = text->session_id;
    out_buf[1] = text->op_index;
    WriteU16LE(&out_buf[2], text->x);
    WriteU16LE(&out_buf[4], text->y);
    out_buf[6] = text->font_id;
    out_buf[7] = text->text_len;
    if (text->text_len > 0U) {
        memcpy(&out_buf[RF_DRAW_TEXT_FIXED_LEN], text->text, text->text_len);
    }
    return total_len;
}

size_t RfDrawProtocol_EncodeCommit(const RfDrawCommit_t *commit, uint8_t *out_buf, size_t out_capacity)
{
    if ((commit == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_COMMIT_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = commit->session_id;
    return RF_DRAW_COMMIT_PAYLOAD_LEN;
}

bool RfDrawProtocol_DecodeAck(const uint8_t *buf, size_t len, RfDrawAck_t *out_ack)
{
    if ((buf == NULL) || (out_ack == NULL) || (len != RF_DRAW_ACK_PAYLOAD_LEN)) {
        return false;
    }

    out_ack->phase = buf[0];
    out_ack->value = buf[1];
    return true;
}

bool RfDrawProtocol_DecodeError(const uint8_t *buf, size_t len, RfDrawError_t *out_error)
{
    if ((buf == NULL) || (out_error == NULL) || (len != RF_DRAW_ERROR_PAYLOAD_LEN)) {
        return false;
    }

    out_error->phase = buf[0];
    out_error->reason = buf[1];
    return true;
}

bool RfLink_SendFrame(const RfFrame_t *frame)
{
    if (frame == NULL) {
        return false;
    }

    if (g_send_frame_calls < ARRAY_LEN(g_sent_frames)) {
        g_sent_frames[g_send_frame_calls] = *frame;
    }
    ++g_send_frame_calls;
    return g_send_frame_result;
}

bool RfLink_TryReceiveFrame(RfFrame_t *out_frame)
{
    ScriptedRxStep_t *step;

    if ((out_frame == NULL) || (g_scripted_rx_step_index >= g_scripted_rx_step_count)) {
        return false;
    }

    step = &g_scripted_rx_steps[g_scripted_rx_step_index];
    ++g_scripted_rx_step_index;
    if (!step->has_frame) {
        return false;
    }

    *out_frame = step->frame;
    return true;
}

RfLinkPingResult_t RfLink_SendPingAndWaitForPong(uint8_t dst_addr,
                                                 uint8_t seq,
                                                 RfLinkExchangeStats_t *out_stats)
{
    (void)dst_addr;
    (void)seq;
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof(*out_stats));
    }
    return RF_LINK_PING_RESULT_OK;
}

uint32_t HAL_GetTick(void)
{
    return g_tick++;
}

static void ResetCaptures(void)
{
    g_last_ack_id = 0U;
    g_last_nack_id = 0U;
    g_last_nack_reason = 0U;
    g_recover_rx_calls = 0U;
    g_send_frame_result = true;
    g_send_frame_calls = 0U;
    memset(g_sent_frames, 0, sizeof(g_sent_frames));
    memset(g_scripted_rx_steps, 0, sizeof(g_scripted_rx_steps));
    g_scripted_rx_step_count = 0U;
    g_scripted_rx_step_index = 0U;
    g_tick = 0U;
    g_decode_begin_result = false;
    memset(&g_begin_cmd, 0, sizeof(g_begin_cmd));
    g_decode_text_result = false;
    memset(&g_text_cmd, 0, sizeof(g_text_cmd));
    g_decode_commit_result = false;
    memset(&g_commit_cmd, 0, sizeof(g_commit_cmd));
    g_decode_flush_result = false;
    memset(&g_flush_cmd, 0, sizeof(g_flush_cmd));
}

static void ScriptDrawResponse(uint8_t msg_type,
                               uint8_t src_addr,
                               uint8_t seq,
                               const uint8_t *payload,
                               uint8_t payload_len)
{
    RfFrame_t *frame;

    assert(g_scripted_rx_step_count < ARRAY_LEN(g_scripted_rx_steps));
    frame = &g_scripted_rx_steps[g_scripted_rx_step_count].frame;
    g_scripted_rx_steps[g_scripted_rx_step_count].has_frame = true;
    memset(frame, 0, sizeof(*frame));
    frame->version = RF_FRAME_VERSION;
    frame->msg_type = msg_type;
    frame->dst_addr = OPENPERIPH_NODE_ADDR;
    frame->src_addr = src_addr;
    frame->seq = seq;
    frame->payload_len = payload_len;
    if ((payload != NULL) && (payload_len > 0U)) {
        memcpy(frame->payload, payload, payload_len);
    }
    ++g_scripted_rx_step_count;
}

static void ScriptDrawAck(uint8_t src_addr, uint8_t seq, uint8_t phase, uint8_t value)
{
    uint8_t payload[RF_DRAW_ACK_PAYLOAD_LEN] = { phase, value };
    ScriptDrawResponse(RF_MSG_DRAW_ACK, src_addr, seq, payload, RF_DRAW_ACK_PAYLOAD_LEN);
}

static void ScriptDrawError(uint8_t src_addr, uint8_t seq, uint8_t phase, uint8_t reason)
{
    uint8_t payload[RF_DRAW_ERROR_PAYLOAD_LEN] = { phase, reason };
    ScriptDrawResponse(RF_MSG_DRAW_ERROR, src_addr, seq, payload, RF_DRAW_ERROR_PAYLOAD_LEN);
}

static void ScriptNoFrame(uint8_t count)
{
    for (uint8_t i = 0U; i < count; ++i) {
        assert(g_scripted_rx_step_count < ARRAY_LEN(g_scripted_rx_steps));
        g_scripted_rx_steps[g_scripted_rx_step_count].has_frame = false;
        ++g_scripted_rx_step_count;
    }
}

static void TestDrawBeginSuccess(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_DRAW_BEGIN,
        .id = 0x40U,
        .payload_len = 3U,
    };

    ResetCaptures();
    g_decode_begin_result = true;
    g_begin_cmd.dst_addr = 0x22U;
    g_begin_cmd.session_id = 0x31U;
    g_begin_cmd.flags = APP_DRAW_FLAG_CLEAR_FIRST;
    ScriptDrawAck(g_begin_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_BEGIN, 0U);

    AppMaster_HandleUsbPacket(&pkt);

    assert(g_send_frame_calls == 1U);
    assert(g_sent_frames[0].msg_type == RF_MSG_DRAW_BEGIN);
    assert(g_sent_frames[0].dst_addr == 0x22U);
    assert(g_sent_frames[0].seq == pkt.id);
    assert(g_sent_frames[0].payload_len == RF_DRAW_BEGIN_PAYLOAD_LEN);
    assert(g_sent_frames[0].payload[0] == 0x31U);
    assert(g_sent_frames[0].payload[1] == APP_DRAW_FLAG_CLEAR_FIRST);
    assert(g_last_ack_id == pkt.id);
}

static void TestDrawTextSuccess(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_DRAW_TEXT,
        .id = 0x41U,
        .payload_len = 12U,
    };

    ResetCaptures();
    g_decode_text_result = true;
    g_text_cmd.dst_addr = 0x22U;
    g_text_cmd.session_id = 0x31U;
    g_text_cmd.op_index = 0x07U;
    g_text_cmd.x = 12U;
    g_text_cmd.y = 34U;
    g_text_cmd.font_id = APP_FONT_16;
    g_text_cmd.text_len = 5U;
    memcpy(g_text_cmd.text, "Hello", 5U);
    ScriptDrawAck(g_text_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_TEXT, g_text_cmd.op_index);

    AppMaster_HandleUsbPacket(&pkt);

    assert(g_send_frame_calls == 1U);
    assert(g_sent_frames[0].msg_type == RF_MSG_DRAW_TEXT);
    assert(g_sent_frames[0].dst_addr == 0x22U);
    assert(g_sent_frames[0].seq == pkt.id);
    assert(g_sent_frames[0].payload_len == (uint8_t)(RF_DRAW_TEXT_FIXED_LEN + 5U));
    assert(g_sent_frames[0].payload[0] == 0x31U);
    assert(g_sent_frames[0].payload[1] == 0x07U);
    assert(ReadU16LE(&g_sent_frames[0].payload[2]) == 12U);
    assert(ReadU16LE(&g_sent_frames[0].payload[4]) == 34U);
    assert(g_sent_frames[0].payload[6] == APP_FONT_16);
    assert(g_sent_frames[0].payload[7] == 5U);
    assert(memcmp(&g_sent_frames[0].payload[RF_DRAW_TEXT_FIXED_LEN], "Hello", 5U) == 0);
    assert(g_last_ack_id == pkt.id);
}

static void TestDrawTextRetriesAfterMissedAck(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_DRAW_TEXT,
        .id = 0x42U,
        .payload_len = 10U,
    };

    ResetCaptures();
    g_decode_text_result = true;
    g_text_cmd.dst_addr = 0x22U;
    g_text_cmd.session_id = 0x44U;
    g_text_cmd.op_index = 0x03U;
    g_text_cmd.x = 1U;
    g_text_cmd.y = 2U;
    g_text_cmd.font_id = APP_FONT_12;
    g_text_cmd.text_len = 2U;
    memcpy(g_text_cmd.text, "OK", 2U);
    ScriptNoFrame((uint8_t)((RF_LINK_ATTEMPT_TIMEOUT_MS / 2U) + 2U));
    ScriptDrawAck(g_text_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_TEXT, g_text_cmd.op_index);

    AppMaster_HandleUsbPacket(&pkt);

    assert(g_send_frame_calls == 2U);
    assert(memcmp(&g_sent_frames[0], &g_sent_frames[1], sizeof(RfFrame_t)) == 0);
    assert(g_last_ack_id == pkt.id);
}

static void TestDrawCommitAndFlushSuccess(void)
{
    Packet_t commit_pkt = {
        .type = PKT_TYPE_DRAW_COMMIT,
        .id = 0x43U,
        .payload_len = 2U,
    };
    Packet_t flush_pkt = {
        .type = PKT_TYPE_DISPLAY_FLUSH,
        .id = 0x44U,
        .payload_len = 3U,
    };

    ResetCaptures();
    g_decode_commit_result = true;
    g_commit_cmd.dst_addr = 0x22U;
    g_commit_cmd.session_id = 0x31U;
    ScriptDrawAck(g_commit_cmd.dst_addr, commit_pkt.id, RF_DRAW_PHASE_COMMIT, 0U);

    AppMaster_HandleUsbPacket(&commit_pkt);

    assert(g_send_frame_calls == 1U);
    assert(g_sent_frames[0].msg_type == RF_MSG_DRAW_COMMIT);
    assert(g_sent_frames[0].payload_len == RF_DRAW_COMMIT_PAYLOAD_LEN);
    assert(g_sent_frames[0].payload[0] == 0x31U);
    assert(g_last_ack_id == commit_pkt.id);

    g_decode_flush_result = true;
    g_flush_cmd.dst_addr = 0x22U;
    g_flush_cmd.session_id = 0x31U;
    g_flush_cmd.full_refresh = true;
    ScriptDrawAck(g_flush_cmd.dst_addr, flush_pkt.id, RF_DRAW_PHASE_FLUSH, 0U);

    AppMaster_HandleUsbPacket(&flush_pkt);

    assert(g_send_frame_calls == 2U);
    assert(g_sent_frames[1].msg_type == RF_MSG_DISPLAY_FLUSH);
    assert(g_sent_frames[1].payload_len == 2U);
    assert(g_sent_frames[1].payload[0] == 0x31U);
    assert(g_sent_frames[1].payload[1] == 1U);
    assert(g_last_ack_id == flush_pkt.id);
}

static void TestDrawTextRemoteErrorMapsToUsbNack(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_DRAW_TEXT,
        .id = 0x45U,
        .payload_len = 9U,
    };

    ResetCaptures();
    g_decode_text_result = true;
    g_text_cmd.dst_addr = 0x22U;
    g_text_cmd.session_id = 0x50U;
    g_text_cmd.op_index = 0x01U;
    g_text_cmd.x = 0U;
    g_text_cmd.y = 0U;
    g_text_cmd.font_id = APP_FONT_12;
    g_text_cmd.text_len = 1U;
    g_text_cmd.text[0] = 'X';
    ScriptDrawError(g_text_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_TEXT, RF_DRAW_ERROR_REASON_BAD_STATE);

    AppMaster_HandleUsbPacket(&pkt);

    assert(g_send_frame_calls == 1U);
    assert(g_last_ack_id == 0U);
    assert(g_last_nack_id == pkt.id);
    assert(g_last_nack_reason == RF_DRAW_ERROR_REASON_BAD_STATE);
}

static void TestDrawTextTimeoutRecoversRadioAndNacks(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_DRAW_TEXT,
        .id = 0x46U,
        .payload_len = 9U,
    };

    ResetCaptures();
    g_decode_text_result = true;
    g_text_cmd.dst_addr = 0x22U;
    g_text_cmd.session_id = 0x51U;
    g_text_cmd.op_index = 0x02U;
    g_text_cmd.x = 0U;
    g_text_cmd.y = 0U;
    g_text_cmd.font_id = APP_FONT_12;
    g_text_cmd.text_len = 2U;
    memcpy(g_text_cmd.text, "OK", 2U);

    AppMaster_HandleUsbPacket(&pkt);

    assert(g_send_frame_calls == RF_LINK_MAX_RETRIES);
    assert(g_last_ack_id == 0U);
    assert(g_last_nack_id == pkt.id);
    assert(g_last_nack_reason == 0x05U);
    assert(g_recover_rx_calls == RF_LINK_MAX_RETRIES);
}

int main(void)
{
    TestDrawBeginSuccess();
    TestDrawTextSuccess();
    TestDrawTextRetriesAfterMissedAck();
    TestDrawCommitAndFlushSuccess();
    TestDrawTextRemoteErrorMapsToUsbNack();
    TestDrawTextTimeoutRecoversRadioAndNacks();
    return 0;
}
