#include "app_slave.h"
#include "rf_draw_protocol.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool g_display_init_called;
static bool g_draw_text_called;
static bool g_draw_text_result;
static bool g_try_receive_result;
static bool g_is_for_local_result;
static bool g_send_frame_result;
static RfFrame_t g_received_frame;
static RfFrame_t g_sent_frame;
static AppDrawTextCommand_t g_draw_text_cmd;
static RfDrawStart_t g_decoded_start;
static bool g_decode_start_result;
static RfDrawChunk_t g_decoded_chunk;
static bool g_decode_chunk_result;
static bool g_last_ack_valid;
static RfDrawAck_t g_last_ack;
static bool g_last_error_valid;
static RfDrawError_t g_last_error;

bool DisplayService_Init(void)
{
    g_display_init_called = true;
    return true;
}

bool DisplayService_DrawText(const AppDrawTextCommand_t *cmd)
{
    if (cmd != NULL) {
        g_draw_text_cmd = *cmd;
    }
    g_draw_text_called = true;
    return g_draw_text_result;
}

bool AppProtocol_DecodeDrawText(const uint8_t *buf,
                                size_t len,
                                AppDrawTextCommand_t *out_cmd)
{
    (void)buf;
    (void)len;
    (void)out_cmd;
    return false;
}

bool RfDrawProtocol_DecodeStart(const uint8_t *buf, size_t len, RfDrawStart_t *out_start)
{
    (void)buf;
    (void)len;

    if ((out_start == NULL) || !g_decode_start_result) {
        return false;
    }

    *out_start = g_decoded_start;
    return true;
}

bool RfDrawProtocol_DecodeChunk(const uint8_t *buf, size_t len, RfDrawChunk_t *out_chunk)
{
    (void)buf;
    (void)len;

    if ((out_chunk == NULL) || !g_decode_chunk_result) {
        return false;
    }

    *out_chunk = g_decoded_chunk;
    return true;
}

size_t RfDrawProtocol_EncodeAck(const RfDrawAck_t *ack, uint8_t *out_buf, size_t out_capacity)
{
    if ((ack == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_ACK_PAYLOAD_LEN)) {
        return 0U;
    }

    g_last_ack_valid = true;
    g_last_ack = *ack;
    out_buf[0] = ack->phase;
    out_buf[1] = ack->value;
    return RF_DRAW_ACK_PAYLOAD_LEN;
}

size_t RfDrawProtocol_EncodeError(const RfDrawError_t *error, uint8_t *out_buf, size_t out_capacity)
{
    if ((error == NULL) || (out_buf == NULL) || (out_capacity < RF_DRAW_ERROR_PAYLOAD_LEN)) {
        return 0U;
    }

    g_last_error_valid = true;
    g_last_error = *error;
    out_buf[0] = error->phase;
    out_buf[1] = error->reason;
    return RF_DRAW_ERROR_PAYLOAD_LEN;
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
    g_draw_text_result = true;
    g_try_receive_result = false;
    g_is_for_local_result = false;
    g_send_frame_result = false;
    g_decode_start_result = false;
    g_decode_chunk_result = false;
    memset(&g_received_frame, 0, sizeof(g_received_frame));
    memset(&g_sent_frame, 0, sizeof(g_sent_frame));
    memset(&g_draw_text_cmd, 0, sizeof(g_draw_text_cmd));
    memset(&g_decoded_start, 0, sizeof(g_decoded_start));
    memset(&g_decoded_chunk, 0, sizeof(g_decoded_chunk));
    g_last_ack_valid = false;
    memset(&g_last_ack, 0, sizeof(g_last_ack));
    g_last_error_valid = false;
    memset(&g_last_error, 0, sizeof(g_last_error));
    AppSlave_ClearDrawState();
}

