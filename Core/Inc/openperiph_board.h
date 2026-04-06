#ifndef OPENPERIPH_BOARD_H
#define OPENPERIPH_BOARD_H

#include "main.h"

extern SPI_HandleTypeDef hspi1;

void OpenPeriph_BoardInit(void);

GPIO_TypeDef *OpenPeriph_RfCsPort(void);
uint16_t OpenPeriph_RfCsPin(void);
GPIO_TypeDef *OpenPeriph_RfGdo0Port(void);
uint16_t OpenPeriph_RfGdo0Pin(void);
GPIO_TypeDef *OpenPeriph_RfGdo2Port(void);
uint16_t OpenPeriph_RfGdo2Pin(void);
GPIO_TypeDef *OpenPeriph_EpdCsPort(void);
uint16_t OpenPeriph_EpdCsPin(void);
GPIO_TypeDef *OpenPeriph_EpdDcPort(void);
uint16_t OpenPeriph_EpdDcPin(void);
GPIO_TypeDef *OpenPeriph_EpdRstPort(void);
uint16_t OpenPeriph_EpdRstPin(void);
GPIO_TypeDef *OpenPeriph_EpdBusyPort(void);
uint16_t OpenPeriph_EpdBusyPin(void);

#endif
