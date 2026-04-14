#ifndef RF_LINK_H
#define RF_LINK_H

#include <stdbool.h>

#include "rf_frame.h"

typedef enum {
    RF_LINK_PING_RESULT_OK = 0,
    RF_LINK_PING_RESULT_SEND_FAIL,
    RF_LINK_PING_RESULT_TIMEOUT,
} RfLinkPingResult_t;

bool RfLink_Init(void);
bool RfLink_SendFrame(const RfFrame_t *frame);
bool RfLink_TryReceiveFrame(RfFrame_t *out_frame);
bool RfLink_IsForLocalNode(const RfFrame_t *frame);
RfLinkPingResult_t RfLink_SendPingAndWaitForPong(uint8_t dst_addr, uint8_t seq);

#endif /* RF_LINK_H */