static void PrimeFrame(uint8_t msg_type, uint8_t seq)
{
    g_try_receive_result = true;
    g_is_for_local_result = true;
    g_send_frame_result = true;
    g_received_frame.version = RF_FRAME_VERSION;
    g_received_frame.msg_type = msg_type;
    g_received_frame.dst_addr = OPENPERIPH_NODE_ADDR;
    g_received_frame.src_addr = 0x22U;
    g_received_frame.seq = seq;
}

static void AssertAck(uint8_t phase, uint8_t value, uint8_t seq)
{
    assert(g_last_ack_valid);
    assert(g_sent_frame.version == RF_FRAME_VERSION);
    assert(g_sent_frame.msg_type == RF_MSG_DRAW_ACK);
    assert(g_sent_frame.dst_addr == 0x22U);
    assert(g_sent_frame.src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frame.seq == seq);
    assert(g_sent_frame.payload_len == RF_DRAW_ACK_PAYLOAD_LEN);
    assert(g_last_ack.phase == phase);
    assert(g_last_ack.value == value);
    assert(g_sent_frame.payload[0] == phase);
    assert(g_sent_frame.payload[1] == value);
}

static void AssertError(uint8_t phase, uint8_t reason, uint8_t seq)
{
    assert(g_last_error_valid);
    assert(g_sent_frame.version == RF_FRAME_VERSION);
    assert(g_sent_frame.msg_type == RF_MSG_DRAW_ERROR);
    assert(g_sent_frame.dst_addr == 0x22U);
    assert(g_sent_frame.src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frame.seq == seq);
    assert(g_sent_frame.payload_len == RF_DRAW_ERROR_PAYLOAD_LEN);
    assert(g_last_error.phase == phase);
    assert(g_last_error.reason == reason);
    assert(g_sent_frame.payload[0] == phase);
    assert(g_sent_frame.payload[1] == reason);
}

static void TestDrawStartStoresStateAndAcks(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_START, 0x51U);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = true;
    g_decoded_start.x = 12U;
    g_decoded_start.y = 34U;
    g_decoded_start.font_id = APP_FONT_16;
    g_decoded_start.flags = APP_DRAW_FLAG_CLEAR_FIRST;
    g_decoded_start.total_text_len = 5U;

    AppSlave_Poll();

    assert(!g_draw_text_called);
    AssertAck(RF_DRAW_PHASE_START, 0U, 0x51U);
}

static void TestDrawChunkAcceptsExpectedChunkAndAcks(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_START, 0x52U);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = true;
    g_decoded_start.x = 7U;
    g_decoded_start.y = 9U;
    g_decoded_start.font_id = APP_FONT_12;
    g_decoded_start.flags = APP_DRAW_FLAG_FULL_REFRESH;
    g_decoded_start.total_text_len = 5U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x53U);
    g_received_frame.payload_len = 5U;
    g_decode_chunk_result = true;
    g_decoded_chunk.chunk_index = 0U;
    g_decoded_chunk.chunk_len = 3U;
    memcpy(g_decoded_chunk.data, "abc", 3U);

    AppSlave_Poll();

    assert(!g_draw_text_called);
    AssertAck(RF_DRAW_PHASE_CHUNK, 0U, 0x53U);
}

static void TestDuplicateIdenticalChunkIsReAcked(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_START, 0x54U);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = true;
    g_decoded_start.total_text_len = 4U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x55U);
    g_received_frame.payload_len = 4U;
    g_decode_chunk_result = true;
    g_decoded_chunk.chunk_index = 0U;
    g_decoded_chunk.chunk_len = 2U;
    memcpy(g_decoded_chunk.data, "ab", 2U);
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x56U);
    g_received_frame.payload_len = 4U;
    g_decoded_chunk.chunk_index = 0U;
    g_decoded_chunk.chunk_len = 2U;
    memcpy(g_decoded_chunk.data, "ab", 2U);

    AppSlave_Poll();

    AssertAck(RF_DRAW_PHASE_CHUNK, 0U, 0x56U);
}

