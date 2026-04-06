# OpenPeriph Merge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build one STM32CubeIDE firmware project that can compile as either master or slave and complete the phase 1 flow `USB CDC -> master -> addressed RF -> slave -> e-ink text render`.

**Architecture:** Keep CubeMX-generated startup, HAL, USB, and `.ioc` ownership intact while adding new handwritten modules under `Core/Inc` and `Core/Src`. Split pure frame/protocol logic into host-testable C modules and keep HAL-bound RF/EPD integrations behind small services so bench bring-up failures stay localized.

**Tech Stack:** STM32CubeIDE-generated STM32 HAL, STM32 USB CDC middleware, C11/C99-style embedded C, GNU Tools for STM32, host `cc` for pure-C smoke tests, Python 3 for the host send script.

---

## File Structure

### Create

- `Core/Inc/openperiph_config.h`
- `Core/Inc/openperiph_board.h`
- `Core/Inc/app_protocol.h`
- `Core/Inc/rf_frame.h`
- `Core/Inc/rf_link.h`
- `Core/Inc/display_service.h`
- `Core/Inc/epd_port.h`
- `Core/Inc/cc1101_radio.h`
- `Core/Inc/app_master.h`
- `Core/Inc/app_slave.h`
- `Core/Src/openperiph_board.c`
- `Core/Src/app_protocol.c`
- `Core/Src/rf_frame.c`
- `Core/Src/rf_link.c`
- `Core/Src/display_service.c`
- `Core/Src/epd_port.c`
- `Core/Src/cc1101_radio.c`
- `Core/Src/app_master.c`
- `Core/Src/app_slave.c`
- `Core/Src/EPD_2in13_V4.c`
- `Core/Src/DEV_Config.c`
- `Core/Src/GUI_Paint.c`
- `Core/Src/font8.c`
- `Core/Src/font12.c`
- `Core/Src/font16.c`
- `Core/Src/font20.c`
- `Core/Src/font24.c`
- `Core/Src/font12CN.c`
- `Core/Src/font24CN.c`
- `Core/Inc/EPD_2in13_V4.h`
- `Core/Inc/DEV_Config.h`
- `Core/Inc/GUI_Paint.h`
- `Core/Inc/fonts.h`
- `Core/Inc/Debug.h`
- `tests/host/test_rf_frame.c`
- `tests/host/test_app_protocol.c`

### Modify

- `Core/Src/main.c`
- `Core/Inc/usb_protocol.h`
- `USB_DEVICE/App/usbd_cdc_if.c`
- `scripts/send_data.py`

### Purpose Map

- `openperiph_config.*`: compile-time role, address, and feature toggles.
- `openperiph_board.*`: shared access to `hspi1`, RF GPIOs, EPD GPIOs, and helper wrappers.
- `app_protocol.*`: USB payload schema for draw-text commands.
- `rf_frame.*`: HAL-free addressed RF frame encode/decode helpers.
- `cc1101_radio.*`: low-level radio driver using HAL SPI/GPIO.
- `rf_link.*`: radio service for send/receive/init/address filtering.
- `epd_port.*`: adapter between project board resources and Waveshare-style EPD glue.
- `display_service.*`: panel init and draw-text operations.
- `app_master.*`: USB packet to RF message orchestration.
- `app_slave.*`: RF message to display orchestration.
- `tests/host/*`: host-compiled pure-C verification of framing and app payload formats.

### Constraints

- Do not include or compile source files from `ref/`.
- Keep all custom behavior in handwritten files or CubeMX `USER CODE` regions.
- Do not add runtime SPI reinitialization.
- If command-line builds stop tracking newly added source files, regenerate the STM32CubeIDE project metadata once from the existing project instead of hand-editing generated `Debug/*.mk` files.

### Build Commands

- Embedded build: `make -C Debug clean all`
- RF frame host test: `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame`
- App protocol host test: `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_protocol.c Core/Src/app_protocol.c -o /tmp/test_app_protocol && /tmp/test_app_protocol`

### Expected Embedded Build Output

- `Finished building target: openPeriph_monorepo.elf`
- `arm-none-eabi-size  openPeriph_monorepo.elf`

### Expected Host Test Output

- Process exits `0`
- No assertion failures

## Task 1: Configuration and Board Scaffold

**Files:**
- Create: `Core/Inc/openperiph_config.h`
- Create: `Core/Inc/openperiph_board.h`
- Create: `Core/Src/openperiph_board.c`
- Modify: `Core/Src/main.c`
- Test: `make -C Debug clean all`

- [ ] **Step 1: Write the failing compile by making `main.c` depend on the new config boundary**

Add these includes and calls in `Core/Src/main.c` user sections before creating the new files:

```c
#include "openperiph_config.h"
#include "openperiph_board.h"

/* ... */

  OpenPeriph_BoardInit();
```

