#include "rf_draw_protocol.h"
#include "app_master.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARRAY_LEN(x) (sizeof(x) / sizeof((x)[0]))

typedef enum {
    EVENT_NONE = 0,
    EVENT_RF_SEND,
    EVENT_USB_ACK,
    EVENT_USB_NACK,
} EventKind_t;

typedef struct {
    EventKind_t kind;
    uint8_t arg0;
    uint8_t arg1;
} Event_t;

typedef struct {
    bool has_frame;
    RfFrame_t frame;
} ScriptedRxStep_t;

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
static bool g_decode_draw_result;
static AppDrawTextCommand_t g_draw_cmd;
static bool g_send_frame_result;
static uint8_t g_send_frame_calls;
static RfFrame_t g_sent_frames[16];
static ScriptedRxStep_t g_scripted_rx_steps[128];
static uint8_t g_scripted_rx_step_count;
static uint8_t g_scripted_rx_step_index;
static Event_t g_events[16];
static uint8_t g_event_count;
static uint32_t g_tick;

static void LogEvent(EventKind_t kind, uint8_t arg0, uint8_t arg1)
{
    if (g_event_count >= ARRAY_LEN(g_events)) {
        return;
    }

    g_events[g_event_count].kind = kind;
    g_events[g_event_count].arg0 = arg0;
    g_events[g_event_count].arg1 = arg1;
    ++g_event_count;
}

void OpenPeriph_SendUsbAck(uint8_t packet_id)
{
    g_last_ack_id = packet_id;
    LogEvent(EVENT_USB_ACK, packet_id, 0U);
}

