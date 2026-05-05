#define OPENPERIPH_HOST_TEST

#include "app_slave.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool g_display_init_called;
static bool g_display_clear_buffer_called;
static bool g_display_clear_buffer_result;
static bool g_draw_text_called;
static bool g_draw_text_result;
static bool g_draw_tilemap_called;
static bool g_draw_tilemap_result;
static bool g_display_flush_called;
static bool g_display_flush_result;
static bool g_try_receive_result;
static bool g_is_for_local_result;
static bool g_send_frame_result;
static RfFrame_t g_received_frame;
static RfFrame_t g_sent_frame;
static AppDrawTextCommand_t g_draw_text_cmd;
static RfDrawBegin_t g_decoded_begin;
static bool g_decode_begin_result;
static RfDrawText_t g_decoded_text;
static bool g_decode_text_result;
static RfDrawTilemap_t g_decoded_tilemap;
static bool g_decode_tilemap_result;
static RfDrawCommit_t g_decoded_commit;
static bool g_decode_commit_result;
static bool g_last_ack_valid;
static RfDrawAck_t g_last_ack;
static bool g_last_error_valid;
static RfDrawError_t g_last_error;
static uint32_t g_tick;
static GPIO_PinState g_button_pin_state;
static GPIO_PinState g_led_pin_state;
static uint16_t g_last_gpio_write_pin;
static uint8_t g_radio_init_calls;

bool DisplayService_Init(void)
{
    g_display_init_called = true;
    return true;
}

bool DisplayService_Clear(bool full_refresh)
{
    (void)full_refresh;
    return true;
}

bool DisplayService_ClearBuffer(void)
{
    g_display_clear_buffer_called = true;
    return g_display_clear_buffer_result;
}

bool DisplayService_DrawText(const AppDrawTextCommand_t *cmd)
{
    if (cmd != NULL) {
        g_draw_text_cmd = *cmd;
    }
    g_draw_text_called = true;
    return g_draw_text_result;
}

bool DisplayService_DrawTileGlyph(uint16_t tile_index, uint8_t glyph_id)
{
    (void)tile_index;
    (void)glyph_id;
    return true;
}

bool DisplayService_DrawTilemapChunk(uint16_t tile_offset,
                                     const uint8_t *packed_ids,
                                     uint8_t byte_count)
{
    (void)tile_offset;
    (void)packed_ids;
    g_draw_tilemap_called = true;
    return g_draw_tilemap_result && (byte_count > 0U);
}

bool DisplayService_RenderText(uint16_t x,
                               uint16_t y,
                               DisplayServiceFont_t font,
                               const char *text,
                               bool clear_first,
                               bool full_refresh)
{
    (void)x;
    (void)y;
    (void)font;
    (void)text;
    (void)clear_first;
    (void)full_refresh;
    return true;
}

bool DisplayService_Flush(bool full_refresh)
{
    (void)full_refresh;
    g_display_flush_called = true;
    return g_display_flush_result;
}