- [ ] **Step 2: Run build to verify it fails**

Run: `make -C Debug clean all`
Expected: FAIL with missing header errors for `openperiph_config.h` and `openperiph_board.h`

- [ ] **Step 3: Write minimal config and board implementation**

Create `Core/Inc/openperiph_config.h`:

```c
#ifndef OPENPERIPH_CONFIG_H
#define OPENPERIPH_CONFIG_H

#include <stdint.h>

#define OPENPERIPH_ROLE_MASTER 1U
#define OPENPERIPH_ROLE_SLAVE  2U

#ifndef OPENPERIPH_ROLE
#define OPENPERIPH_ROLE OPENPERIPH_ROLE_MASTER
#endif

#ifndef OPENPERIPH_NODE_ADDR
#define OPENPERIPH_NODE_ADDR 0x01U
#endif

#define OPENPERIPH_RF_CHANNEL_DEFAULT 0x00U
#define OPENPERIPH_FEATURE_USB_BRIDGE 1U
#define OPENPERIPH_FEATURE_DISPLAY    1U

#endif
```

Create `Core/Inc/openperiph_board.h`:

```c
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
```

Create `Core/Src/openperiph_board.c`:

```c
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
```

Update `Core/Src/main.c` to call `OpenPeriph_BoardInit()` after `MX_SPI1_Init()`.

- [ ] **Step 4: Run build to verify it passes**

Run: `make -C Debug clean all`
Expected: PASS and emit `openPeriph_monorepo.elf`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/openperiph_config.h Core/Inc/openperiph_board.h Core/Src/openperiph_board.c Core/Src/main.c
git commit -m "feat: add openperiph config and board scaffold"
```

## Task 2: RF Frame Format as a Host-Tested Pure-C Module

**Files:**
- Create: `Core/Inc/rf_frame.h`
- Create: `Core/Src/rf_frame.c`
- Create: `tests/host/test_rf_frame.c`
- Test: `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame`

- [ ] **Step 1: Write the failing host test**

Create `tests/host/test_rf_frame.c`:

```c
#include "rf_frame.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    uint8_t encoded[64];
    uint8_t payload[] = {'H', 'i'};
    RfFrame_t frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_TEXT,
        .dst_addr = 0x22,
        .src_addr = 0x01,
        .seq = 0x05,
        .payload_len = sizeof(payload),
    };
    memcpy(frame.payload, payload, sizeof(payload));

    size_t used = RfFrame_Encode(&frame, encoded, sizeof(encoded));
    assert(used == 8U);

    RfFrame_t decoded;
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.msg_type == RF_MSG_DRAW_TEXT);
    assert(decoded.dst_addr == 0x22);
    assert(decoded.src_addr == 0x01);
    assert(decoded.seq == 0x05);
    assert(decoded.payload_len == 2U);
    assert(decoded.payload[0] == 'H');
    assert(decoded.payload[1] == 'i');
    return 0;
}
```

- [ ] **Step 2: Run host test to verify it fails**

Run: `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame`
Expected: FAIL because `rf_frame.h` and `rf_frame.c` do not exist yet

- [ ] **Step 3: Write minimal RF frame implementation**

Create `Core/Inc/rf_frame.h`:

```c
#ifndef RF_FRAME_H
#define RF_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RF_FRAME_VERSION 1U
#define RF_FRAME_MAX_PAYLOAD 48U

typedef enum {
    RF_MSG_DRAW_TEXT = 0x01,
    RF_MSG_ACK = 0x80,
    RF_MSG_ERROR = 0x81,
} RfMessageType_t;

typedef struct {
    uint8_t version;
    uint8_t msg_type;
    uint8_t dst_addr;
    uint8_t src_addr;
    uint8_t seq;
    uint8_t payload_len;
    uint8_t payload[RF_FRAME_MAX_PAYLOAD];
} RfFrame_t;

size_t RfFrame_Encode(const RfFrame_t *frame, uint8_t *out, size_t out_size);
bool RfFrame_Decode(const uint8_t *data, size_t data_len, RfFrame_t *out);

#endif
```

Create `Core/Src/rf_frame.c`:

```c
#include "rf_frame.h"
#include <string.h>

size_t RfFrame_Encode(const RfFrame_t *frame, uint8_t *out, size_t out_size)
{
    size_t total = 6U + frame->payload_len;
    if ((frame == NULL) || (out == NULL) || (frame->payload_len > RF_FRAME_MAX_PAYLOAD) || (out_size < total)) {
        return 0U;
    }

    out[0] = frame->version;
    out[1] = frame->msg_type;
    out[2] = frame->dst_addr;
    out[3] = frame->src_addr;
    out[4] = frame->seq;
    out[5] = frame->payload_len;
    memcpy(&out[6], frame->payload, frame->payload_len);
    return total;
}