void OpenPeriph_SendUsbNack(uint8_t packet_id, uint8_t reason)
{
    g_last_nack_id = packet_id;
    g_last_nack_reason = reason;
    LogEvent(EVENT_USB_NACK, packet_id, reason);
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

    if ((out_cmd == NULL) || !g_decode_draw_result) {
        return false;
    }

    *out_cmd = g_draw_cmd;
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

size_t RfDrawProtocol_EncodeStart(const RfDrawStart_t *start, uint8_t *out_buf, size_t out_capacity)
{
    if ((start == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_START_PAYLOAD_LEN)) {
        return 0U;
    }

    WriteU16LE(&out_buf[0], start->x);
    WriteU16LE(&out_buf[2], start->y);
    out_buf[4] = start->font_id;
    out_buf[5] = start->flags;
    out_buf[6] = start->total_text_len;
    return RF_DRAW_START_PAYLOAD_LEN;
}

bool RfDrawProtocol_DecodeStart(const uint8_t *buf, size_t len, RfDrawStart_t *out_start)
{
    if ((buf == NULL) || (out_start == NULL) || (len != RF_DRAW_START_PAYLOAD_LEN)) {
        return false;
    }

    out_start->x = ReadU16LE(&buf[0]);
    out_start->y = ReadU16LE(&buf[2]);
    out_start->font_id = buf[4];
    out_start->flags = buf[5];
    out_start->total_text_len = buf[6];
    return true;
}

size_t RfDrawProtocol_EncodeChunk(const RfDrawChunk_t *chunk, uint8_t *out_buf, size_t out_capacity)
{
    size_t needed_len;

    if ((chunk == NULL) || (out_buf == NULL) || (chunk->chunk_len > RF_DRAW_CHUNK_MAX_DATA)) {
        return 0U;
    }

    needed_len = RF_DRAW_CHUNK_HEADER_LEN + (size_t)chunk->chunk_len;
    if (out_capacity < needed_len) {
        return 0U;
    }

    out_buf[0] = chunk->chunk_index;
    out_buf[1] = chunk->chunk_len;
    if (chunk->chunk_len > 0U) {
        memcpy(&out_buf[2], chunk->data, chunk->chunk_len);
    }
    return needed_len;
}

bool RfDrawProtocol_DecodeChunk(const uint8_t *buf, size_t len, RfDrawChunk_t *out_chunk)
{
    size_t expected_len;

    if ((buf == NULL) || (out_chunk == NULL) || (len < RF_DRAW_CHUNK_HEADER_LEN)) {
        return false;
    }

    expected_len = RF_DRAW_CHUNK_HEADER_LEN + (size_t)buf[1];
    if ((buf[1] > RF_DRAW_CHUNK_MAX_DATA) || (len != expected_len)) {
        return false;
    }

    out_chunk->chunk_index = buf[0];
    out_chunk->chunk_len = buf[1];
    if (out_chunk->chunk_len > 0U) {
        memcpy(out_chunk->data, &buf[2], out_chunk->chunk_len);
    }
    return true;
}

size_t RfDrawProtocol_EncodeAck(const RfDrawAck_t *ack, uint8_t *out_buf, size_t out_capacity)
{
    if ((ack == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_ACK_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = ack->phase;
    out_buf[1] = ack->value;
    return RF_DRAW_ACK_PAYLOAD_LEN;
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

size_t RfDrawProtocol_EncodeError(const RfDrawError_t *error, uint8_t *out_buf, size_t out_capacity)
{
    if ((error == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_ERROR_PAYLOAD_LEN)) {
        return 0U;
    }

    out_buf[0] = error->phase;
    out_buf[1] = error->reason;
    return RF_DRAW_ERROR_PAYLOAD_LEN;
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
    LogEvent(EVENT_RF_SEND, frame->msg_type, frame->seq);
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

uint32_t HAL_GetTick(void)
{
    return g_tick++;
}

RfLinkPingResult_t RfLink_SendPingAndWaitForPong(uint8_t dst_addr,
                                                 uint8_t seq,
                                                 RfLinkExchangeStats_t *out_stats)
{
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof(*out_stats));
    }

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
    g_usb_rx_available = 0U;
    g_radio_state = 0U;
    g_chip_partnum = 0x12U;
    g_chip_version = 0x34U;
    g_chip_info_result = true;
    g_local_hello_called = false;
    g_local_hello_result = true;
    g_ping_result = RF_LINK_PING_RESULT_OK;
    g_last_ping_dst = 0U;
    g_last_ping_seq = 0U;
    g_decode_draw_result = false;
    memset(&g_draw_cmd, 0, sizeof(g_draw_cmd));
    g_send_frame_result = true;
    g_send_frame_calls = 0U;
    memset(g_sent_frames, 0, sizeof(g_sent_frames));
    memset(g_scripted_rx_steps, 0, sizeof(g_scripted_rx_steps));
    g_scripted_rx_step_count = 0U;
    g_scripted_rx_step_index = 0U;
    memset(g_events, 0, sizeof(g_events));
    g_event_count = 0U;
    g_tick = 0U;
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

static void ScriptNoFrame(uint8_t count)
{
    for (uint8_t i = 0U; i < count; ++i) {
        assert(g_scripted_rx_step_count < ARRAY_LEN(g_scripted_rx_steps));
        g_scripted_rx_steps[g_scripted_rx_step_count].has_frame = false;
        memset(&g_scripted_rx_steps[g_scripted_rx_step_count].frame,
               0,
               sizeof(g_scripted_rx_steps[g_scripted_rx_step_count].frame));
        ++g_scripted_rx_step_count;
    }
}

static void ScriptDrawAck(uint8_t src_addr, uint8_t seq, uint8_t phase, uint8_t value)
{
    uint8_t payload[RF_DRAW_ACK_PAYLOAD_LEN];
    RfDrawAck_t ack = {
        .phase = phase,
        .value = value,
    };
    size_t len = RfDrawProtocol_EncodeAck(&ack, payload, sizeof(payload));

    assert(len == RF_DRAW_ACK_PAYLOAD_LEN);
    ScriptDrawResponse(RF_MSG_DRAW_ACK, src_addr, seq, payload, (uint8_t)len);
}

static void ScriptDrawError(uint8_t src_addr, uint8_t seq, uint8_t phase, uint8_t reason)
{
    uint8_t payload[RF_DRAW_ERROR_PAYLOAD_LEN];
    RfDrawError_t error = {
        .phase = phase,
        .reason = reason,
    };
    size_t len = RfDrawProtocol_EncodeError(&error, payload, sizeof(payload));

    assert(len == RF_DRAW_ERROR_PAYLOAD_LEN);
    ScriptDrawResponse(RF_MSG_DRAW_ERROR, src_addr, seq, payload, (uint8_t)len);
}

static void PrimeDrawCommand(uint8_t dst_addr, const char *text)
{
    size_t text_len = strlen(text);

    assert(text_len <= APP_TEXT_MAX_LEN);
    g_decode_draw_result = true;
    memset(&g_draw_cmd, 0, sizeof(g_draw_cmd));
    g_draw_cmd.dst_addr = dst_addr;
    g_draw_cmd.x = 12U;
    g_draw_cmd.y = 34U;
    g_draw_cmd.font_id = APP_FONT_16;
    g_draw_cmd.flags = APP_DRAW_FLAG_CLEAR_FIRST;
    g_draw_cmd.text_len = (uint8_t)text_len;
    memcpy(g_draw_cmd.text, text, text_len);
}

static size_t ExpectedChunkCount(uint8_t text_len)
{
    return ((size_t)text_len + RF_DRAW_CHUNK_MAX_DATA - 1U) / RF_DRAW_CHUNK_MAX_DATA;
}

static void AssertStartFrame(size_t index, uint8_t seq)
{
    RfDrawStart_t start;

    assert(index < g_send_frame_calls);
    assert(g_sent_frames[index].version == RF_FRAME_VERSION);
    assert(g_sent_frames[index].msg_type == RF_MSG_DRAW_START);
    assert(g_sent_frames[index].dst_addr == g_draw_cmd.dst_addr);
    assert(g_sent_frames[index].src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frames[index].seq == seq);
    assert(g_sent_frames[index].payload_len == RF_DRAW_START_PAYLOAD_LEN);
    assert(RfDrawProtocol_DecodeStart(g_sent_frames[index].payload,
                                      g_sent_frames[index].payload_len,
                                      &start));
    assert(start.x == g_draw_cmd.x);
    assert(start.y == g_draw_cmd.y);
    assert(start.font_id == g_draw_cmd.font_id);
    assert(start.flags == g_draw_cmd.flags);
    assert(start.total_text_len == g_draw_cmd.text_len);
}

static void AssertChunkFrame(size_t index, uint8_t seq, uint8_t chunk_index, size_t offset)
{
    RfDrawChunk_t chunk;
    size_t expected_len = (size_t)g_draw_cmd.text_len - offset;

    if (expected_len > RF_DRAW_CHUNK_MAX_DATA) {
        expected_len = RF_DRAW_CHUNK_MAX_DATA;
    }

    assert(index < g_send_frame_calls);
    assert(g_sent_frames[index].version == RF_FRAME_VERSION);
    assert(g_sent_frames[index].msg_type == RF_MSG_DRAW_CHUNK);
    assert(g_sent_frames[index].dst_addr == g_draw_cmd.dst_addr);
    assert(g_sent_frames[index].src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frames[index].seq == seq);
    assert(RfDrawProtocol_DecodeChunk(g_sent_frames[index].payload,
                                      g_sent_frames[index].payload_len,
                                      &chunk));
    assert(chunk.chunk_index == chunk_index);
    assert(chunk.chunk_len == expected_len);
    assert(memcmp(chunk.data, &g_draw_cmd.text[offset], expected_len) == 0);
}

static void AssertCommitFrame(size_t index, uint8_t seq)
{
    assert(index < g_send_frame_calls);
    assert(g_sent_frames[index].version == RF_FRAME_VERSION);
    assert(g_sent_frames[index].msg_type == RF_MSG_DRAW_COMMIT);
    assert(g_sent_frames[index].dst_addr == g_draw_cmd.dst_addr);
    assert(g_sent_frames[index].src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frames[index].seq == seq);
    assert(g_sent_frames[index].payload_len == 0U);
}

static void TestGetStatusReportsRadioInfo(void)
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
}

static void TestGetStatusHandlesChipInfoFailure(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_COMMAND,
        .id = 0x33U,
        .payload_len = 1U,
        .payload = { CMD_GET_STATUS },
    };

    ResetCaptures();
    g_chip_info_result = false;
    g_usb_rx_available = 0x1234U;
    g_radio_state = CC1101_RADIO_STATE_RX;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_type == PKT_TYPE_STATUS);
    assert(g_last_payload_len == 10U);
    assert(g_last_payload[8] == 0xFFU);
    assert(g_last_payload[9] == 0xFFU);
}

static void TestLocalHelloSuccess(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_COMMAND,
        .id = 0x44U,
        .payload_len = 1U,
        .payload = { CMD_LOCAL_HELLO },
    };

    ResetCaptures();
    g_local_hello_result = true;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_local_hello_called);
    assert(g_last_ack_id == 0x44U);
    assert(g_last_nack_id == 0U);
}