void DisplayService_Sleep(void)
{
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

bool RfDrawProtocol_DecodeBegin(const uint8_t *buf, size_t len, RfDrawBegin_t *out_begin)
{
    (void)buf;
    (void)len;

    if ((out_begin == NULL) || !g_decode_begin_result) {
        return false;
    }
    *out_begin = g_decoded_begin;
    return true;
}

bool RfDrawProtocol_DecodeText(const uint8_t *buf, size_t len, RfDrawText_t *out_text)
{
    (void)buf;
    (void)len;

    if ((out_text == NULL) || !g_decode_text_result) {
        return false;
    }
    *out_text = g_decoded_text;
    return true;
}

bool RfDrawProtocol_DecodeTilemap(const uint8_t *buf, size_t len, RfDrawTilemap_t *out_tilemap)
{
    (void)buf;
    (void)len;

    if ((out_tilemap == NULL) || !g_decode_tilemap_result) {
        return false;
    }
    *out_tilemap = g_decoded_tilemap;
    return true;
}

bool RfDrawProtocol_DecodeCommit(const uint8_t *buf, size_t len, RfDrawCommit_t *out_commit)
{
    (void)buf;
    (void)len;

    if ((out_commit == NULL) || !g_decode_commit_result) {
        return false;
    }
    *out_commit = g_decoded_commit;
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

uint32_t HAL_GetTick(void)
{
    return g_tick;
}

void HAL_Delay(uint32_t delay)
{
    g_tick += delay;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    (void)GPIOx;
    if (GPIO_Pin == GPIO_PIN_11) {
        return g_button_pin_state;
    }
    return GPIO_PIN_SET;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
    (void)GPIOx;
    g_last_gpio_write_pin = GPIO_Pin;
    if (GPIO_Pin == GPIO_PIN_14) {
        g_led_pin_state = PinState;
    }
}

bool Cc1101Radio_Init(void)
{
    ++g_radio_init_calls;
    return true;
}

static void ResetFakes(void)
{
    g_display_init_called = false;
    g_display_clear_buffer_called = false;
    g_display_clear_buffer_result = true;
    g_draw_text_called = false;
    g_draw_text_result = true;
    g_draw_tilemap_called = false;
    g_draw_tilemap_result = true;
    g_display_flush_called = false;
    g_display_flush_result = true;
    g_try_receive_result = false;
    g_is_for_local_result = false;
    g_send_frame_result = false;
    memset(&g_received_frame, 0, sizeof(g_received_frame));
    memset(&g_sent_frame, 0, sizeof(g_sent_frame));
    memset(&g_draw_text_cmd, 0, sizeof(g_draw_text_cmd));
    memset(&g_decoded_begin, 0, sizeof(g_decoded_begin));
    memset(&g_decoded_text, 0, sizeof(g_decoded_text));
    memset(&g_decoded_tilemap, 0, sizeof(g_decoded_tilemap));
    memset(&g_decoded_commit, 0, sizeof(g_decoded_commit));
    g_decode_begin_result = false;
    g_decode_text_result = false;
    g_decode_tilemap_result = false;
    g_decode_commit_result = false;
    g_last_ack_valid = false;
    memset(&g_last_ack, 0, sizeof(g_last_ack));
    g_last_error_valid = false;
    memset(&g_last_error, 0, sizeof(g_last_error));
    AppSlave_ClearDrawState();
    g_tick = 0U;
    g_button_pin_state = GPIO_PIN_SET;
    g_led_pin_state = GPIO_PIN_RESET;
    g_last_gpio_write_pin = 0U;
    g_radio_init_calls = 0U;
    AppSlave_ResetButtonStateForTest();
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
    assert(g_sent_frame.msg_type == RF_MSG_DRAW_ACK);
    assert(g_sent_frame.dst_addr == 0x22U);
    assert(g_sent_frame.src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frame.seq == seq);
    assert(g_last_ack.phase == phase);
    assert(g_last_ack.value == value);
}

static void AssertError(uint8_t phase, uint8_t reason, uint8_t seq)
{
    assert(g_last_error_valid);
    assert(g_sent_frame.msg_type == RF_MSG_DRAW_ERROR);
    assert(g_sent_frame.dst_addr == 0x22U);
    assert(g_sent_frame.src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frame.seq == seq);
    assert(g_last_error.phase == phase);
    assert(g_last_error.reason == reason);
}

static void TestDrawBeginClearsAndAcks(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_BEGIN, 0x51U);
    g_received_frame.payload_len = RF_DRAW_BEGIN_PAYLOAD_LEN;
    g_decode_begin_result = true;
    g_decoded_begin.session_id = 0x31U;
    g_decoded_begin.flags = APP_DRAW_FLAG_CLEAR_FIRST;

    AppSlave_Poll();

    assert(g_display_clear_buffer_called);
    AssertAck(RF_DRAW_PHASE_BEGIN, 0U, 0x51U);
}

static void TestDuplicateBeginIsReAcked(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_BEGIN, 0x52U);
    g_received_frame.payload_len = RF_DRAW_BEGIN_PAYLOAD_LEN;
    g_decode_begin_result = true;
    g_decoded_begin.session_id = 0x31U;
    g_decoded_begin.flags = APP_DRAW_FLAG_CLEAR_FIRST;
    AppSlave_Poll();

    g_display_clear_buffer_called = false;
    PrimeFrame(RF_MSG_DRAW_BEGIN, 0x53U);
    g_received_frame.payload_len = RF_DRAW_BEGIN_PAYLOAD_LEN;
    AppSlave_Poll();

    assert(!g_display_clear_buffer_called);
    AssertAck(RF_DRAW_PHASE_BEGIN, 0U, 0x53U);
}

