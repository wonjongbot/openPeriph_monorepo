#include "openperiph_board.h"

void OpenPeriph_BoardInit(void)
{
    HAL_GPIO_WritePin(OpenPeriph_RfCsPort(), OpenPeriph_RfCsPin(), GPIO_PIN_SET);
    HAL_GPIO_WritePin(OpenPeriph_EpdCsPort(), OpenPeriph_EpdCsPin(), GPIO_PIN_SET);
    HAL_GPIO_WritePin(OpenPeriph_EpdDcPort(), OpenPeriph_EpdDcPin(), GPIO_PIN_RESET);
    HAL_GPIO_WritePin(OpenPeriph_EpdRstPort(), OpenPeriph_EpdRstPin(), GPIO_PIN_SET);
}

GPIO_TypeDef *OpenPeriph_RfCsPort(void) { return GPIOC; }
uint16_t OpenPeriph_RfCsPin(void) { return GPIO_PIN_2; }
GPIO_TypeDef *OpenPeriph_RfMisoPort(void) { return GPIOA; }
uint16_t OpenPeriph_RfMisoPin(void) { return GPIO_PIN_6; }
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
