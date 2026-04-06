#include "epd_port.h"

#include "DEV_Config.h"
#include "openperiph_board.h"

extern SPI_HandleTypeDef hspi1;

void EpdPort_Init(void)
{
    HAL_GPIO_WritePin(OpenPeriph_EpdCsPort(), OpenPeriph_EpdCsPin(), GPIO_PIN_SET);
    HAL_GPIO_WritePin(OpenPeriph_EpdDcPort(), OpenPeriph_EpdDcPin(), GPIO_PIN_SET);
    HAL_GPIO_WritePin(OpenPeriph_EpdRstPort(), OpenPeriph_EpdRstPin(), GPIO_PIN_SET);
}

void EpdPort_Exit(void)
{
    HAL_GPIO_WritePin(OpenPeriph_EpdCsPort(), OpenPeriph_EpdCsPin(), GPIO_PIN_SET);
}

void EpdPort_WriteDigital(GPIO_TypeDef *port, uint16_t pin, uint8_t value)
{
    HAL_GPIO_WritePin(port, pin, value == 0U ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

uint8_t EpdPort_ReadDigital(GPIO_TypeDef *port, uint16_t pin)
{
    return (uint8_t)HAL_GPIO_ReadPin(port, pin);
}

void EpdPort_DelayMs(uint32_t delay_ms)
{
    HAL_Delay(delay_ms);
}

void EpdPort_SpiWriteByte(uint8_t value)
{
    (void)HAL_SPI_Transmit(&hspi1, &value, 1U, HAL_MAX_DELAY);
}

void EpdPort_SpiWriteBuffer(const uint8_t *buffer, size_t length)
{
    if ((buffer == NULL) || (length == 0U)) {
        return;
    }

    (void)HAL_SPI_Transmit(&hspi1, (uint8_t *)buffer, (uint16_t)length, HAL_MAX_DELAY);
}

void DEV_SPI_WriteByte(UBYTE value)
{
    EpdPort_SpiWriteByte(value);
}

void DEV_SPI_Write_nByte(const UBYTE *buffer, UDOUBLE length)
{
    EpdPort_SpiWriteBuffer(buffer, (size_t)length);
}

int DEV_Module_Init(void)
{
    EpdPort_Init();
    DEV_Digital_Write(EPD_DC_PIN, 0U);
    DEV_Digital_Write(EPD_CS_PIN, 1U);
    DEV_Digital_Write(EPD_RST_PIN, 1U);
    return 0;
}

void DEV_Module_Exit(void)
{
    DEV_Digital_Write(EPD_DC_PIN, 0U);
    DEV_Digital_Write(EPD_CS_PIN, 1U);
    DEV_Digital_Write(EPD_RST_PIN, 0U);
    EpdPort_Exit();
}
