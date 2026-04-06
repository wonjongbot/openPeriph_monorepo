#include "cc1101_radio.h"

#include "openperiph_board.h"
#include "openperiph_config.h"

#define CC1101_REG_IOCFG2      0x00U
#define CC1101_REG_IOCFG1      0x01U
#define CC1101_REG_IOCFG0      0x02U
#define CC1101_REG_FIFOTHR     0x03U
#define CC1101_REG_SYNC1       0x04U
#define CC1101_REG_SYNC0       0x05U
#define CC1101_REG_PKTLEN      0x06U
#define CC1101_REG_PKTCTRL1    0x07U
#define CC1101_REG_PKTCTRL0    0x08U
#define CC1101_REG_ADDR        0x09U
#define CC1101_REG_CHANNR      0x0AU
#define CC1101_REG_FSCTRL1     0x0BU
#define CC1101_REG_FSCTRL0     0x0CU
#define CC1101_REG_FREQ2       0x0DU
#define CC1101_REG_FREQ1       0x0EU
#define CC1101_REG_FREQ0       0x0FU
#define CC1101_REG_MDMCFG4     0x10U
#define CC1101_REG_MDMCFG3     0x11U
#define CC1101_REG_MDMCFG2     0x12U
#define CC1101_REG_MDMCFG1     0x13U
#define CC1101_REG_MDMCFG0     0x14U
#define CC1101_REG_DEVIATN     0x15U
#define CC1101_REG_MCSM2       0x16U
#define CC1101_REG_MCSM1       0x17U
#define CC1101_REG_MCSM0       0x18U
#define CC1101_REG_FOCCFG      0x19U
#define CC1101_REG_BSCFG       0x1AU
#define CC1101_REG_AGCCTRL2    0x1BU
#define CC1101_REG_AGCCTRL1    0x1CU
#define CC1101_REG_AGCCTRL0    0x1DU
#define CC1101_REG_WOREVT1     0x1EU
#define CC1101_REG_WOREVT0     0x1FU
#define CC1101_REG_WORCTRL     0x20U
#define CC1101_REG_FREND1      0x21U
#define CC1101_REG_FREND0      0x22U
#define CC1101_REG_FSCAL3      0x23U
#define CC1101_REG_FSCAL2      0x24U
#define CC1101_REG_FSCAL1      0x25U
#define CC1101_REG_FSCAL0      0x26U
#define CC1101_REG_RCCTRL1     0x27U
#define CC1101_REG_RCCTRL0     0x28U
#define CC1101_REG_FSTEST      0x29U
#define CC1101_REG_PTEST       0x2AU
#define CC1101_REG_AGCTEST     0x2BU
#define CC1101_REG_TEST2       0x2CU
#define CC1101_REG_TEST1       0x2DU
#define CC1101_REG_TEST0       0x2EU
#define CC1101_REG_MARCSTATE   0x35U
#define CC1101_REG_RXBYTES     0x3BU
#define CC1101_REG_PATABLE     0x3EU
#define CC1101_REG_FIFO        0x3FU

#define CC1101_STROBE_SRES     0x30U
#define CC1101_STROBE_SRX      0x34U
#define CC1101_STROBE_STX      0x35U
#define CC1101_STROBE_SIDLE    0x36U
#define CC1101_STROBE_SFRX     0x3AU
#define CC1101_STROBE_SFTX     0x3BU

#define CC1101_WRITE_BURST     0x40U
#define CC1101_READ_SINGLE     0x80U
#define CC1101_READ_BURST      0xC0U

#define CC1101_STATE_IDLE              0x01U
#define CC1101_STATE_RX                0x0DU
#define CC1101_STATE_TXFIFO_UNDERFLOW  0x16U

#define CC1101_STATUS_STATE_MASK       0x1FU
#define CC1101_STATUS_RX_BYTES_MASK    0x7FU
#define CC1101_STATUS_RX_OVERFLOW      0x80U
#define CC1101_STATUS_CRC_OK           0x80U

#define CC1101_MISO_TIMEOUT_MS         50U
#define CC1101_IO_TIMEOUT_MS           20U
#define CC1101_TX_TIMEOUT_MS           100U

static const uint8_t kCc1101PaTable[8] = {0xC0U, 0xC0U, 0xC0U, 0xC0U, 0xC0U, 0xC0U, 0xC0U, 0xC0U};
static bool s_radio_initialized = false;