bool RfFrame_Decode(const uint8_t *data, size_t data_len, RfFrame_t *out)
{
    if ((data == NULL) || (out == NULL) || (data_len < 6U)) {
        return false;
    }

    out->version = data[0];
    out->msg_type = data[1];
    out->dst_addr = data[2];
    out->src_addr = data[3];
    out->seq = data[4];
    out->payload_len = data[5];

    if ((out->version != RF_FRAME_VERSION) || (out->payload_len > RF_FRAME_MAX_PAYLOAD) || (data_len != (size_t)(6U + out->payload_len))) {
        return false;
    }

    memcpy(out->payload, &data[6], out->payload_len);
    return true;
}
```

- [ ] **Step 4: Run host test to verify it passes**

Run: `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame`
Expected: PASS with exit code `0`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/rf_frame.h Core/Src/rf_frame.c tests/host/test_rf_frame.c
git commit -m "feat: add addressed rf frame codec"
```

## Task 3: Draw-Text USB Payload Codec

**Files:**
- Create: `Core/Inc/app_protocol.h`
- Create: `Core/Src/app_protocol.c`
- Create: `tests/host/test_app_protocol.c`
- Modify: `Core/Inc/usb_protocol.h`
- Test: `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_protocol.c Core/Src/app_protocol.c -o /tmp/test_app_protocol && /tmp/test_app_protocol`

- [ ] **Step 1: Write the failing host test**

Create `tests/host/test_app_protocol.c`:

```c
#include "app_protocol.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    AppDrawTextCommand_t cmd = {
        .dst_addr = 0x22,
        .x = 10,
        .y = 20,
        .font_id = APP_FONT_16,
        .flags = APP_DRAW_FLAG_CLEAR_FIRST | APP_DRAW_FLAG_FULL_REFRESH,
        .text_len = 5,
    };
    memcpy(cmd.text, "Hello", 5);

    uint8_t encoded[64];
    size_t used = AppProtocol_EncodeDrawText(&cmd, encoded, sizeof(encoded));
    assert(used == 13U);

    AppDrawTextCommand_t decoded;
    assert(AppProtocol_DecodeDrawText(encoded, used, &decoded));
    assert(decoded.dst_addr == 0x22);
    assert(decoded.x == 10);
    assert(decoded.y == 20);
    assert(decoded.font_id == APP_FONT_16);
    assert(decoded.flags == (APP_DRAW_FLAG_CLEAR_FIRST | APP_DRAW_FLAG_FULL_REFRESH));
    assert(decoded.text_len == 5U);
    assert(memcmp(decoded.text, "Hello", 5) == 0);
    return 0;
}
```

- [ ] **Step 2: Run host test to verify it fails**

Run: `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_protocol.c Core/Src/app_protocol.c -o /tmp/test_app_protocol && /tmp/test_app_protocol`
Expected: FAIL because `app_protocol.h` and `app_protocol.c` do not exist yet

- [ ] **Step 3: Write minimal app payload codec**

Create `Core/Inc/app_protocol.h`:

```c
#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_TEXT_MAX_LEN 40U
#define APP_CMD_DRAW_TEXT 0x20U
#define APP_FONT_12 0x01U
#define APP_FONT_16 0x02U

#define APP_DRAW_FLAG_CLEAR_FIRST  0x01U
#define APP_DRAW_FLAG_FULL_REFRESH 0x02U

typedef struct {
    uint8_t dst_addr;
    uint16_t x;
    uint16_t y;
    uint8_t font_id;
    uint8_t flags;
    uint8_t text_len;
    uint8_t text[APP_TEXT_MAX_LEN];
} AppDrawTextCommand_t;

size_t AppProtocol_EncodeDrawText(const AppDrawTextCommand_t *cmd, uint8_t *out, size_t out_size);
bool AppProtocol_DecodeDrawText(const uint8_t *data, size_t data_len, AppDrawTextCommand_t *out);

#endif
```

Create `Core/Src/app_protocol.c`:

```c
#include "app_protocol.h"
#include <string.h>

size_t AppProtocol_EncodeDrawText(const AppDrawTextCommand_t *cmd, uint8_t *out, size_t out_size)
{
    size_t total = 8U + cmd->text_len;
    if ((cmd == NULL) || (out == NULL) || (cmd->text_len > APP_TEXT_MAX_LEN) || (out_size < total)) {
        return 0U;
    }

    out[0] = cmd->dst_addr;
    out[1] = (uint8_t)(cmd->x & 0xFFU);
    out[2] = (uint8_t)(cmd->x >> 8);
    out[3] = (uint8_t)(cmd->y & 0xFFU);
    out[4] = (uint8_t)(cmd->y >> 8);
    out[5] = cmd->font_id;
    out[6] = cmd->flags;
    out[7] = cmd->text_len;
    memcpy(&out[8], cmd->text, cmd->text_len);
    return total;
}

bool AppProtocol_DecodeDrawText(const uint8_t *data, size_t data_len, AppDrawTextCommand_t *out)
{
    if ((data == NULL) || (out == NULL) || (data_len < 8U)) {
        return false;
    }

    out->dst_addr = data[0];
    out->x = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
    out->y = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
    out->font_id = data[5];
    out->flags = data[6];
    out->text_len = data[7];

    if ((out->text_len > APP_TEXT_MAX_LEN) || (data_len != (size_t)(8U + out->text_len))) {
        return false;
    }

    memcpy(out->text, &data[8], out->text_len);
    return true;
}
```

