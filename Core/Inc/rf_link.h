#ifndef RF_LINK_H
#define RF_LINK_H

#include <stdbool.h>

#include "rf_frame.h"

void RfLink_Init(void);
bool RfLink_SendFrame(const RfFrame_t *frame);
bool RfLink_TryReceiveFrame(RfFrame_t *out_frame);
bool RfLink_IsForLocalNode(const RfFrame_t *frame);

#endif /* RF_LINK_H */