static GPIO_TypeDef *Cc1101_CsPort(void)
{
    return OpenPeriph_RfCsPort();
}

static uint16_t Cc1101_CsPin(void)
{
    return OpenPeriph_RfCsPin();
}

static bool Cc1101_WaitForSpiReady(void)
{
    const uint32_t start_tick = HAL_GetTick();

    while (HAL_GPIO_ReadPin(OpenPeriph_RfMisoPort(), OpenPeriph_RfMisoPin()) != GPIO_PIN_RESET) {
        if ((HAL_GetTick() - start_tick) >= CC1101_MISO_TIMEOUT_MS) {
            return false;
        }
    }

    return true;
}

static HAL_StatusTypeDef Cc1101_Select(void)
{
    HAL_GPIO_WritePin(Cc1101_CsPort(), Cc1101_CsPin(), GPIO_PIN_RESET);
    if (!Cc1101_WaitForSpiReady()) {
        HAL_GPIO_WritePin(Cc1101_CsPort(), Cc1101_CsPin(), GPIO_PIN_SET);
        return HAL_TIMEOUT;
    }

    return HAL_OK;
}

static void Cc1101_Deselect(void)
{
    HAL_GPIO_WritePin(Cc1101_CsPort(), Cc1101_CsPin(), GPIO_PIN_SET);
}

static HAL_StatusTypeDef Cc1101_Write(const uint8_t *header, uint16_t header_len,
                                      const uint8_t *payload, uint16_t payload_len)
{
    HAL_StatusTypeDef status = Cc1101_Select();
    if (status != HAL_OK) {
        return status;
    }

    status = HAL_SPI_Transmit(&hspi1, (uint8_t *)header, header_len, HAL_MAX_DELAY);
    if (status == HAL_OK && payload != NULL && payload_len > 0U) {
        status = HAL_SPI_Transmit(&hspi1, (uint8_t *)payload, payload_len, HAL_MAX_DELAY);
    }

    Cc1101_Deselect();
    return status;
}

static HAL_StatusTypeDef Cc1101_Read(uint8_t header, uint8_t *payload, uint16_t payload_len)
{
    HAL_StatusTypeDef status = Cc1101_Select();
    if (status != HAL_OK) {
        return status;
    }

    status = HAL_SPI_Transmit(&hspi1, &header, 1U, HAL_MAX_DELAY);
    if (status == HAL_OK && payload != NULL && payload_len > 0U) {
        status = HAL_SPI_Receive(&hspi1, payload, payload_len, HAL_MAX_DELAY);
    }

    Cc1101_Deselect();
    return status;
}

static bool Cc1101_WriteReg(uint8_t addr, uint8_t value)
{
    return Cc1101_Write(&addr, 1U, &value, 1U) == HAL_OK;
}

static bool Cc1101_WriteBurst(uint8_t addr, const uint8_t *payload, uint8_t length)
{
    const uint8_t header = (uint8_t)(addr | CC1101_WRITE_BURST);
    return Cc1101_Write(&header, 1U, payload, length) == HAL_OK;
}

static bool Cc1101_Strobe(uint8_t strobe)
{
    return Cc1101_Write(&strobe, 1U, NULL, 0U) == HAL_OK;
}

static bool Cc1101_ReadSingle(uint8_t addr, uint8_t *value)
{
    uint8_t header = (uint8_t)(addr | CC1101_READ_SINGLE);
    return Cc1101_Read(header, value, 1U) == HAL_OK;
}

static bool Cc1101_ReadBurst(uint8_t addr, uint8_t *payload, uint8_t length)
{
    uint8_t header = (uint8_t)(addr | CC1101_READ_BURST);
    return Cc1101_Read(header, payload, length) == HAL_OK;
}

static uint8_t Cc1101_ReadStableStatus(uint8_t addr)
{
    uint8_t first = 0U;
    uint8_t second = 0U;

    for (uint8_t attempt = 0U; attempt < 4U; ++attempt) {
        if (!Cc1101_ReadBurst(addr, &first, 1U)) {
            return second;
        }
        if (!Cc1101_ReadBurst(addr, &second, 1U)) {
            return second;
        }
        if (first == second) {
            return second;
        }
    }

    return second;
}

