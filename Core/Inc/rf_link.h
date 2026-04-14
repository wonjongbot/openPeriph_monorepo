#ifndef RF_LINK_H
#define RF_LINK_H

#include <stdbool.h>

#include "rf_frame.h"

typedef enum {
    RF_LINK_PING_RESULT_OK = 0,
    RF_LINK_PING_RESULT_SEND_FAIL,
    RF_LINK_PING_RESULT_TIMEOUT,
} RfLinkPingResult_t;

typedef struct {
    uint8_t attempts_used;
    uint8_t retries_used;
    uint16_t elapsed_ms;
    uint8_t remote_phase;
    uint8_t remote_reason;
} RfLinkExchangeStats_t;

#define RF_LINK_MAX_RETRIES 8U
#define RF_LINK_ATTEMPT_TIMEOUT_MS 75U
#define RF_LINK_PING_TOTAL_TIMEOUT_MS 600U
#define RF_LINK_DRAW_TOTAL_TIMEOUT_MS 2000U

bool RfLink_Init(void);
bool RfLink_SendFrame(const RfFrame_t *frame);
bool RfLink_TryReceiveFrame(RfFrame_t *out_frame);
bool RfLink_IsForLocalNode(const RfFrame_t *frame);
RfLinkPingResult_t RfLink_SendPingAndWaitForPong(uint8_t dst_addr,
                                                 uint8_t seq,
                                                 RfLinkExchangeStats_t *out_stats);

#endif /* RF_LINK_H */