static void TestDrawTextAcceptsExpectedOpAndReAcksDuplicate(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_BEGIN, 0x54U);
    g_received_frame.payload_len = RF_DRAW_BEGIN_PAYLOAD_LEN;
    g_decode_begin_result = true;
    g_decoded_begin.session_id = 0x31U;
    g_decoded_begin.flags = 0U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_TEXT, 0x00U);
    g_received_frame.payload_len = RF_DRAW_TEXT_FIXED_LEN + 5U;
    g_decode_text_result = true;
    g_decoded_text.session_id = 0x31U;
    g_decoded_text.op_index = 0x00U;
    g_decoded_text.x = 100U;
    g_decoded_text.y = 200U;
    g_decoded_text.font_id = APP_FONT_16;
    g_decoded_text.text_len = 5U;
    memcpy(g_decoded_text.text, "hello", 5U);
    AppSlave_Poll();

    assert(g_draw_text_called);
    assert(g_draw_text_cmd.session_id == 0x31U);
    assert(g_draw_text_cmd.op_index == 0x00U);
    assert(g_draw_text_cmd.x == 100U);
    assert(g_draw_text_cmd.y == 200U);
    assert(g_draw_text_cmd.font_id == APP_FONT_16);
    assert(g_draw_text_cmd.text_len == 5U);
    assert(memcmp(g_draw_text_cmd.text, "hello", 5U) == 0);
    AssertAck(RF_DRAW_PHASE_TEXT, 0x00U, 0x00U);

    g_draw_text_called = false;
    PrimeFrame(RF_MSG_DRAW_TEXT, 0x00U);
    g_received_frame.payload_len = RF_DRAW_TEXT_FIXED_LEN + 5U;
    AppSlave_Poll();

    assert(!g_draw_text_called);
    AssertAck(RF_DRAW_PHASE_TEXT, 0x00U, 0x00U);
}

static void TestOutOfOrderTextErrors(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_BEGIN, 0x55U);
    g_received_frame.payload_len = RF_DRAW_BEGIN_PAYLOAD_LEN;
    g_decode_begin_result = true;
    g_decoded_begin.session_id = 0x31U;
    g_decoded_begin.flags = 0U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_TEXT, 0x02U);
    g_received_frame.payload_len = RF_DRAW_TEXT_FIXED_LEN + 1U;
    g_decode_text_result = true;
    g_decoded_text.session_id = 0x31U;
    g_decoded_text.op_index = 0x02U;
    g_decoded_text.x = 0U;
    g_decoded_text.y = 0U;
    g_decoded_text.font_id = APP_FONT_12;
    g_decoded_text.text_len = 1U;
    g_decoded_text.text[0] = 'X';

    AppSlave_Poll();

    AssertError(RF_DRAW_PHASE_TEXT, RF_DRAW_ERROR_REASON_BAD_CHUNK, 0x02U);
}

