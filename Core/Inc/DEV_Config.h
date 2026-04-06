#ifndef DEV_CONFIG_H
#define DEV_CONFIG_H

#include "epd_port.h"
#include "openperiph_config.h"
#include "openperiph_board.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

#define EPD_RST_PIN  OpenPeriph_EpdRstPort(), OpenPeriph_EpdRstPin()
#define EPD_DC_PIN   OpenPeriph_EpdDcPort(), OpenPeriph_EpdDcPin()
#define EPD_CS_PIN   OpenPeriph_EpdCsPort(), OpenPeriph_EpdCsPin()
#define EPD_BUSY_PIN OpenPeriph_EpdBusyPort(), OpenPeriph_EpdBusyPin()

#define DEV_Digital_Write(_pin, _value) EpdPort_WriteDigital(_pin, _value)
#define DEV_Digital_Read(_pin) EpdPort_ReadDigital(_pin)
#define DEV_Delay_ms(__xms) EpdPort_DelayMs(__xms)
#define DEV_BUSY_TIMEOUT_MS OPENPERIPH_EPD_BUSY_TIMEOUT_MS

void DEV_SPI_WriteByte(UBYTE value);
void DEV_SPI_Write_nByte(const UBYTE *buffer, UDOUBLE length);
int DEV_Module_Init(void);
void DEV_Module_Exit(void);

#endif