Add to `Core/Inc/usb_protocol.h`:

```c
#define PKT_TYPE_DRAW_TEXT 0x11
```

- [ ] **Step 4: Run host test to verify it passes**

Run: `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_protocol.c Core/Src/app_protocol.c -o /tmp/test_app_protocol && /tmp/test_app_protocol`
Expected: PASS with exit code `0`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/app_protocol.h Core/Src/app_protocol.c Core/Inc/usb_protocol.h tests/host/test_app_protocol.c
git commit -m "feat: add draw text app protocol codec"
```

## Task 4: USB CDC Receive Plumbing and Main Loop Handoff

**Files:**
- Modify: `USB_DEVICE/App/usbd_cdc_if.c`
- Modify: `Core/Src/main.c`
- Modify: `Core/Inc/usb_protocol.h`
- Test: `make -C Debug clean all`

- [ ] **Step 1: Write the failing compile by moving packet dispatch out of `main.c`**

Replace direct `ProcessPacket()` usage in `Core/Src/main.c` with a not-yet-implemented application handoff:

```c
extern void OpenPeriph_HandleUsbPacket(const Packet_t *pkt);
/* ... */
OpenPeriph_HandleUsbPacket(&g_parser.pkt);
```

- [ ] **Step 2: Run build to verify it fails**

Run: `make -C Debug clean all`
Expected: FAIL with undefined reference to `OpenPeriph_HandleUsbPacket`

- [ ] **Step 3: Implement receive buffering and keep temporary packet handoff local**

In `USB_DEVICE/App/usbd_cdc_if.c` add the user-section declarations:

```c
#include "ring_buffer.h"

extern RingBuffer_t g_usb_rx_ringbuf;
extern volatile uint8_t g_usb_rx_flag;
```

Replace `CDC_Receive_FS()` user code with:

```c
for (uint32_t i = 0; i < *Len; ++i) {
    RingBuf_WriteByte(&g_usb_rx_ringbuf, Buf[i]);
}
g_usb_rx_flag = 1;
USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
USBD_CDC_ReceivePacket(&hUsbDeviceFS);
return (USBD_OK);
```

In `Core/Src/main.c`, add a temporary adapter:

```c
void OpenPeriph_HandleUsbPacket(const Packet_t *pkt)
{
    ProcessPacket(pkt);
}
```

Change the parser loop call site:

```c
OpenPeriph_HandleUsbPacket(&g_parser.pkt);
```

- [ ] **Step 4: Run build to verify it passes**

Run: `make -C Debug clean all`
Expected: PASS and emit `openPeriph_monorepo.elf`

- [ ] **Step 5: Commit**

```bash
git add USB_DEVICE/App/usbd_cdc_if.c Core/Src/main.c
git commit -m "feat: wire usb cdc receive path into packet parser"
```

## Task 5: Reimplement the CC1101 Radio and RF Link Service

**Files:**
- Create: `Core/Inc/cc1101_radio.h`
- Create: `Core/Src/cc1101_radio.c`
- Create: `Core/Inc/rf_link.h`
- Create: `Core/Src/rf_link.c`
- Modify: `Core/Src/main.c`
- Test: `make -C Debug clean all`

- [ ] **Step 1: Write the failing compile by calling RF service init from `main.c`**

Add to `Core/Src/main.c`:

```c
#include "rf_link.h"
/* ... */
  RfLink_Init();
```

- [ ] **Step 2: Run build to verify it fails**

Run: `make -C Debug clean all`
Expected: FAIL with missing `rf_link.h`

- [ ] **Step 3: Implement the low-level radio and service**

Create `Core/Inc/cc1101_radio.h` with an API shaped like:

```c
#ifndef CC1101_RADIO_H
#define CC1101_RADIO_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

void Cc1101Radio_Init(void);
bool Cc1101Radio_Reset(void);
bool Cc1101Radio_Send(const uint8_t *data, uint8_t len);
bool Cc1101Radio_Receive(uint8_t *data, uint8_t *len);
void Cc1101Radio_EnterRx(void);
uint8_t Cc1101Radio_GetMarcState(void);