static uint8_t Cc1101_ReadRxBytesRaw(void)
{
    return Cc1101_ReadStableStatus(CC1101_REG_RXBYTES);
}

static void Cc1101_WriteSettings(void)
{
    (void)Cc1101_WriteReg(CC1101_REG_IOCFG2, 0x29U);
    (void)Cc1101_WriteReg(CC1101_REG_IOCFG1, 0x2EU);
    (void)Cc1101_WriteReg(CC1101_REG_IOCFG0, 0x06U);
    (void)Cc1101_WriteReg(CC1101_REG_FIFOTHR, 0x47U);
    (void)Cc1101_WriteReg(CC1101_REG_SYNC1, 0xD3U);
    (void)Cc1101_WriteReg(CC1101_REG_SYNC0, 0x91U);
    (void)Cc1101_WriteReg(CC1101_REG_PKTLEN, 0xFFU);
    (void)Cc1101_WriteReg(CC1101_REG_PKTCTRL1, 0x04U);
    (void)Cc1101_WriteReg(CC1101_REG_PKTCTRL0, 0x05U);
    (void)Cc1101_WriteReg(CC1101_REG_ADDR, OPENPERIPH_NODE_ADDR);
    (void)Cc1101_WriteReg(CC1101_REG_CHANNR, OPENPERIPH_RF_CHANNEL_DEFAULT);
    (void)Cc1101_WriteReg(CC1101_REG_FSCTRL1, 0x08U);
    (void)Cc1101_WriteReg(CC1101_REG_FSCTRL0, 0x00U);
    (void)Cc1101_WriteReg(CC1101_REG_FREQ2, 0x10U);
    (void)Cc1101_WriteReg(CC1101_REG_FREQ1, 0xB4U);
    (void)Cc1101_WriteReg(CC1101_REG_FREQ0, 0x2EU);
    (void)Cc1101_WriteReg(CC1101_REG_MDMCFG4, 0x8AU);
    (void)Cc1101_WriteReg(CC1101_REG_MDMCFG3, 0x83U);
    (void)Cc1101_WriteReg(CC1101_REG_MDMCFG2, 0x12U);
    (void)Cc1101_WriteReg(CC1101_REG_MDMCFG1, 0x22U);
    (void)Cc1101_WriteReg(CC1101_REG_MDMCFG0, 0xF8U);
    (void)Cc1101_WriteReg(CC1101_REG_DEVIATN, 0x34U);
    (void)Cc1101_WriteReg(CC1101_REG_MCSM2, 0x07U);
    (void)Cc1101_WriteReg(CC1101_REG_MCSM1, 0x30U);
    (void)Cc1101_WriteReg(CC1101_REG_MCSM0, 0x18U);
    (void)Cc1101_WriteReg(CC1101_REG_FOCCFG, 0x16U);
    (void)Cc1101_WriteReg(CC1101_REG_BSCFG, 0x6CU);
    (void)Cc1101_WriteReg(CC1101_REG_AGCCTRL2, 0x43U);
    (void)Cc1101_WriteReg(CC1101_REG_AGCCTRL1, 0x40U);
    (void)Cc1101_WriteReg(CC1101_REG_AGCCTRL0, 0x91U);
    (void)Cc1101_WriteReg(CC1101_REG_WOREVT1, 0x87U);
    (void)Cc1101_WriteReg(CC1101_REG_WOREVT0, 0x6BU);
    (void)Cc1101_WriteReg(CC1101_REG_WORCTRL, 0xF8U);
    (void)Cc1101_WriteReg(CC1101_REG_FREND1, 0x56U);
    (void)Cc1101_WriteReg(CC1101_REG_FREND0, 0x10U);
    (void)Cc1101_WriteReg(CC1101_REG_FSCAL3, 0xE9U);
    (void)Cc1101_WriteReg(CC1101_REG_FSCAL2, 0x2AU);
    (void)Cc1101_WriteReg(CC1101_REG_FSCAL1, 0x00U);
    (void)Cc1101_WriteReg(CC1101_REG_FSCAL0, 0x1FU);
    (void)Cc1101_WriteReg(CC1101_REG_RCCTRL1, 0x41U);
    (void)Cc1101_WriteReg(CC1101_REG_RCCTRL0, 0x00U);
    (void)Cc1101_WriteReg(CC1101_REG_FSTEST, 0x59U);
    (void)Cc1101_WriteReg(CC1101_REG_PTEST, 0x7FU);
    (void)Cc1101_WriteReg(CC1101_REG_AGCTEST, 0x3FU);
    (void)Cc1101_WriteReg(CC1101_REG_TEST2, 0x81U);
    (void)Cc1101_WriteReg(CC1101_REG_TEST1, 0x35U);
    (void)Cc1101_WriteReg(CC1101_REG_TEST0, 0x09U);
}