static void TestLocalHelloFailure(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_COMMAND,
        .id = 0x45U,
        .payload_len = 1U,
        .payload = { CMD_LOCAL_HELLO },
    };

    ResetCaptures();
    g_local_hello_result = false;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_local_hello_called);
    assert(g_last_nack_id == 0x45U);
    assert(g_last_nack_reason == 0x05U);
}

static void TestDrawTextStagedSuccess(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_DRAW_TEXT,
        .id = 0x50U,
        .payload_len = 1U,
    };
    size_t chunk_count;
    size_t offset = 0U;

    ResetCaptures();
    PrimeDrawCommand(0x22U, "Hello staged draw");
    chunk_count = ExpectedChunkCount(g_draw_cmd.text_len);
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_START, 0U);
    for (size_t i = 0U; i < chunk_count; ++i) {
        ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_CHUNK, (uint8_t)i);
    }
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_COMMIT, 0U);

    AppMaster_HandleUsbPacket(&pkt);

    assert(g_send_frame_calls == (uint8_t)(chunk_count + 2U));
    AssertStartFrame(0U, pkt.id);
    for (size_t i = 0U; i < chunk_count; ++i) {
        AssertChunkFrame(i + 1U, pkt.id, (uint8_t)i, offset);
        offset += RF_DRAW_CHUNK_MAX_DATA;
    }
    AssertCommitFrame(chunk_count + 1U, pkt.id);
    assert(g_last_ack_id == pkt.id);
    assert(g_last_nack_id == 0U);
    assert(g_event_count == (uint8_t)(chunk_count + 3U));
    assert(g_events[0].kind == EVENT_RF_SEND);
    assert(g_events[0].arg0 == RF_MSG_DRAW_START);
    assert(g_events[1].kind == EVENT_RF_SEND);
    assert(g_events[1].arg0 == RF_MSG_DRAW_CHUNK);
    assert(g_events[2].kind == EVENT_RF_SEND);
    assert(g_events[2].arg0 == RF_MSG_DRAW_COMMIT);
    assert(g_events[3].kind == EVENT_USB_ACK);
    assert(g_events[3].arg0 == pkt.id);
}

