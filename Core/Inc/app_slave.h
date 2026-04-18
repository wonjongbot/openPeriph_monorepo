#ifndef APP_SLAVE_H
#define APP_SLAVE_H

#include "app_protocol.h"
#include "display_service.h"
#include "rf_link.h"
#include "rf_draw_protocol.h"

#include <string.h>

typedef struct {
    bool has_session;
    bool accepting_text;
    bool committed;
    bool flushed;
    uint8_t session_id;
    uint8_t begin_flags;
    uint8_t next_op_index;
    uint8_t last_op_index;
    bool has_last_text;
    AppDrawTextCommand_t last_text;
} AppSlaveDrawState_t;

static AppSlaveDrawState_t g_app_slave_draw_state;

static inline void AppSlave_ClearDrawState(void)
{
    memset(&g_app_slave_draw_state, 0, sizeof(g_app_slave_draw_state));
}

static inline void AppSlave_SendDrawAck(const RfFrame_t *frame, uint8_t phase, uint8_t value)
{
    RfFrame_t response = {0};
    RfDrawAck_t ack = {
        .phase = phase,
        .value = value,
    };

    response.version = RF_FRAME_VERSION;
    response.msg_type = RF_MSG_DRAW_ACK;
    response.dst_addr = frame->src_addr;
    response.src_addr = OPENPERIPH_NODE_ADDR;
    response.seq = frame->seq;
    response.payload_len = (uint8_t)RfDrawProtocol_EncodeAck(&ack, response.payload, sizeof(response.payload));
    (void)RfLink_SendFrame(&response);
}

static inline void AppSlave_SendDrawError(const RfFrame_t *frame, uint8_t phase, uint8_t reason)
{
    RfFrame_t response = {0};
    RfDrawError_t error = {
        .phase = phase,
        .reason = reason,
    };

    response.version = RF_FRAME_VERSION;
    response.msg_type = RF_MSG_DRAW_ERROR;
    response.dst_addr = frame->src_addr;
    response.src_addr = OPENPERIPH_NODE_ADDR;
    response.seq = frame->seq;
    response.payload_len = (uint8_t)RfDrawProtocol_EncodeError(&error, response.payload, sizeof(response.payload));
    (void)RfLink_SendFrame(&response);
}

static inline bool AppSlave_IsDuplicateText(const RfDrawText_t *text)
{
    if ((text == NULL) || !g_app_slave_draw_state.has_last_text) {
        return false;
    }

    return (g_app_slave_draw_state.last_op_index == text->op_index) &&
           (g_app_slave_draw_state.last_text.session_id == text->session_id) &&
           (g_app_slave_draw_state.last_text.op_index == text->op_index) &&
           (g_app_slave_draw_state.last_text.x == text->x) &&
           (g_app_slave_draw_state.last_text.y == text->y) &&
           (g_app_slave_draw_state.last_text.font_id == text->font_id) &&
           (g_app_slave_draw_state.last_text.text_len == text->text_len) &&
           ((text->text_len == 0U) ||
            (memcmp(g_app_slave_draw_state.last_text.text, text->text, text->text_len) == 0));
}

static inline void AppSlave_HandleDrawBegin(const RfFrame_t *frame)
{
    RfDrawBegin_t begin;

    if (!RfDrawProtocol_DecodeBegin(frame->payload, frame->payload_len, &begin)) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_BEGIN, RF_DRAW_ERROR_REASON_LENGTH);
        return;
    }

    if (g_app_slave_draw_state.has_session &&
        (g_app_slave_draw_state.session_id == begin.session_id)) {
        if (g_app_slave_draw_state.begin_flags != begin.flags) {
            AppSlave_SendDrawError(frame, RF_DRAW_PHASE_BEGIN, RF_DRAW_ERROR_REASON_BAD_STATE);
            return;
        }
        AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_BEGIN, 0U);
        return;
    }

    AppSlave_ClearDrawState();
    if ((begin.flags & APP_DRAW_FLAG_CLEAR_FIRST) != 0U) {
        if (!DisplayService_ClearBuffer()) {
            AppSlave_SendDrawError(frame, RF_DRAW_PHASE_BEGIN, RF_DRAW_ERROR_REASON_RENDER);
            return;
        }
    }

    g_app_slave_draw_state.has_session = true;
    g_app_slave_draw_state.accepting_text = true;
    g_app_slave_draw_state.session_id = begin.session_id;
    g_app_slave_draw_state.begin_flags = begin.flags;
    AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_BEGIN, 0U);
}