#endif
```

Implement `Core/Src/cc1101_radio.c` by re-expressing the reference behavior in repo-local code:

```c
#include "cc1101_radio.h"
#include "openperiph_board.h"

/* static helpers for SPI read/write, MISO-ready timeout, reset, RX/TX FIFO management */
/* use hspi1 and board pin accessors; do not hardcode a separate SPI handle */
```

Create `Core/Inc/rf_link.h`:

```c
#ifndef RF_LINK_H
#define RF_LINK_H

#include "app_protocol.h"
#include "rf_frame.h"
#include <stdbool.h>

void RfLink_Init(void);
bool RfLink_SendFrame(const RfFrame_t *frame);
bool RfLink_TryReceiveFrame(RfFrame_t *frame);
bool RfLink_IsForLocalNode(const RfFrame_t *frame);

#endif
```

Create `Core/Src/rf_link.c` with:

```c
#include "rf_link.h"
#include "cc1101_radio.h"
#include "openperiph_config.h"

void RfLink_Init(void)
{
    Cc1101Radio_Init();
    (void)Cc1101Radio_Reset();
    Cc1101Radio_EnterRx();
}
```

Implement `RfLink_SendFrame()`, `RfLink_TryReceiveFrame()`, and `RfLink_IsForLocalNode()` using `RfFrame_Encode()`, `RfFrame_Decode()`, and `OPENPERIPH_NODE_ADDR`.

- [ ] **Step 4: Run build to verify it passes**

Run: `make -C Debug clean all`
Expected: PASS and emit `openPeriph_monorepo.elf`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/cc1101_radio.h Core/Src/cc1101_radio.c Core/Inc/rf_link.h Core/Src/rf_link.c Core/Src/main.c
git commit -m "feat: add cc1101 radio and rf link service"
```

## Task 6: Reimplement the EPD Driver and Display Service

**Files:**
- Create: `Core/Inc/epd_port.h`
- Create: `Core/Src/epd_port.c`
- Create: `Core/Inc/display_service.h`
- Create: `Core/Src/display_service.c`
- Create: `Core/Inc/EPD_2in13_V4.h`
- Create: `Core/Inc/DEV_Config.h`
- Create: `Core/Inc/GUI_Paint.h`
- Create: `Core/Inc/fonts.h`
- Create: `Core/Inc/Debug.h`
- Create: `Core/Src/EPD_2in13_V4.c`
- Create: `Core/Src/DEV_Config.c`
- Create: `Core/Src/GUI_Paint.c`
- Create: `Core/Src/font8.c`
- Create: `Core/Src/font12.c`
- Create: `Core/Src/font16.c`
- Create: `Core/Src/font20.c`
- Create: `Core/Src/font24.c`
- Create: `Core/Src/font12CN.c`
- Create: `Core/Src/font24CN.c`
- Test: `make -C Debug clean all`

- [ ] **Step 1: Write the failing compile by calling display init from `main.c`**

Add to `Core/Src/main.c`:

```c
#include "display_service.h"
/* ... */
#if OPENPERIPH_FEATURE_DISPLAY
  DisplayService_Init();
#endif
```

- [ ] **Step 2: Run build to verify it fails**

Run: `make -C Debug clean all`
Expected: FAIL with missing `display_service.h`

- [ ] **Step 3: Implement the port layer and display service**

Create `Core/Inc/epd_port.h`:

```c
#ifndef EPD_PORT_H
#define EPD_PORT_H

#include <stdbool.h>
#include <stdint.h>

void EpdPort_Init(void);
void EpdPort_Reset(void);
void EpdPort_SendCommand(uint8_t cmd);
void EpdPort_SendData(uint8_t data);
void EpdPort_SendBuffer(const uint8_t *data, uint16_t len);
bool EpdPort_WaitUntilIdle(uint32_t timeout_ms);

#endif
```

Create `Core/Inc/display_service.h`:

```c
#ifndef DISPLAY_SERVICE_H
#define DISPLAY_SERVICE_H

#include "app_protocol.h"
#include <stdbool.h>

void DisplayService_Init(void);
bool DisplayService_DrawText(const AppDrawTextCommand_t *cmd);

#endif
```

Create `Core/Src/display_service.c` with a framebuffer-backed implementation:

```c
#include "display_service.h"
#include "DEV_Config.h"
#include "EPD_2in13_V4.h"
#include "GUI_Paint.h"
#include "fonts.h"

static UBYTE g_black_image[((EPD_2in13_V4_WIDTH / 8) + 1) * EPD_2in13_V4_HEIGHT];
static bool g_display_ready = false;

void DisplayService_Init(void)
{
    DEV_Module_Init();
    EPD_2in13_V4_Init();
    EPD_2in13_V4_Clear();
    Paint_NewImage(g_black_image, EPD_2in13_V4_WIDTH, EPD_2in13_V4_HEIGHT, 90, WHITE);
    Paint_SelectImage(g_black_image);
    Paint_Clear(WHITE);
    g_display_ready = true;
}
```

