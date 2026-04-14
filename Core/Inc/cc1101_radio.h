#ifndef CC1101_RADIO_H
#define CC1101_RADIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CC1101_RADIO_MAX_PACKET_LEN 61U
#define CC1101_RADIO_STATE_RX 0x0DU
#define CC1101_RADIO_STATE_RXFIFO_OVERFLOW 0x11U
#define CC1101_RADIO_STATE_TXFIFO_UNDERFLOW 0x16U
#define CC1101_DEFAULT_FREQ2 0x0CU
#define CC1101_DEFAULT_FREQ1 0x1DU
#define CC1101_DEFAULT_FREQ0 0x8AU
#define CC1101_DEFAULT_TEST0 0x0BU
#define CC1101_DEFAULT_PATABLE_ENTRY 0xC0U

bool Cc1101Radio_Init(void);
bool Cc1101Radio_Reset(void);
bool Cc1101Radio_Send(const uint8_t *payload, uint8_t length);
bool Cc1101Radio_Receive(uint8_t *payload, uint8_t *in_out_length);
bool Cc1101Radio_EnterRx(void);
bool Cc1101Radio_RecoverRx(void);
uint8_t Cc1101Radio_GetMarcState(void);
bool Cc1101Radio_ReadChipInfo(uint8_t *partnum, uint8_t *version);

#endif /* CC1101_RADIO_H */
