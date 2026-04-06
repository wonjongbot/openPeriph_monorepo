#ifndef CC1101_RADIO_H
#define CC1101_RADIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CC1101_RADIO_MAX_PACKET_LEN 61U

bool Cc1101Radio_Init(void);
bool Cc1101Radio_Reset(void);
bool Cc1101Radio_Send(const uint8_t *payload, uint8_t length);
bool Cc1101Radio_Receive(uint8_t *payload, uint8_t *in_out_length);
bool Cc1101Radio_EnterRx(void);
uint8_t Cc1101Radio_GetMarcState(void);

#endif /* CC1101_RADIO_H */