static void Cc1101_EnterIdle(void)
{
    const uint32_t start_tick = HAL_GetTick();

    (void)Cc1101_Strobe(CC1101_STROBE_SIDLE);
    while (Cc1101Radio_GetMarcState() != CC1101_STATE_IDLE) {
        if ((HAL_GetTick() - start_tick) >= CC1101_IO_TIMEOUT_MS) {
            break;
        }
    }
}

static void Cc1101_FlushRxFifo(void)
{
    Cc1101_EnterIdle();
    (void)Cc1101_Strobe(CC1101_STROBE_SFRX);
}

static void Cc1101_FlushTxFifo(void)
{
    Cc1101_EnterIdle();
    (void)Cc1101_Strobe(CC1101_STROBE_SFTX);
}

static bool Cc1101_RecoverRxAfterTxDisturbance(void)
{
    Cc1101_FlushTxFifo();
    return Cc1101Radio_EnterRx();
}

static bool Cc1101_IsPacketReady(void)
{
    const uint8_t rx_bytes_raw = Cc1101_ReadRxBytesRaw();

    if ((rx_bytes_raw & CC1101_STATUS_RX_OVERFLOW) != 0U) {
        Cc1101_FlushRxFifo();
        return false;
    }

    if ((rx_bytes_raw & CC1101_STATUS_RX_BYTES_MASK) == 0U) {
        return false;
    }

    return HAL_GPIO_ReadPin(OpenPeriph_RfGdo0Port(), OpenPeriph_RfGdo0Pin()) == GPIO_PIN_RESET;
}

static bool Cc1101_WaitForTxDone(uint32_t timeout_ms)
{
    const uint32_t start_tick = HAL_GetTick();

    while (HAL_GPIO_ReadPin(OpenPeriph_RfGdo0Port(), OpenPeriph_RfGdo0Pin()) == GPIO_PIN_RESET) {
        const uint8_t marc_state = Cc1101Radio_GetMarcState();
        if (marc_state == CC1101_STATE_IDLE) {
            return true;
        }
        if (marc_state == CC1101_STATE_TXFIFO_UNDERFLOW) {
            Cc1101_FlushTxFifo();
            return false;
        }
        if ((HAL_GetTick() - start_tick) >= timeout_ms) {
            return false;
        }
    }

    while (HAL_GPIO_ReadPin(OpenPeriph_RfGdo0Port(), OpenPeriph_RfGdo0Pin()) == GPIO_PIN_SET) {
        if ((HAL_GetTick() - start_tick) >= timeout_ms) {
            return false;
        }
    }

    return Cc1101Radio_GetMarcState() == CC1101_STATE_IDLE;
}

bool Cc1101Radio_Init(void)
{
    HAL_GPIO_WritePin(Cc1101_CsPort(), Cc1101_CsPin(), GPIO_PIN_SET);

    if (!Cc1101Radio_Reset()) {
        s_radio_initialized = false;
        return false;
    }

    Cc1101_WriteSettings();
    if (!Cc1101_WriteBurst(CC1101_REG_PATABLE, kCc1101PaTable, (uint8_t)sizeof(kCc1101PaTable))) {
        s_radio_initialized = false;
        return false;
    }

    Cc1101_FlushRxFifo();
    Cc1101_FlushTxFifo();
    s_radio_initialized = true;
    if (!Cc1101Radio_EnterRx()) {
        s_radio_initialized = false;
        return false;
    }

    return true;
}

