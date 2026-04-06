#ifndef EPD_PORT_H
#define EPD_PORT_H

#include "main.h"
#include <stddef.h>
#include <stdint.h>

void EpdPort_Init(void);
void EpdPort_Exit(void);
void EpdPort_WriteDigital(GPIO_TypeDef *port, uint16_t pin, uint8_t value);
uint8_t EpdPort_ReadDigital(GPIO_TypeDef *port, uint16_t pin);
void EpdPort_DelayMs(uint32_t delay_ms);
void EpdPort_SpiWriteByte(uint8_t value);
void EpdPort_SpiWriteBuffer(const uint8_t *buffer, size_t length);

#endif