static void TestCommitAndFlushAreIdempotent(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_BEGIN, 0x56U);
    g_received_frame.payload_len = RF_DRAW_BEGIN_PAYLOAD_LEN;
    g_decode_begin_result = true;
    g_decoded_begin.session_id = 0x31U;
    g_decoded_begin.flags = 0U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_COMMIT, 0x57U);
    g_received_frame.payload_len = RF_DRAW_COMMIT_PAYLOAD_LEN;
    g_decode_commit_result = true;
    g_decoded_commit.session_id = 0x31U;
    AppSlave_Poll();
    AssertAck(RF_DRAW_PHASE_COMMIT, 0U, 0x57U);

    PrimeFrame(RF_MSG_DRAW_COMMIT, 0x58U);
    g_received_frame.payload_len = RF_DRAW_COMMIT_PAYLOAD_LEN;
    AppSlave_Poll();
    AssertAck(RF_DRAW_PHASE_COMMIT, 0U, 0x58U);

    PrimeFrame(RF_MSG_DISPLAY_FLUSH, 0x59U);
    g_received_frame.payload_len = 2U;
    g_received_frame.payload[0] = 0x31U;
    g_received_frame.payload[1] = 1U;
    AppSlave_Poll();
    assert(g_display_flush_called);
    AssertAck(RF_DRAW_PHASE_FLUSH, 0U, 0x59U);

    g_display_flush_called = false;
    PrimeFrame(RF_MSG_DISPLAY_FLUSH, 0x5AU);
    g_received_frame.payload_len = 2U;
    g_received_frame.payload[0] = 0x31U;
    g_received_frame.payload[1] = 1U;
    AppSlave_Poll();
    assert(!g_display_flush_called);
    AssertAck(RF_DRAW_PHASE_FLUSH, 0U, 0x5AU);
}

static void TestDrawTilemapAcceptsExpectedOffsetAndReAcksDuplicate(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_BEGIN, 0x60U);
    g_received_frame.payload_len = RF_DRAW_BEGIN_PAYLOAD_LEN;
    g_decode_begin_result = true;
    g_decoded_begin.session_id = 0x31U;
    g_decoded_begin.flags = 0U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DRAW_TILEMAP, 0x61U);
    g_received_frame.payload_len = RF_DRAW_TILEMAP_FIXED_LEN + 3U;
    g_decode_tilemap_result = true;
    g_decoded_tilemap.session_id = 0x31U;
    g_decoded_tilemap.tile_offset = 0U;
    g_decoded_tilemap.byte_count = 3U;
    g_decoded_tilemap.packed_ids[0] = 0x12U;
    g_decoded_tilemap.packed_ids[1] = 0x34U;
    g_decoded_tilemap.packed_ids[2] = 0x56U;
    AppSlave_Poll();

    assert(g_draw_tilemap_called);
    AssertAck(RF_DRAW_PHASE_TILEMAP, 0U, 0x61U);

    g_draw_tilemap_called = false;
    PrimeFrame(RF_MSG_DRAW_TILEMAP, 0x62U);
    g_received_frame.payload_len = RF_DRAW_TILEMAP_FIXED_LEN + 3U;
    AppSlave_Poll();

    assert(!g_draw_tilemap_called);
    AssertAck(RF_DRAW_PHASE_TILEMAP, 0U, 0x62U);
}

static void TestFlushWithoutCommitErrors(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_DRAW_BEGIN, 0x5BU);
    g_received_frame.payload_len = RF_DRAW_BEGIN_PAYLOAD_LEN;
    g_decode_begin_result = true;
    g_decoded_begin.session_id = 0x31U;
    g_decoded_begin.flags = 0U;
    AppSlave_Poll();

    PrimeFrame(RF_MSG_DISPLAY_FLUSH, 0x5CU);
    g_received_frame.payload_len = 2U;
    g_received_frame.payload[0] = 0x31U;
    g_received_frame.payload[1] = 0U;
    AppSlave_Poll();

    AssertError(RF_DRAW_PHASE_FLUSH, RF_DRAW_ERROR_REASON_BAD_STATE, 0x5CU);
}