static void TestDrawTextIgnoresStaleAckUntilCorrectAckArrives(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_DRAW_TEXT,
        .id = 0x53U,
        .payload_len = 1U,
    };

    ResetCaptures();
    PrimeDrawCommand(0x22U, "Hello");
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_CHUNK, 0U);
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_START, 0U);
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_CHUNK, 0U);
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_COMMIT, 0U);

    AppMaster_HandleUsbPacket(&pkt);

    assert(g_send_frame_calls == 3U);
    AssertStartFrame(0U, pkt.id);
    AssertChunkFrame(1U, pkt.id, 0U, 0U);
    AssertCommitFrame(2U, pkt.id);
    assert(g_last_ack_id == pkt.id);
    assert(g_last_nack_id == 0U);
}

static void TestDrawTextRetriesChunkAfterMissedAck(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_DRAW_TEXT,
        .id = 0x54U,
        .payload_len = 1U,
    };

    ResetCaptures();
    PrimeDrawCommand(0x22U, "Hello");
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_START, 0U);
    ScriptNoFrame((uint8_t)((RF_LINK_ATTEMPT_TIMEOUT_MS / 2U) + 2U));
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_CHUNK, 0U);
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_COMMIT, 0U);

    AppMaster_HandleUsbPacket(&pkt);

    assert(g_send_frame_calls == 4U);
    AssertStartFrame(0U, pkt.id);
    AssertChunkFrame(1U, pkt.id, 0U, 0U);
    AssertChunkFrame(2U, pkt.id, 0U, 0U);
    AssertCommitFrame(3U, pkt.id);
    assert(g_last_ack_id == pkt.id);
    assert(g_last_nack_id == 0U);
}