static void TestDuplicateMutatedChunkErrors(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_START, 0x57U);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = true;
    g_decoded_start.total_text_len = 4U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x58U);
    g_received_frame.payload_len = 4U;
    g_decode_chunk_result = true;
    g_decoded_chunk.chunk_index = 0U;
    g_decoded_chunk.chunk_len = 2U;
    memcpy(g_decoded_chunk.data, "ab", 2U);
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x59U);
    g_received_frame.payload_len = 4U;
    g_decoded_chunk.chunk_index = 0U;
    g_decoded_chunk.chunk_len = 2U;
    memcpy(g_decoded_chunk.data, "zz", 2U);
    AppSlave_Poll();

    assert(!g_draw_text_called);
    AssertError(RF_DRAW_PHASE_CHUNK, RF_DRAW_ERROR_REASON_BAD_CHUNK, 0x59U);
}

static void TestNewDrawStartResetsStaleState(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_START, 0x5AU);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = true;
    g_decoded_start.x = 1U;
    g_decoded_start.y = 2U;
    g_decoded_start.font_id = APP_FONT_12;
    g_decoded_start.flags = 0U;
    g_decoded_start.total_text_len = 4U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x5BU);
    g_received_frame.payload_len = 4U;
    g_decode_chunk_result = true;
    g_decoded_chunk.chunk_index = 0U;
    g_decoded_chunk.chunk_len = 2U;
    memcpy(g_decoded_chunk.data, "ab", 2U);
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_START, 0x5CU);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decoded_start.x = 90U;
    g_decoded_start.y = 91U;
    g_decoded_start.font_id = APP_FONT_16;
    g_decoded_start.flags = APP_DRAW_FLAG_CLEAR_FIRST;
    g_decoded_start.total_text_len = 2U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_COMMIT, 0x5DU);
    g_received_frame.payload_len = 0U;
    AppSlave_Poll();

    assert(!g_draw_text_called);
    AssertError(RF_DRAW_PHASE_COMMIT, RF_DRAW_ERROR_REASON_LENGTH, 0x5DU);
}

static void TestMalformedDrawStartClearsPreviousState(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_START, 0x5BU);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = true;
    g_decoded_start.total_text_len = 4U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x5CU);
    g_received_frame.payload_len = 4U;
    g_decode_chunk_result = true;
    g_decoded_chunk.chunk_index = 0U;
    g_decoded_chunk.chunk_len = 2U;
    memcpy(g_decoded_chunk.data, "ab", 2U);
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_START, 0x5DU);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = false;
    AppSlave_Poll();

    AssertError(RF_DRAW_PHASE_START, RF_DRAW_ERROR_REASON_LENGTH, 0x5DU);

    PrimeFrame(RF_MSG_DRAW_COMMIT, 0x5EU);
    g_received_frame.payload_len = 0U;
    AppSlave_Poll();

    assert(!g_draw_text_called);
    AssertError(RF_DRAW_PHASE_COMMIT, RF_DRAW_ERROR_REASON_BAD_STATE, 0x5EU);
}

static void TestZeroLengthDrawStartErrors(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_START, 0x5FU);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = true;
    g_decoded_start.total_text_len = 0U;

    AppSlave_Poll();

    assert(!g_draw_text_called);
    AssertError(RF_DRAW_PHASE_START, RF_DRAW_ERROR_REASON_LENGTH, 0x5FU);
}

static void TestOutOfOrderChunkErrors(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_START, 0x60U);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = true;
    g_decoded_start.total_text_len = 5U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x61U);
    g_received_frame.payload_len = 5U;
    g_decode_chunk_result = true;
    g_decoded_chunk.chunk_index = 1U;
    g_decoded_chunk.chunk_len = 3U;
    memcpy(g_decoded_chunk.data, "bad", 3U);
    AppSlave_Poll();

    assert(!g_draw_text_called);
    AssertError(RF_DRAW_PHASE_CHUNK, RF_DRAW_ERROR_REASON_BAD_CHUNK, 0x61U);
}

