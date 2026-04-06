/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "openperiph_config.h"
#include "openperiph_board.h"
#include "rf_link.h"
#include "usbd_cdc_if.h"
#include "ring_buffer.h"
#include "usb_protocol.h"
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

/* USER CODE BEGIN PV */
RingBuffer_t g_usb_rx_ringbuf;
volatile uint8_t g_usb_rx_flag = 0;
static volatile uint8_t g_usb_rx_overflow = 0;
static ProtocolParser_t g_parser;
static uint8_t g_tx_buf[PKT_MAX_FRAME];
static bool g_rf_link_ready = false;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */
static void ProcessPacket(const Packet_t *pkt);
void OpenPeriph_HandleUsbRxBytes(const uint8_t *buf, uint32_t len);
void OpenPeriph_HandleUsbPacket(const Packet_t *pkt);
static void HandleCommand(const Packet_t *pkt);
static void SendResponse(PacketType_t type, const uint8_t *payload, uint16_t len);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USB_DEVICE_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */
  OpenPeriph_BoardInit();
  g_rf_link_ready = RfLink_Init();
  /* Blink PC14 three times to confirm firmware is running */
  //For Debug
  for (int i = 0; i < 3; i++) {
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_SET);
      HAL_Delay(200);
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_RESET);
      HAL_Delay(200);
  }
  RingBuf_Init(&g_usb_rx_ringbuf);
  Protocol_Init(&g_parser);
  HAL_Delay(100);

  /* Startup message */
  if (g_rf_link_ready) {
      const char *hello = "\r\nopenPeriph USB Bridge v1.0 ready\r\n";
      SendResponse(PKT_TYPE_STATUS, (const uint8_t *)hello, strlen(hello));
  } else {
      const uint8_t rf_init_error = 0x10;
      SendResponse(PKT_TYPE_ERROR, &rf_init_error, 1);
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (g_usb_rx_overflow) {
        __disable_irq();
        /* Reset shared parser state before releasing the overflow latch. */
        RingBuf_Init(&g_usb_rx_ringbuf);
        Protocol_Init(&g_parser);
        g_usb_rx_overflow = 0;
        g_usb_rx_flag = 0;
        __enable_irq();
        {
            const uint8_t overflow_code = 0x01;
            SendResponse(PKT_TYPE_ERROR, &overflow_code, 1);
        }
        continue;
    }

    if (g_usb_rx_flag) {
        g_usb_rx_flag = 0;
        uint8_t byte;
        while (RingBuf_ReadByte(&g_usb_rx_ringbuf, &byte)) {
            if (g_usb_rx_overflow) {
                break;
            }
            if (Protocol_ParseByte(&g_parser, byte)) {
                if (g_parser.pkt.valid) {
                    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
                    OpenPeriph_HandleUsbPacket(&g_parser.pkt);
                } else {
                    uint16_t len = Protocol_BuildNACK(&g_parser,
                        g_parser.pkt.id, 0x01, g_tx_buf);
                    CDC_Transmit_Blocking(g_tx_buf, len, 100);
                }
            }
        }
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_2
                          |GPIO_PIN_4, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC13 PC14 PC15 PC2
                           PC4 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15|GPIO_PIN_2
                          |GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PC0 PC1 PC12 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB2 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PB10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void OpenPeriph_HandleUsbRxBytes(const uint8_t *buf, uint32_t len)
{
    if (g_usb_rx_overflow) {
        g_usb_rx_flag = 1;
        return;
    }

    for (uint32_t i = 0; i < len; ++i) {
        if (!RingBuf_WriteByte(&g_usb_rx_ringbuf, buf[i])) {
            g_usb_rx_overflow = 1;
            break;
        }
    }

    g_usb_rx_flag = 1;
}

void OpenPeriph_HandleUsbPacket(const Packet_t *pkt)
{
    ProcessPacket(pkt);
}

static void SendResponse(PacketType_t type, const uint8_t *payload, uint16_t len)
{
    uint16_t frame_len = Protocol_BuildPacket(&g_parser, type, payload, len, g_tx_buf);
    CDC_Transmit_Blocking(g_tx_buf, frame_len, 200);
}

static void ProcessPacket(const Packet_t *pkt)
{
    switch (pkt->type) {
    case PKT_TYPE_IMAGE_DATA:
    case PKT_TYPE_EMAIL_DATA:
    case PKT_TYPE_TEXT_DATA:
        /* TODO: forward to CC1101. For now, ACK back. */
        {
            uint16_t len = Protocol_BuildACK(&g_parser, pkt->id, g_tx_buf);
            CDC_Transmit_Blocking(g_tx_buf, len, 100);
        }
        break;

    case PKT_TYPE_FILE_START:
    case PKT_TYPE_FILE_END:
        {
            uint16_t len = Protocol_BuildACK(&g_parser, pkt->id, g_tx_buf);
            CDC_Transmit_Blocking(g_tx_buf, len, 100);
        }
        break;

    case PKT_TYPE_COMMAND:
        HandleCommand(pkt);
        break;

    default:
        {
            uint16_t len = Protocol_BuildNACK(&g_parser, pkt->id, 0x02, g_tx_buf);
            CDC_Transmit_Blocking(g_tx_buf, len, 100);
        }
        break;
    }
}

static void HandleCommand(const Packet_t *pkt)
{
    if (pkt->payload_len < 1) return;
    CommandID_t cmd = (CommandID_t)pkt->payload[0];
    uint16_t resp_len;

    switch (cmd) {
    case CMD_PING:
        resp_len = Protocol_BuildACK(&g_parser, pkt->id, g_tx_buf);
        CDC_Transmit_Blocking(g_tx_buf, resp_len, 100);
        break;

    case CMD_RESET:
        resp_len = Protocol_BuildACK(&g_parser, pkt->id, g_tx_buf);
        CDC_Transmit_Blocking(g_tx_buf, resp_len, 200);
        HAL_Delay(10);
        NVIC_SystemReset();
        break;

    case CMD_GET_STATUS:
        {
            uint8_t sp[8];
            sp[0] = 1; sp[1] = 0;  /* firmware v1.0 */
            sp[2] = 0;             /* CC1101 status placeholder */
            sp[3] = (uint8_t)(RingBuf_Available(&g_usb_rx_ringbuf) & 0xFF);
            sp[4] = (uint8_t)(RingBuf_Available(&g_usb_rx_ringbuf) >> 8);
            sp[5] = 0; sp[6] = 0; sp[7] = 0;
            resp_len = Protocol_BuildPacket(&g_parser, PKT_TYPE_STATUS, sp, 8, g_tx_buf);
            CDC_Transmit_Blocking(g_tx_buf, resp_len, 100);
        }
        break;

    case CMD_SET_RF_CHANNEL:
    case CMD_SET_RF_POWER:
    case CMD_SET_RF_ADDR:
        if (pkt->payload_len >= 2) {
            /* TODO: CC1101 driver call with pkt->payload[1] */
            resp_len = Protocol_BuildACK(&g_parser, pkt->id, g_tx_buf);
        } else {
            resp_len = Protocol_BuildNACK(&g_parser, pkt->id, 0x03, g_tx_buf);
        }
        CDC_Transmit_Blocking(g_tx_buf, resp_len, 100);
        break;

    default:
        resp_len = Protocol_BuildNACK(&g_parser, pkt->id, 0x04, g_tx_buf);
        CDC_Transmit_Blocking(g_tx_buf, resp_len, 100);
        break;
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