static void TestPingStillPongs(void)
{
    ResetFakes();
    PrimeFrame(RF_MSG_PING, 0x5DU);
    g_received_frame.payload_len = 0U;

    AppSlave_Poll();

    assert(g_sent_frame.msg_type == RF_MSG_PONG);
    assert(g_sent_frame.dst_addr == 0x22U);
    assert(g_sent_frame.src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frame.seq == 0x5DU);
}

static void TestButtonPressDebouncesAndSendsAgentTrigger(void)
{
    ResetFakes();
    g_send_frame_result = true;
    g_button_pin_state = GPIO_PIN_RESET;

    AppSlave_Poll();
    assert(g_sent_frame.msg_type == 0U);

    g_tick = 31U;
    AppSlave_Poll();

    assert(g_sent_frame.msg_type == RF_MSG_AGENT_TRIGGER);
    assert(g_sent_frame.dst_addr == OPENPERIPH_MASTER_ADDR);
    assert(g_sent_frame.src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frame.payload_len == 4U);
    assert(g_sent_frame.payload[0] == 1U);
    assert(g_sent_frame.payload[1] == 1U);
    assert(g_led_pin_state == GPIO_PIN_SET);
    assert(g_last_gpio_write_pin == GPIO_PIN_14);
}

static void TestButtonHeldLowOnlyTriggersOnceUntilReleased(void)
{
    ResetFakes();
    g_send_frame_result = true;
    g_button_pin_state = GPIO_PIN_RESET;

    AppSlave_Poll();
    assert(g_sent_frame.msg_type == 0U);

    g_tick = 31U;
    AppSlave_Poll();
    assert(g_sent_frame.msg_type == RF_MSG_AGENT_TRIGGER);
    assert(g_sent_frame.payload[0] == 1U);

    memset(&g_sent_frame, 0, sizeof(g_sent_frame));
    g_tick = 80U;
    AppSlave_Poll();
    assert(g_sent_frame.msg_type == 0U);

    g_button_pin_state = GPIO_PIN_SET;
    AppSlave_Poll();
    g_button_pin_state = GPIO_PIN_RESET;
    g_tick = 120U;
    AppSlave_Poll();
    assert(g_sent_frame.msg_type == 0U);

    g_tick = 151U;
    AppSlave_Poll();

    assert(g_sent_frame.msg_type == RF_MSG_AGENT_TRIGGER);
    assert(g_sent_frame.payload[0] == 2U);
}

static void TestButtonLedPulseTurnsOffAfterDeadline(void)
{
    ResetFakes();
    g_send_frame_result = true;
    g_button_pin_state = GPIO_PIN_RESET;

    AppSlave_Poll();
    assert(g_led_pin_state == GPIO_PIN_RESET);

    g_tick = 31U;
    AppSlave_Poll();
    assert(g_led_pin_state == GPIO_PIN_SET);

    g_tick = 130U;
    AppSlave_Poll();
    assert(g_led_pin_state == GPIO_PIN_SET);

    g_tick = 132U;
    AppSlave_Poll();
    assert(g_led_pin_state == GPIO_PIN_RESET);
}

int main(void)
{
    TestButtonPressDebouncesAndSendsAgentTrigger();
    TestButtonHeldLowOnlyTriggersOnceUntilReleased();
    TestButtonLedPulseTurnsOffAfterDeadline();
    TestDrawBeginClearsAndAcks();
    TestDuplicateBeginIsReAcked();
    TestDrawTextAcceptsExpectedOpAndReAcksDuplicate();
    TestDrawTilemapAcceptsExpectedOffsetAndReAcksDuplicate();
    TestOutOfOrderTextErrors();
    TestCommitAndFlushAreIdempotent();
    TestFlushWithoutCommitErrors();
    TestPingStillPongs();
    return 0;
}