Implement `DisplayService_DrawText()` to:

```c
if (cmd->flags & APP_DRAW_FLAG_CLEAR_FIRST) { Paint_Clear(WHITE); }
Paint_DrawString_EN(cmd->x, cmd->y, (const char *)cmd->text, chosen_font, BLACK, WHITE);
EPD_2in13_V4_Display_Base(g_black_image);
```

Populate the EPD vendor-style sources in `Core/Inc` and `Core/Src`, but rewrite the hardware-facing calls so they route through repo-local board/port glue rather than copied standalone `main.c` assumptions.

- [ ] **Step 4: Run build to verify it passes**

Run: `make -C Debug clean all`
Expected: PASS and emit `openPeriph_monorepo.elf`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/epd_port.h Core/Src/epd_port.c Core/Inc/display_service.h Core/Src/display_service.c Core/Inc/EPD_2in13_V4.h Core/Inc/DEV_Config.h Core/Inc/GUI_Paint.h Core/Inc/fonts.h Core/Inc/Debug.h Core/Src/EPD_2in13_V4.c Core/Src/DEV_Config.c Core/Src/GUI_Paint.c Core/Src/font8.c Core/Src/font12.c Core/Src/font16.c Core/Src/font20.c Core/Src/font24.c Core/Src/font12CN.c Core/Src/font24CN.c Core/Src/main.c
git commit -m "feat: add epd driver and display service"
```

## Task 7: Slave Application Path

**Files:**
- Create: `Core/Inc/app_slave.h`
- Create: `Core/Src/app_slave.c`
- Modify: `Core/Src/main.c`
- Test: `make -C Debug clean all`

- [ ] **Step 1: Write the failing compile by moving slave behavior into its own module**

Add to `Core/Src/main.c`:

```c
#include "app_slave.h"
/* ... */
#if OPENPERIPH_ROLE == OPENPERIPH_ROLE_SLAVE
  AppSlave_Init();
#endif
```

Inside the main loop, add:

```c
#if OPENPERIPH_ROLE == OPENPERIPH_ROLE_SLAVE
  AppSlave_Poll();
#endif
```

- [ ] **Step 2: Run build to verify it fails**

Run: `make -C Debug clean all`
Expected: FAIL with missing `app_slave.h`

- [ ] **Step 3: Implement the slave app**

Create `Core/Inc/app_slave.h`:

```c
#ifndef APP_SLAVE_H
#define APP_SLAVE_H

void AppSlave_Init(void);
void AppSlave_Poll(void);

#endif
```

Create `Core/Src/app_slave.c`:

```c
#include "app_slave.h"
#include "app_protocol.h"
#include "display_service.h"
#include "rf_link.h"

void AppSlave_Init(void)
{
    RfLink_Init();
    DisplayService_Init();
}

void AppSlave_Poll(void)
{
    RfFrame_t frame;
    if (!RfLink_TryReceiveFrame(&frame)) {
        return;
    }

    if (!RfLink_IsForLocalNode(&frame)) {
        return;
    }

    if (frame.msg_type == RF_MSG_DRAW_TEXT) {
        AppDrawTextCommand_t cmd;
        if (AppProtocol_DecodeDrawText(frame.payload, frame.payload_len, &cmd)) {
            (void)DisplayService_DrawText(&cmd);
        }
    }
}
```

- [ ] **Step 4: Run build to verify it passes**

Run: `make -C Debug clean all`
Expected: PASS and emit `openPeriph_monorepo.elf`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/app_slave.h Core/Src/app_slave.c Core/Src/main.c
git commit -m "feat: add slave rf-to-display flow"
```

## Task 8: Master Application Path

**Files:**
- Create: `Core/Inc/app_master.h`
- Create: `Core/Src/app_master.c`
- Modify: `Core/Src/main.c`
- Test: `make -C Debug clean all`

- [ ] **Step 1: Write the failing compile by moving master behavior into its own module**

Add to `Core/Src/main.c`:

```c
#include "app_master.h"
/* ... */
#if OPENPERIPH_ROLE == OPENPERIPH_ROLE_MASTER
  AppMaster_Init();
#endif
```

Replace the temporary `OpenPeriph_HandleUsbPacket()` adapter with:

```c
#if OPENPERIPH_ROLE == OPENPERIPH_ROLE_MASTER
  AppMaster_HandleUsbPacket(pkt);
#endif
```

- [ ] **Step 2: Run build to verify it fails**

Run: `make -C Debug clean all`
Expected: FAIL with missing `app_master.h`

- [ ] **Step 3: Implement the master app**

Create `Core/Inc/app_master.h`:

