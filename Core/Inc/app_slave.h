#ifndef APP_SLAVE_H
#define APP_SLAVE_H

#include "app_protocol.h"
#include "display_service.h"
#include "rf_link.h"
#include "rf_draw_protocol.h"

#include <string.h>

typedef struct {
    bool active;
    uint16_t x;
    uint16_t y;
    uint8_t font_id;
    uint8_t flags;
    uint8_t total_text_len;
    uint8_t staged_len;
    uint8_t next_chunk_index;
    uint8_t last_chunk_index;
    uint8_t last_chunk_len;
    bool has_last_chunk;
    uint8_t text[APP_TEXT_MAX_LEN];
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

static inline void AppSlave_HandleDrawStart(const RfFrame_t *frame)
{
    RfDrawStart_t start;

    AppSlave_ClearDrawState();

    if (!RfDrawProtocol_DecodeStart(frame->payload, frame->payload_len, &start)) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_START, RF_DRAW_ERROR_REASON_LENGTH);
        return;
    }
    if ((start.total_text_len == 0U) || (start.total_text_len > APP_TEXT_MAX_LEN)) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_START, RF_DRAW_ERROR_REASON_LENGTH);
        return;
    }

    g_app_slave_draw_state.active = true;
    g_app_slave_draw_state.x = start.x;
    g_app_slave_draw_state.y = start.y;
    g_app_slave_draw_state.font_id = start.font_id;
    g_app_slave_draw_state.flags = start.flags;
    g_app_slave_draw_state.total_text_len = start.total_text_len;
    AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_START, 0U);
}

static inline void AppSlave_HandleDrawChunk(const RfFrame_t *frame)
{
    RfDrawChunk_t chunk;
    size_t last_chunk_offset;

    if (!g_app_slave_draw_state.active) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_CHUNK, RF_DRAW_ERROR_REASON_BAD_STATE);
        return;
    }
    if (!RfDrawProtocol_DecodeChunk(frame->payload, frame->payload_len, &chunk)) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_CHUNK, RF_DRAW_ERROR_REASON_BAD_CHUNK);
        return;
    }
    if (g_app_slave_draw_state.has_last_chunk &&
        (chunk.chunk_index == g_app_slave_draw_state.last_chunk_index)) {
        last_chunk_offset =
            (size_t)g_app_slave_draw_state.staged_len - (size_t)g_app_slave_draw_state.last_chunk_len;
        if ((chunk.chunk_len != g_app_slave_draw_state.last_chunk_len) ||
            ((chunk.chunk_len > 0U) &&
             (memcmp(&g_app_slave_draw_state.text[last_chunk_offset], chunk.data, chunk.chunk_len) != 0))) {
            AppSlave_SendDrawError(frame, RF_DRAW_PHASE_CHUNK, RF_DRAW_ERROR_REASON_BAD_CHUNK);
            return;
        }
        AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_CHUNK, chunk.chunk_index);
        return;
    }
    if (chunk.chunk_index != g_app_slave_draw_state.next_chunk_index) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_CHUNK, RF_DRAW_ERROR_REASON_BAD_CHUNK);
        return;
    }
    if ((size_t)g_app_slave_draw_state.staged_len + (size_t)chunk.chunk_len >
        (size_t)g_app_slave_draw_state.total_text_len) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_CHUNK, RF_DRAW_ERROR_REASON_LENGTH);
        return;
    }

    if (chunk.chunk_len > 0U) {
        memcpy(&g_app_slave_draw_state.text[g_app_slave_draw_state.staged_len],
               chunk.data,
               chunk.chunk_len);
    }
    g_app_slave_draw_state.staged_len =
        (uint8_t)(g_app_slave_draw_state.staged_len + chunk.chunk_len);
    g_app_slave_draw_state.last_chunk_index = chunk.chunk_index;
    g_app_slave_draw_state.last_chunk_len = chunk.chunk_len;
    g_app_slave_draw_state.has_last_chunk = true;
    g_app_slave_draw_state.next_chunk_index = (uint8_t)(chunk.chunk_index + 1U);
    AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_CHUNK, chunk.chunk_index);
}

static inline void AppSlave_HandleDrawCommit(const RfFrame_t *frame)
{
    AppDrawTextCommand_t cmd = {0};
    bool draw_ok;

    if (!g_app_slave_draw_state.active) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_COMMIT, RF_DRAW_ERROR_REASON_BAD_STATE);
        AppSlave_ClearDrawState();
        return;
    }
    if (g_app_slave_draw_state.staged_len != g_app_slave_draw_state.total_text_len) {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_COMMIT, RF_DRAW_ERROR_REASON_LENGTH);
        AppSlave_ClearDrawState();
        return;
    }

    cmd.dst_addr = OPENPERIPH_NODE_ADDR;
    cmd.x = g_app_slave_draw_state.x;
    cmd.y = g_app_slave_draw_state.y;
    cmd.font_id = g_app_slave_draw_state.font_id;
    cmd.flags = g_app_slave_draw_state.flags;
    cmd.text_len = g_app_slave_draw_state.total_text_len;
    if (cmd.text_len > 0U) {
        memcpy(cmd.text, g_app_slave_draw_state.text, cmd.text_len);
    }

    draw_ok = DisplayService_DrawText(&cmd);
    if (draw_ok) {
        AppSlave_SendDrawAck(frame, RF_DRAW_PHASE_COMMIT, 0U);
    } else {
        AppSlave_SendDrawError(frame, RF_DRAW_PHASE_COMMIT, RF_DRAW_ERROR_REASON_RENDER);
    }
    AppSlave_ClearDrawState();
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
    case RF_MSG_DRAW_START:
        AppSlave_HandleDrawStart(&frame);
        break;

    case RF_MSG_DRAW_CHUNK:
        AppSlave_HandleDrawChunk(&frame);
        break;

    case RF_MSG_DRAW_COMMIT:
        AppSlave_HandleDrawCommit(&frame);
        break;

    case RF_MSG_PING:
    case RF_MSG_DRAW_ACK:
    case RF_MSG_DRAW_ERROR:
    default:
        break;
    }
}

#endif /* APP_SLAVE_H */