static void TestIncompleteCommitErrors(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_START, 0x62U);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = true;
    g_decoded_start.total_text_len = 5U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x63U);
    g_received_frame.payload_len = 4U;
    g_decode_chunk_result = true;
    g_decoded_chunk.chunk_index = 0U;
    g_decoded_chunk.chunk_len = 2U;
    memcpy(g_decoded_chunk.data, "ab", 2U);
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_COMMIT, 0x64U);
    g_received_frame.payload_len = 0U;
    AppSlave_Poll();

    assert(!g_draw_text_called);
    AssertError(RF_DRAW_PHASE_COMMIT, RF_DRAW_ERROR_REASON_LENGTH, 0x64U);
}

static void TestSuccessfulCommitDrawsTextAndAcks(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_START, 0x65U);
    g_received_frame.payload_len = RF_DRAW_START_PAYLOAD_LEN;
    g_decode_start_result = true;
    g_decoded_start.x = 100U;
    g_decoded_start.y = 200U;
    g_decoded_start.font_id = APP_FONT_16;
    g_decoded_start.flags = APP_DRAW_FLAG_CLEAR_FIRST | APP_DRAW_FLAG_FULL_REFRESH;
    g_decoded_start.total_text_len = 5U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x66U);
    g_received_frame.payload_len = 5U;
    g_decode_chunk_result = true;
    g_decoded_chunk.chunk_index = 0U;
    g_decoded_chunk.chunk_len = 3U;
    memcpy(g_decoded_chunk.data, "hel", 3U);
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_CHUNK, 0x67U);
    g_received_frame.payload_len = 4U;
    g_decoded_chunk.chunk_index = 1U;
    g_decoded_chunk.chunk_len = 2U;
    memcpy(g_decoded_chunk.data, "lo", 2U);
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_COMMIT, 0x68U);
    g_received_frame.payload_len = 0U;

    AppSlave_Poll();

    assert(g_draw_text_called);
    assert(g_draw_text_cmd.dst_addr == OPENPERIPH_NODE_ADDR);
    assert(g_draw_text_cmd.x == 100U);
    assert(g_draw_text_cmd.y == 200U);
    assert(g_draw_text_cmd.font_id == APP_FONT_16);
    assert(g_draw_text_cmd.flags == (APP_DRAW_FLAG_CLEAR_FIRST | APP_DRAW_FLAG_FULL_REFRESH));
    assert(g_draw_text_cmd.text_len == 5U);
    assert(memcmp(g_draw_text_cmd.text, "hello", 5U) == 0);
    AssertAck(RF_DRAW_PHASE_COMMIT, 0U, 0x68U);
}

static void TestPingStillPongs(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_PING, 0x69U);
    g_received_frame.payload_len = 0U;

    AppSlave_Poll();

    assert(!g_draw_text_called);
    assert(g_sent_frame.version == RF_FRAME_VERSION);
    assert(g_sent_frame.msg_type == RF_MSG_PONG);
    assert(g_sent_frame.dst_addr == 0x22U);
    assert(g_sent_frame.src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frame.seq == 0x69U);
    assert(g_sent_frame.payload_len == 0U);
}

int main(void)
{
    TestDrawStartStoresStateAndAcks();
    TestDrawChunkAcceptsExpectedChunkAndAcks();
    TestDuplicateIdenticalChunkIsReAcked();
    TestDuplicateMutatedChunkErrors();
    TestNewDrawStartResetsStaleState();
    TestMalformedDrawStartClearsPreviousState();
    TestZeroLengthDrawStartErrors();
    TestOutOfOrderChunkErrors();
    TestIncompleteCommitErrors();
    TestSuccessfulCommitDrawsTextAndAcks();
    TestPingStillPongs();

    return 0;
}