static void TestDrawTextExchangeFailureMapsToUsbNack(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_DRAW_TEXT,
        .id = 0x51U,
        .payload_len = 1U,
    };

    ResetCaptures();
    PrimeDrawCommand(0x22U, "Hello");
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_START, 0U);

    AppMaster_HandleUsbPacket(&pkt);

    assert(g_send_frame_calls >= 2U);
    AssertStartFrame(0U, pkt.id);
    for (uint8_t i = 1U; i < g_send_frame_calls; ++i) {
        AssertChunkFrame(i, pkt.id, 0U, 0U);
    }
    assert(g_last_ack_id == 0U);
    assert(g_last_nack_id == pkt.id);
    assert(g_last_nack_reason == 0x05U);
    assert(g_event_count == (uint8_t)(g_send_frame_calls + 1U));
    assert(g_events[0].kind == EVENT_RF_SEND);
    assert(g_events[0].arg0 == RF_MSG_DRAW_START);
    assert(g_events[1].kind == EVENT_RF_SEND);
    assert(g_events[1].arg0 == RF_MSG_DRAW_CHUNK);
    assert(g_events[g_event_count - 1U].kind == EVENT_USB_NACK);
    assert(g_events[g_event_count - 1U].arg0 == pkt.id);
}

static void TestDrawTextRemoteErrorMapsToUsbNack(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_DRAW_TEXT,
        .id = 0x52U,
        .payload_len = 1U,
    };

    ResetCaptures();
    PrimeDrawCommand(0x22U, "Hello");
    ScriptDrawAck(g_draw_cmd.dst_addr, pkt.id, RF_DRAW_PHASE_START, 0U);
    ScriptDrawError(g_draw_cmd.dst_addr,
                    pkt.id,
                    RF_DRAW_PHASE_CHUNK,
                    RF_DRAW_ERROR_REASON_BAD_CHUNK);

    AppMaster_HandleUsbPacket(&pkt);

    assert(g_send_frame_calls == 2U);
    AssertStartFrame(0U, pkt.id);
    AssertChunkFrame(1U, pkt.id, 0U, 0U);
    assert(g_last_ack_id == 0U);
    assert(g_last_nack_id == pkt.id);
    assert(g_last_nack_reason == RF_DRAW_ERROR_REASON_BAD_CHUNK);
    assert(g_event_count == 3U);
    assert(g_events[2].kind == EVENT_USB_NACK);
}

static void TestRfPingSuccess(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_COMMAND,
        .id = 0x46U,
        .payload_len = 2U,
        .payload = { CMD_RF_PING, 0x22U },
    };

    ResetCaptures();
    g_ping_result = RF_LINK_PING_RESULT_OK;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_ack_id == 0x46U);
    assert(g_last_nack_id == 0U);
    assert(g_last_ping_dst == 0x22U);
    assert(g_last_ping_seq == 0x46U);
}

static void TestRfPingTimeout(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_COMMAND,
        .id = 0x47U,
        .payload_len = 2U,
        .payload = { CMD_RF_PING, 0x22U },
    };

    ResetCaptures();
    g_ping_result = RF_LINK_PING_RESULT_TIMEOUT;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_ack_id == 0U);
    assert(g_last_nack_id == 0x47U);
    assert(g_last_nack_reason == 0x06U);
}

static void TestRfPingRejectsInvalidLength(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_COMMAND,
        .id = 0x48U,
        .payload_len = 3U,
        .payload = { CMD_RF_PING, 0x22U, 0x99U },
    };

    ResetCaptures();
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_ack_id == 0U);
    assert(g_last_nack_id == 0x48U);
    assert(g_last_nack_reason == 0x03U);
}

static void TestRfPingSendFail(void)
{
    Packet_t pkt = {
        .type = PKT_TYPE_COMMAND,
        .id = 0x49U,
        .payload_len = 2U,
        .payload = { CMD_RF_PING, 0x22U },
    };

    ResetCaptures();
    g_ping_result = RF_LINK_PING_RESULT_SEND_FAIL;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_ack_id == 0U);
    assert(g_last_nack_id == 0x49U);
    assert(g_last_nack_reason == 0x05U);
}

int main(void)
{
    TestGetStatusReportsRadioInfo();
    TestGetStatusHandlesChipInfoFailure();
    TestLocalHelloSuccess();
    TestLocalHelloFailure();
    TestDrawTextStagedSuccess();
    TestDrawTextIgnoresStaleAckUntilCorrectAckArrives();
    TestDrawTextRetriesChunkAfterMissedAck();
    TestDrawTextExchangeFailureMapsToUsbNack();
    TestDrawTextRemoteErrorMapsToUsbNack();
    TestRfPingSuccess();
    TestRfPingTimeout();
    TestRfPingRejectsInvalidLength();
    TestRfPingSendFail();
    return 0;
}
