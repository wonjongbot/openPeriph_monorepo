#include "openperiph_board.h"

void OpenPeriph_BoardInit(void)
{
}

GPIO_TypeDef *OpenPeriph_RfCsPort(void) { return GPIOC; }
uint16_t OpenPeriph_RfCsPin(void) { return GPIO_PIN_2; }
GPIO_TypeDef *OpenPeriph_RfGdo0Port(void) { return GPIOC; }
uint16_t OpenPeriph_RfGdo0Pin(void) { return GPIO_PIN_0; }
GPIO_TypeDef *OpenPeriph_RfGdo2Port(void) { return GPIOC; }
uint16_t OpenPeriph_RfGdo2Pin(void) { return GPIO_PIN_1; }
GPIO_TypeDef *OpenPeriph_EpdCsPort(void) { return GPIOB; }
uint16_t OpenPeriph_EpdCsPin(void) { return GPIO_PIN_0; }
GPIO_TypeDef *OpenPeriph_EpdDcPort(void) { return GPIOB; }
uint16_t OpenPeriph_EpdDcPin(void) { return GPIO_PIN_1; }
GPIO_TypeDef *OpenPeriph_EpdRstPort(void) { return GPIOB; }
uint16_t OpenPeriph_EpdRstPin(void) { return GPIO_PIN_2; }
GPIO_TypeDef *OpenPeriph_EpdBusyPort(void) { return GPIOB; }
uint16_t OpenPeriph_EpdBusyPin(void) { return GPIO_PIN_10; }