```c
#ifndef APP_MASTER_H
#define APP_MASTER_H

#include "usb_protocol.h"

void AppMaster_Init(void);
void AppMaster_HandleUsbPacket(const Packet_t *pkt);

#endif
```

Create `Core/Src/app_master.c`:

```c
#include "app_master.h"
#include "app_protocol.h"
#include "openperiph_config.h"
#include "rf_frame.h"
#include "rf_link.h"
#include "usbd_cdc_if.h"

extern ProtocolParser_t g_parser;
extern uint8_t g_tx_buf[PKT_MAX_FRAME];

static void AppMaster_SendAck(uint8_t pkt_id)
{
    uint16_t len = Protocol_BuildACK(&g_parser, pkt_id, g_tx_buf);
    (void)CDC_Transmit_Blocking(g_tx_buf, len, 100);
}

void AppMaster_Init(void)
{
    RfLink_Init();
}

void AppMaster_HandleUsbPacket(const Packet_t *pkt)
{
    if ((pkt->type != PKT_TYPE_DRAW_TEXT) || (pkt->payload_len < 8U)) {
        return;
    }

    AppDrawTextCommand_t cmd;
    if (!AppProtocol_DecodeDrawText(pkt->payload, pkt->payload_len, &cmd)) {
        return;
    }

    RfFrame_t frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_TEXT,
        .dst_addr = cmd.dst_addr,
        .src_addr = OPENPERIPH_NODE_ADDR,
        .seq = pkt->id,
        .payload_len = (uint8_t)pkt->payload_len,
    };
    memcpy(frame.payload, pkt->payload, pkt->payload_len);

    if (RfLink_SendFrame(&frame)) {
        AppMaster_SendAck(pkt->id);
    }
}
```

- [ ] **Step 4: Run build to verify it passes**

Run: `make -C Debug clean all`
Expected: PASS and emit `openPeriph_monorepo.elf`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/app_master.h Core/Src/app_master.c Core/Src/main.c
git commit -m "feat: add master usb-to-rf flow"
```

## Task 9: Extend USB Packet Handling and Remove Temporary Main Logic

**Files:**
- Modify: `Core/Src/main.c`
- Modify: `Core/Inc/usb_protocol.h`
- Test: `make -C Debug clean all`

- [ ] **Step 1: Write the failing build by deleting the old placeholder packet handlers**

Remove `ProcessPacket()` handling for `PKT_TYPE_TEXT_DATA`, `PKT_TYPE_IMAGE_DATA`, `PKT_TYPE_EMAIL_DATA`, and the temporary `OpenPeriph_HandleUsbPacket()` adapter before updating the packet switch to recognize `PKT_TYPE_DRAW_TEXT`.

- [ ] **Step 2: Run build to verify it fails**

Run: `make -C Debug clean all`
Expected: FAIL with missing packet handler path for draw-text or undefined references after removing the temporary adapter

- [ ] **Step 3: Implement the final dispatch**

In `Core/Inc/usb_protocol.h`, ensure:

```c
typedef enum {
    PKT_TYPE_IMAGE_DATA     = 0x01,
    PKT_TYPE_EMAIL_DATA     = 0x02,
    PKT_TYPE_TEXT_DATA      = 0x03,
    PKT_TYPE_FILE_START     = 0x04,
    PKT_TYPE_FILE_END       = 0x05,
    PKT_TYPE_COMMAND        = 0x10,
    PKT_TYPE_DRAW_TEXT      = 0x11,
    PKT_TYPE_ACK            = 0x80,
    PKT_TYPE_NACK           = 0x81,
    PKT_TYPE_STATUS         = 0x82,
    PKT_TYPE_RF_TX_DONE     = 0x83,
    PKT_TYPE_RF_RX_DATA     = 0x84,
    PKT_TYPE_ERROR          = 0xFE,
    PKT_TYPE_DEBUG          = 0xFF,
} PacketType_t;
```

In `Core/Src/main.c`, shrink `ProcessPacket()` to:

```c
static void ProcessPacket(const Packet_t *pkt)
{
    switch (pkt->type) {
    case PKT_TYPE_COMMAND:
        HandleCommand(pkt);
        break;
    case PKT_TYPE_DRAW_TEXT:
#if OPENPERIPH_ROLE == OPENPERIPH_ROLE_MASTER
        AppMaster_HandleUsbPacket(pkt);
#else
        {
            uint16_t len = Protocol_BuildNACK(&g_parser, pkt->id, 0x05, g_tx_buf);
            (void)CDC_Transmit_Blocking(g_tx_buf, len, 100);
        }
#endif
        break;
    default:
        {
            uint16_t len = Protocol_BuildNACK(&g_parser, pkt->id, 0x02, g_tx_buf);
            (void)CDC_Transmit_Blocking(g_tx_buf, len, 100);
        }
        break;
    }
}
```

- [ ] **Step 4: Run build to verify it passes**

Run: `make -C Debug clean all`
Expected: PASS and emit `openPeriph_monorepo.elf`

- [ ] **Step 5: Commit**

```bash
git add Core/Src/main.c Core/Inc/usb_protocol.h
git commit -m "refactor: finalize usb draw text dispatch"
```

## Task 10: Host Script Support for Draw-Text Commands

**Files:**
- Modify: `scripts/send_data.py`
- Test: `python3 scripts/send_data.py --help`

- [ ] **Step 1: Write the failing CLI expectation**

Run the current help and note that there is no `--draw-text` command:

Run: `python3 scripts/send_data.py --help`
Expected: output does not contain `--draw-text`

- [ ] **Step 2: Update the CLI and packet builder**

Modify `scripts/send_data.py` to add:

```python
PKT_TYPE_DRAW_TEXT = 0x11
APP_FONT_12 = 0x01
APP_FONT_16 = 0x02
APP_DRAW_FLAG_CLEAR_FIRST = 0x01
APP_DRAW_FLAG_FULL_REFRESH = 0x02
```

Add CLI arguments:

```python
parser.add_argument('--draw-text', help='Text to draw on slave EPD')
parser.add_argument('--dst', type=lambda x: int(x, 0), help='Destination slave address')
parser.add_argument('--x', type=int, default=0, help='Draw X coordinate')
parser.add_argument('--y', type=int, default=0, help='Draw Y coordinate')
parser.add_argument('--font', choices=['12', '16'], default='16', help='Font size')
parser.add_argument('--clear-first', action='store_true', help='Clear display before drawing')
parser.add_argument('--full-refresh', action='store_true', help='Request full display refresh')
```

Add encoder:

```python
def encode_draw_text_payload(dst: int, x: int, y: int, font: str, clear_first: bool, full_refresh: bool, text: str) -> bytes:
    text_bytes = text.encode('ascii')
    font_id = APP_FONT_12 if font == '12' else APP_FONT_16
    flags = 0
    if clear_first:
        flags |= APP_DRAW_FLAG_CLEAR_FIRST
    if full_refresh:
        flags |= APP_DRAW_FLAG_FULL_REFRESH
    return bytes([dst]) + struct.pack('<HHBB', x, y, font_id, flags) + bytes([len(text_bytes)]) + text_bytes
