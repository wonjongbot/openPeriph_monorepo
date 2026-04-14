#ifndef APP_SLAVE_H
#define APP_SLAVE_H

#include "app_protocol.h"
#include "display_service.h"
#include "rf_link.h"

static inline void AppSlave_Init(void)
{
    (void)DisplayService_Init();
}

static inline void AppSlave_Poll(void)
{
    RfFrame_t frame;
    AppDrawTextCommand_t cmd;

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
    if (frame.msg_type != RF_MSG_DRAW_START) {
        return;
    }
    if (!AppProtocol_DecodeDrawText(frame.payload, frame.payload_len, &cmd)) {
        return;
    }

    (void)DisplayService_DrawText(&cmd);
}

#endif /* APP_SLAVE_H */