static inline void AppSlave_HandleDrawText(const RfFrame_t *frame)
{
    AppDrawTextCommand_t cmd = {0};
    RfDrawText_t text;

    if (!g_app_slave_draw_state.has_session ||
        !g_app_slave_draw_state.accepting_text ||
        g_app_slave_draw_state.committed) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_TEXT, RF_DRAW_ERROR_REASON_BAD_STATE);
        return;
    }
    if (!RfDrawProtocol_DecodeText(frame->payload, frame->payload_len, &text)) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_TEXT, RF_DRAW_ERROR_REASON_LENGTH);
        return;
    }
    if (text.session_id != g_app_slave_draw_state.session_id) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_TEXT, RF_DRAW_ERROR_REASON_BAD_STATE);
        return;
    }
    if (AppSlave_IsDuplicateText(&text)) {
        AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_TEXT, text.op_index);
        return;
    }
    if (text.op_index != g_app_slave_draw_state.next_op_index) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_TEXT, RF_DRAW_ERROR_REASON_BAD_CHUNK);
        return;
    }

    cmd.dst_addr = OPENPERIPH_NODE_ADDR;
    cmd.session_id = text.session_id;
    cmd.op_index = text.op_index;
    cmd.x = text.x;
    cmd.y = text.y;
    cmd.font_id = text.font_id;
    cmd.text_len = text.text_len;
    if (cmd.text_len > 0U) {
        memcpy(cmd.text, text.text, cmd.text_len);
    }

    if (!DisplayService_DrawText(&cmd)) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_TEXT, RF_DRAW_ERROR_REASON_RENDER);
        return;
    }

    g_app_slave_draw_state.last_text = cmd;
    g_app_slave_draw_state.has_last_text = true;
    g_app_slave_draw_state.last_op_index = text.op_index;
    g_app_slave_draw_state.next_op_index = (uint8_t)(text.op_index + 1U);
    AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_TEXT, text.op_index);
}

static inline void AppSlave_HandleDisplayFlush(const RfFrame_t *frame)
{
    uint8_t session_id;
    bool full_refresh;

    if (frame->payload_len != 2U) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_FLUSH, RF_DRAW_ERROR_REASON_LENGTH);
        return;
    }

    session_id = frame->payload[0];
    full_refresh = (frame->payload[1] != 0U);
    if (!g_app_slave_draw_state.has_session ||
        (session_id != g_app_slave_draw_state.session_id) ||
        !g_app_slave_draw_state.committed) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_FLUSH, RF_DRAW_ERROR_REASON_BAD_STATE);
        return;
    }
    if (g_app_slave_draw_state.flushed) {
        AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_FLUSH, 0U);
        return;
    }

    if (DisplayService_Flush(full_refresh)) {
        g_app_slave_draw_state.flushed = true;
        g_app_slave_draw_state.accepting_text = false;
        AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_FLUSH, 0U);
    } else {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_FLUSH, RF_DRAW_ERROR_REASON_RENDER);
    }
}

static inline void AppSlave_HandleDrawCommit(const RfFrame_t *frame)
{
    RfDrawCommit_t commit;

    if (!RfDrawProtocol_DecodeCommit(frame->payload, frame->payload_len, &commit)) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_COMMIT, RF_DRAW_ERROR_REASON_LENGTH);
        return;
    }
    if (!g_app_slave_draw_state.has_session ||
        (commit.session_id != g_app_slave_draw_state.session_id)) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_COMMIT, RF_DRAW_ERROR_REASON_BAD_STATE);
        return;
    }
    if (g_app_slave_draw_state.committed) {
        AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_COMMIT, 0U);
        return;
    }

    g_app_slave_draw_state.committed = true;
    g_app_slave_draw_state.accepting_text = false;
    AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_COMMIT, 0U);
}

static inline void AppSlave_Init(void)
{
    AppSlave_ClearDrawState();
    (void)DisplayService_Init();
}

static inline void AppSlave_Poll(void)
{
    RfFrame_t frame;

    if (!RfLink_TryReceiveFrame(&frame)) {
        return;
    }
    if (!RfLink_IsForLocalNode(&frame)) {
        return;
    }

    if ((frame.msg_type == RF_MSG_PING) && (frame.payload_len == 0U)) {
        RfFrame_t response = {
            .version = RF_FRAME_VERSION,
            .msg_type = RF_MSG_PONG,
            .dst_addr = frame.src_addr,
            .src_addr = OPENPERIPH_NODE_ADDR,
            .seq = frame.seq,
            .payload_len = 0U,
        };

        (void)RfLink_SendFrame(&response);
        return;
    }

    switch (frame.msg_type) {
    case RF_MSG_DRAW_BEGIN:
        AppSlave_HandleDrawBegin(&frame);
        break;

    case RF_MSG_DRAW_TEXT:
        AppSlave_HandleDrawText(&frame);
        break;

    case RF_MSG_DRAW_COMMIT:
        AppSlave_HandleDrawCommit(&frame);
        break;

    case RF_MSG_DISPLAY_FLUSH:
        AppSlave_HandleDisplayFlush(&frame);
        break;

    case RF_MSG_PING:
    case RF_MSG_DRAW_ACK:
    case RF_MSG_DRAW_ERROR:
    default:
        break;
    }
}

#endif /* APP_SLAVE_H */