```

Add send path:

```python
elif args.draw_text:
    payload = encode_draw_text_payload(args.dst, args.x, args.y, args.font, args.clear_first, args.full_refresh, args.draw_text)
    frame = build_packet(PKT_TYPE_DRAW_TEXT, payload)
    send_and_wait_ack(ser, frame, "DRAW_TEXT")
```

- [ ] **Step 3: Run help to verify the new command exists**

Run: `python3 scripts/send_data.py --help`
Expected: help output contains `--draw-text`, `--dst`, `--x`, `--y`, `--font`, `--clear-first`, and `--full-refresh`

- [ ] **Step 4: Commit**

```bash
git add scripts/send_data.py
git commit -m "feat: add draw text host command"
```

## Task 11: Bench Verification and Build Variant Check

**Files:**
- Modify: `Core/Inc/openperiph_config.h`
- Test: `make -C Debug clean all`
- Test: `python3 scripts/send_data.py --help`

- [ ] **Step 1: Build the master variant**

Set in `Core/Inc/openperiph_config.h`:

```c
#define OPENPERIPH_ROLE OPENPERIPH_ROLE_MASTER
#define OPENPERIPH_NODE_ADDR 0x01U
```

Run: `make -C Debug clean all`
Expected: PASS and emit `openPeriph_monorepo.elf`

- [ ] **Step 2: Build the slave variant**

Change `Core/Inc/openperiph_config.h`:

```c
#define OPENPERIPH_ROLE OPENPERIPH_ROLE_SLAVE
#define OPENPERIPH_NODE_ADDR 0x22U
```

Run: `make -C Debug clean all`
Expected: PASS and emit `openPeriph_monorepo.elf`

- [ ] **Step 3: Run host-side smoke checks**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_protocol.c Core/Src/app_protocol.c -o /tmp/test_app_protocol && /tmp/test_app_protocol
python3 scripts/send_data.py --help
```

Expected:

- both host tests exit `0`
- CLI help contains `--draw-text`

- [ ] **Step 4: Bench run**

Flash one board as master and one as slave.

Run from host:

```bash
python3 scripts/send_data.py --port /dev/ttyACM0 --draw-text "Hello EPD" --dst 0x22 --x 10 --y 20 --font 16 --clear-first --full-refresh
```

Expected:

- host prints `ACK received`
- target slave updates the EPD with `Hello EPD`
- any non-target slave ignores the packet

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/openperiph_config.h
git commit -m "test: verify master and slave build variants"
```