bool Cc1101Radio_Reset(void)
{
    uint8_t strobe = CC1101_STROBE_SRES;

    HAL_GPIO_WritePin(Cc1101_CsPort(), Cc1101_CsPin(), GPIO_PIN_SET);
    for (volatile uint32_t delay = 0U; delay < 100U; ++delay) {
    }
    HAL_GPIO_WritePin(Cc1101_CsPort(), Cc1101_CsPin(), GPIO_PIN_RESET);
    for (volatile uint32_t delay = 0U; delay < 100U; ++delay) {
    }
    HAL_GPIO_WritePin(Cc1101_CsPort(), Cc1101_CsPin(), GPIO_PIN_SET);
    for (volatile uint32_t delay = 0U; delay < 500U; ++delay) {
    }

    if (Cc1101_Select() != HAL_OK) {
        return false;
    }

    if (HAL_SPI_Transmit(&hspi1, &strobe, 1U, HAL_MAX_DELAY) != HAL_OK) {
        Cc1101_Deselect();
        return false;
    }

    Cc1101_Deselect();
    HAL_Delay(1U);
    return true;
}

bool Cc1101Radio_Send(const uint8_t *payload, uint8_t length)
{
    if (!s_radio_initialized || payload == NULL || length == 0U || length > CC1101_RADIO_MAX_PACKET_LEN) {
        return false;
    }

    Cc1101_FlushTxFifo();

    if (!Cc1101_WriteReg(CC1101_REG_FIFO, length)) {
        (void)Cc1101_RecoverRxAfterTxDisturbance();
        return false;
    }
    if (!Cc1101_WriteBurst(CC1101_REG_FIFO, payload, length)) {
        (void)Cc1101_RecoverRxAfterTxDisturbance();
        return false;
    }
    if (!Cc1101_Strobe(CC1101_STROBE_STX)) {
        (void)Cc1101_RecoverRxAfterTxDisturbance();
        return false;
    }
    if (!Cc1101_WaitForTxDone(CC1101_TX_TIMEOUT_MS)) {
        (void)Cc1101_RecoverRxAfterTxDisturbance();
        return false;
    }

    return Cc1101Radio_EnterRx();
}

bool Cc1101Radio_Receive(uint8_t *payload, uint8_t *in_out_length)
{
    uint8_t status[2] = {0U, 0U};
    uint8_t packet_length = 0U;
    uint32_t start_tick;

    if (!s_radio_initialized || payload == NULL || in_out_length == NULL || *in_out_length == 0U) {
        return false;
    }
    if (!Cc1101_IsPacketReady()) {
        return false;
    }
    if (!Cc1101_ReadSingle(CC1101_REG_FIFO, &packet_length)) {
        return false;
    }
    if (packet_length == 0U || packet_length > *in_out_length) {
        *in_out_length = packet_length;
        Cc1101_FlushRxFifo();
        (void)Cc1101Radio_EnterRx();
        return false;
    }

    start_tick = HAL_GetTick();
    while ((Cc1101_ReadRxBytesRaw() & CC1101_STATUS_RX_BYTES_MASK) < (uint8_t)(packet_length + 2U)) {
        if ((Cc1101_ReadRxBytesRaw() & CC1101_STATUS_RX_OVERFLOW) != 0U) {
            Cc1101_FlushRxFifo();
            (void)Cc1101Radio_EnterRx();
            return false;
        }
        if ((HAL_GetTick() - start_tick) >= CC1101_IO_TIMEOUT_MS) {
            Cc1101_FlushRxFifo();
            (void)Cc1101Radio_EnterRx();
            return false;
        }
    }

    if (!Cc1101_ReadBurst(CC1101_REG_FIFO, payload, packet_length) ||
        !Cc1101_ReadBurst(CC1101_REG_FIFO, status, (uint8_t)sizeof(status))) {
        Cc1101_FlushRxFifo();
        (void)Cc1101Radio_EnterRx();
        return false;
    }

    *in_out_length = packet_length;
    (void)Cc1101Radio_EnterRx();
    return (status[1] & CC1101_STATUS_CRC_OK) != 0U;
}

bool Cc1101Radio_EnterRx(void)
{
    const uint32_t start_tick = HAL_GetTick();

    if (!s_radio_initialized) {
        return false;
    }

    if (!Cc1101_Strobe(CC1101_STROBE_SRX)) {
        return false;
    }

    while (Cc1101Radio_GetMarcState() != CC1101_STATE_RX) {
        if ((HAL_GetTick() - start_tick) >= CC1101_IO_TIMEOUT_MS) {
            return false;
        }
    }

    return true;
}

uint8_t Cc1101Radio_GetMarcState(void)
{
    return (uint8_t)(Cc1101_ReadStableStatus(CC1101_REG_MARCSTATE) & CC1101_STATUS_STATE_MASK);
}
