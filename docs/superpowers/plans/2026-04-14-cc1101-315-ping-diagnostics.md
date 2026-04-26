# CC1101 315 MHz Diagnostics And RF Ping Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retune the CC1101 path to 315 MHz, extend `statusm` with live CC1101 identity reads, and add a host-visible RF ping/pong diagnostic between boards.

**Architecture:** Keep the current split between local USB commands, master-only orchestration, and slave-side RF polling. Add a narrow chip-info helper to the CC1101 driver, keep the 315 MHz defaults as named constants, and implement RF ping/pong as a small extension of the existing `RfFrame_t` transport plus a synchronous master-side wait helper in `rf_link.c`.

**Tech Stack:** STM32 HAL / C11 firmware, header-inline app modules, Python 3 host CLI, `pyserial`, host-side `cc` test binaries, CubeIDE-generated `Debug/makefile`

---

## File Map

- Modify: `Core/Inc/cc1101_radio.h`
  - Add named default register constants for the 315 MHz profile.
  - Add a narrow chip-info read API.
- Modify: `Core/Src/cc1101_radio.c`
  - Read `PARTNUM` and `VERSION`.
  - Use the 315 MHz register constants in `Cc1101_WriteSettings()`.
- Modify: `Core/Inc/app_commands.h`
  - Extend `CMD_GET_STATUS` to emit a 10-byte payload with chip identity bytes.
- Modify: `Core/Inc/usb_protocol.h`
  - Add `CMD_RF_PING`.
- Modify: `Core/Inc/rf_frame.h`
  - Add `RF_MSG_PING` and `RF_MSG_PONG`.
- Modify: `Core/Inc/rf_link.h`
  - Add a typed ping result enum and the synchronous wait helper used by the master.
- Modify: `Core/Src/rf_link.c`
  - Implement send-and-wait-for-pong retry logic.
- Modify: `Core/Inc/app_master.h`
  - Intercept `CMD_RF_PING` before falling back to local commands.
  - Map ping results to ACK / NACK reasons.
- Modify: `Core/Inc/app_slave.h`
  - Reply to `RF_MSG_PING` with `RF_MSG_PONG`.
  - Preserve existing draw-text behavior.
- Modify: `scripts/send_data.py`
  - Parse and print the longer status payload.
  - Add `--rf-ping`.
- Modify: `tests/host/test_app_commands.c`
  - Cover chip-info success and fallback bytes in `CMD_GET_STATUS`.
- Modify: `tests/host/test_app_master_commands.c`
  - Cover RF ping ACK / NACK handling in the master command path.
- Modify: `tests/host/test_rf_frame.c`
  - Cover zero-payload ping/pong frames.
- Modify: `tests/host/test_send_data.py`
  - Cover the longer status output and RF ping CLI output.
- Create: `tests/host/test_cc1101_radio_constants.c`
  - Assert the 315 MHz default constants.
- Create: `tests/host/test_app_slave.c`
  - Assert ping-to-pong behavior in the slave polling path.

### Verification Commands

- Status command host test:
  - `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_commands.c -o /tmp/test_app_commands && /tmp/test_app_commands`
- Master command host test:
  - `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands`
- Slave poll host test:
  - `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave`
- RF frame host test:
  - `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame`
- Radio constant host test:
  - `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_cc1101_radio_constants.c -o /tmp/test_cc1101_radio_constants && /tmp/test_cc1101_radio_constants`
- Python host CLI test:
  - `PYTHONPATH=. python3 -m unittest discover -s tests/host -p 'test_send_data.py' -v`
- Firmware build:
  - `make -C Debug -j4`

### NACK Reason Allocation

Keep existing reason bytes already used in local commands, and use these additional values for RF ping:

- `0x03`: malformed command payload
- `0x05`: RF transmit failure
- `0x06`: RF pong timeout
- `0x07`: invalid destination address, including self-ping

## Task 1: Extend `statusm` With Live CC1101 Identity Bytes

**Files:**
- Modify: `Core/Inc/cc1101_radio.h`
- Modify: `Core/Src/cc1101_radio.c`
- Modify: `Core/Inc/app_commands.h`
- Modify: `tests/host/test_app_commands.c`
- Modify: `tests/host/test_app_master_commands.c`

- [ ] **Step 1: Write the failing status tests**

Add chip-info stub state and 10-byte status expectations to `tests/host/test_app_commands.c`:

```c
static bool g_chip_info_ok;
static uint8_t g_chip_partnum;
static uint8_t g_chip_version;

bool Cc1101Radio_ReadChipInfo(uint8_t *partnum, uint8_t *version)
{
    if (!g_chip_info_ok || partnum == NULL || version == NULL) {
        return false;
    }
    *partnum = g_chip_partnum;
    *version = g_chip_version;
    return true;
}
```

```c
    g_chip_info_ok = true;
    g_chip_partnum = 0x00U;
    g_chip_version = 0x14U;

    assert(AppCommands_HandleLocalCommand(&pkt));
    assert(g_last_type == PKT_TYPE_STATUS);
    assert(g_last_payload_len == 10U);
    assert(g_last_payload[8] == 0x00U);
    assert(g_last_payload[9] == 0x14U);

    ResetCaptures();
    g_chip_info_ok = false;

    assert(AppCommands_HandleLocalCommand(&pkt));
    assert(g_last_payload_len == 10U);
    assert(g_last_payload[8] == 0xFFU);
    assert(g_last_payload[9] == 0xFFU);
```

Mirror the longer status payload and chip-info stub in `tests/host/test_app_master_commands.c`:

```c
static bool g_chip_info_ok;
static uint8_t g_chip_partnum;
static uint8_t g_chip_version;

bool Cc1101Radio_ReadChipInfo(uint8_t *partnum, uint8_t *version)
{
    if (!g_chip_info_ok || partnum == NULL || version == NULL) {
        return false;
    }
    *partnum = g_chip_partnum;
    *version = g_chip_version;
    return true;
}
```

```c
    g_chip_info_ok = true;
    g_chip_partnum = 0x00U;
    g_chip_version = 0x14U;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_payload_len == 10U);
    assert(g_last_payload[8] == 0x00U);
    assert(g_last_payload[9] == 0x14U);
```

- [ ] **Step 2: Run the failing status tests**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_commands.c -o /tmp/test_app_commands && /tmp/test_app_commands
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
```

Expected:

- both test binaries fail to compile because `Cc1101Radio_ReadChipInfo()` does not exist yet
- or they compile and fail because `g_last_payload_len` is still `8`

- [ ] **Step 3: Implement the chip-info helper and 10-byte status payload**

Update `Core/Inc/cc1101_radio.h`:

```c
bool Cc1101Radio_ReadChipInfo(uint8_t *partnum, uint8_t *version);
```

Add chip-info register reads in `Core/Src/cc1101_radio.c`:

```c
#define CC1101_REG_PARTNUM    0x30U
#define CC1101_REG_VERSION    0x31U
```

```c
static bool Cc1101_ReadStatusValue(uint8_t addr, uint8_t *value)
{
    uint8_t first = 0U;
    uint8_t second = 0U;

    if (value == NULL) {
        return false;
    }

    for (uint8_t attempt = 0U; attempt < 4U; ++attempt) {
        if (!Cc1101_ReadBurst(addr, &first, 1U)) {
            return false;
        }
        if (!Cc1101_ReadBurst(addr, &second, 1U)) {
            return false;
        }
        if (first == second) {
            *value = second;
            return true;
        }
    }

    *value = second;
    return true;
}

bool Cc1101Radio_ReadChipInfo(uint8_t *partnum, uint8_t *version)
{
    if (!s_radio_initialized || partnum == NULL || version == NULL) {
        return false;
    }

    return Cc1101_ReadStatusValue(CC1101_REG_PARTNUM, partnum) &&
           Cc1101_ReadStatusValue(CC1101_REG_VERSION, version);
}
```

Extend `Core/Inc/app_commands.h`:

```c
    uint8_t status_payload[10];
```

```c
        {
            uint8_t partnum = 0xFFU;
            uint8_t version = 0xFFU;
            if (Cc1101Radio_ReadChipInfo(&partnum, &version)) {
                status_payload[8] = partnum;
                status_payload[9] = version;
            } else {
                status_payload[8] = 0xFFU;
                status_payload[9] = 0xFFU;
            }
        }
```

- [ ] **Step 4: Run the status tests again**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_commands.c -o /tmp/test_app_commands && /tmp/test_app_commands
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
```

Expected:

- both test binaries exit successfully
- the status payload assertions pass with `0x00/0x14` on success and `0xFF/0xFF` on read failure

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/cc1101_radio.h Core/Src/cc1101_radio.c Core/Inc/app_commands.h tests/host/test_app_commands.c tests/host/test_app_master_commands.c
git commit -m "feat: add cc1101 status diagnostics"
```

## Task 2: Retune The Default CC1101 Profile To 315 MHz

**Files:**
- Modify: `Core/Inc/cc1101_radio.h`
- Modify: `Core/Src/cc1101_radio.c`
- Create: `tests/host/test_cc1101_radio_constants.c`

- [ ] **Step 1: Write the failing constant test**

Create `tests/host/test_cc1101_radio_constants.c`:

```c
#include "cc1101_radio.h"

#include <assert.h>

int main(void)
{
    assert(CC1101_DEFAULT_FREQ2 == 0x0CU);
    assert(CC1101_DEFAULT_FREQ1 == 0x1DU);
    assert(CC1101_DEFAULT_FREQ0 == 0x8AU);
    assert(CC1101_DEFAULT_TEST0 == 0x0BU);
    assert(CC1101_DEFAULT_PATABLE_ENTRY == 0xC0U);
    return 0;
}
```

- [ ] **Step 2: Run the failing constant test**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_cc1101_radio_constants.c -o /tmp/test_cc1101_radio_constants && /tmp/test_cc1101_radio_constants
```

Expected:

- the test fails to compile until the named constants are present in `cc1101_radio.h`

- [ ] **Step 3: Replace the hard-coded 433 MHz literals with 315 MHz defaults**

In `Core/Src/cc1101_radio.c`, replace the frequency-dependent literals:

```c
static const uint8_t kCc1101PaTable[8] = {
    CC1101_DEFAULT_PATABLE_ENTRY,
    CC1101_DEFAULT_PATABLE_ENTRY,
    CC1101_DEFAULT_PATABLE_ENTRY,
    CC1101_DEFAULT_PATABLE_ENTRY,
    CC1101_DEFAULT_PATABLE_ENTRY,
    CC1101_DEFAULT_PATABLE_ENTRY,
    CC1101_DEFAULT_PATABLE_ENTRY,
    CC1101_DEFAULT_PATABLE_ENTRY,
};
```

```c
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_FREQ2, CC1101_DEFAULT_FREQ2);
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_FREQ1, CC1101_DEFAULT_FREQ1);
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_FREQ0, CC1101_DEFAULT_FREQ0);
```

```c
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_TEST0, CC1101_DEFAULT_TEST0);
```

Do not change the proven packet-format settings in this task:

```c
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_SYNC1, 0xD3U);
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_SYNC0, 0x91U);
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_PKTCTRL1, 0x04U);
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_PKTCTRL0, 0x05U);
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_MDMCFG4, 0x8AU);
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_MDMCFG3, 0x83U);
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_MDMCFG2, 0x12U);
    CC1101_WRITE_SETTING_OR_RETURN(CC1101_REG_DEVIATN, 0x34U);
```

- [ ] **Step 4: Run the constant test and a firmware build**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_cc1101_radio_constants.c -o /tmp/test_cc1101_radio_constants && /tmp/test_cc1101_radio_constants
make -C Debug -j4
```

Expected:

- the host constant test exits successfully
- the firmware build completes without introducing `cc1101_radio.c` compile errors

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/cc1101_radio.h Core/Src/cc1101_radio.c tests/host/test_cc1101_radio_constants.c
git commit -m "feat: retune cc1101 defaults to 315mhz"
```

## Task 3: Add RF Ping/Pong To The Firmware Path

**Files:**
- Modify: `Core/Inc/usb_protocol.h`
- Modify: `Core/Inc/rf_frame.h`
- Modify: `Core/Inc/rf_link.h`
- Modify: `Core/Src/rf_link.c`
- Modify: `Core/Inc/app_master.h`
- Modify: `Core/Inc/app_slave.h`
- Modify: `tests/host/test_app_master_commands.c`
- Modify: `tests/host/test_rf_frame.c`
- Create: `tests/host/test_app_slave.c`

- [ ] **Step 1: Write the failing RF ping/pong tests**

Extend `tests/host/test_app_master_commands.c` with a stubbed ping result:

```c
static RfLinkPingResult_t g_ping_result;
static uint8_t g_last_ping_dst;
static uint8_t g_last_ping_seq;

RfLinkPingResult_t RfLink_SendPingAndWaitForPong(uint8_t dst_addr, uint8_t seq)
{
    g_last_ping_dst = dst_addr;
    g_last_ping_seq = seq;
    return g_ping_result;
}
```

```c
    ResetCaptures();
    pkt.id = 0x52U;
    pkt.payload_len = 2U;
    pkt.payload[0] = CMD_RF_PING;
    pkt.payload[1] = 0x22U;
    g_ping_result = RF_LINK_PING_RESULT_OK;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_ping_dst == 0x22U);
    assert(g_last_ping_seq == 0x52U);
    assert(g_last_ack_id == 0x52U);

    ResetCaptures();
    g_ping_result = RF_LINK_PING_RESULT_TIMEOUT;
    AppMaster_HandleUsbPacket(&pkt);

    assert(g_last_nack_id == 0x52U);
    assert(g_last_nack_reason == 0x06U);
```

Extend `tests/host/test_rf_frame.c` with a zero-payload ping frame:

```c
    RfFrame_t ping = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_PING,
        .dst_addr = 0x22,
        .src_addr = 0x01,
        .seq = 0x09,
        .payload_len = 0,
    };
    used = RfFrame_Encode(&ping, encoded, sizeof(encoded));
    assert(used == 6U);
    assert(RfFrame_Decode(encoded, used, &decoded));
    assert(decoded.msg_type == RF_MSG_PING);
    assert(decoded.payload_len == 0U);
```

Create `tests/host/test_app_slave.c`:

```c
#include "app_slave.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

static bool g_have_frame;
static RfFrame_t g_rx_frame;
static bool g_send_called;
static RfFrame_t g_tx_frame;

bool DisplayService_Init(void) { return true; }
bool DisplayService_DrawText(const AppDrawTextCommand_t *cmd) { (void)cmd; return true; }

bool RfLink_TryReceiveFrame(RfFrame_t *out_frame)
{
    if (!g_have_frame || out_frame == NULL) {
        return false;
    }
    *out_frame = g_rx_frame;
    g_have_frame = false;
    return true;
}

bool RfLink_IsForLocalNode(const RfFrame_t *frame)
{
    return frame != NULL && frame->dst_addr == OPENPERIPH_NODE_ADDR;
}

bool RfLink_SendFrame(const RfFrame_t *frame)
{
    assert(frame != NULL);
    g_send_called = true;
    g_tx_frame = *frame;
    return true;
}

int main(void)
{
    memset(&g_rx_frame, 0, sizeof(g_rx_frame));
    g_rx_frame.version = RF_FRAME_VERSION;
    g_rx_frame.msg_type = RF_MSG_PING;
    g_rx_frame.dst_addr = OPENPERIPH_NODE_ADDR;
    g_rx_frame.src_addr = 0x22U;
    g_rx_frame.seq = 0x19U;
    g_have_frame = true;

    AppSlave_Poll();

    assert(g_send_called);
    assert(g_tx_frame.msg_type == RF_MSG_PONG);
    assert(g_tx_frame.dst_addr == 0x22U);
    assert(g_tx_frame.src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_tx_frame.seq == 0x19U);
    assert(g_tx_frame.payload_len == 0U);
    return 0;
}
```

- [ ] **Step 2: Run the failing RF ping/pong tests**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave
```

Expected:

- the master test fails to compile until `CMD_RF_PING`, `RfLinkPingResult_t`, and `RfLink_SendPingAndWaitForPong()` exist
- the RF frame test fails until `RF_MSG_PING` exists
- the slave test fails until `RF_MSG_PONG` handling exists in `AppSlave_Poll()`

- [ ] **Step 3: Implement the RF ping/pong path**

Extend `Core/Inc/usb_protocol.h`:

```c
    CMD_LOCAL_HELLO     = 0x06,
    CMD_RF_PING         = 0x07,
```

Extend `Core/Inc/rf_frame.h`:

```c
    RF_MSG_DRAW_TEXT = 0x01,
    RF_MSG_PING = 0x02,
    RF_MSG_PONG = 0x03,
    RF_MSG_ACK = 0x80,
    RF_MSG_ERROR = 0x81,
```

Add a typed ping helper contract to `Core/Inc/rf_link.h`:

```c
typedef enum {
    RF_LINK_PING_RESULT_OK = 0,
    RF_LINK_PING_RESULT_SEND_FAIL,
    RF_LINK_PING_RESULT_TIMEOUT,
} RfLinkPingResult_t;

RfLinkPingResult_t RfLink_SendPingAndWaitForPong(uint8_t dst_addr, uint8_t seq);
```

Implement the wait helper in `Core/Src/rf_link.c`:

```c
#define RF_LINK_PING_RETRIES 3U
#define RF_LINK_PING_TIMEOUT_MS 75U
```

```c
RfLinkPingResult_t RfLink_SendPingAndWaitForPong(uint8_t dst_addr, uint8_t seq)
{
    RfFrame_t frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_PING,
        .dst_addr = dst_addr,
        .src_addr = OPENPERIPH_NODE_ADDR,
        .seq = seq,
        .payload_len = 0U,
    };

    for (uint8_t attempt = 0U; attempt < RF_LINK_PING_RETRIES; ++attempt) {
        uint32_t start_tick;
        RfFrame_t rx_frame;

        if (!RfLink_SendFrame(&frame)) {
            return RF_LINK_PING_RESULT_SEND_FAIL;
        }

        start_tick = HAL_GetTick();
        while ((HAL_GetTick() - start_tick) < RF_LINK_PING_TIMEOUT_MS) {
            if (!RfLink_TryReceiveFrame(&rx_frame)) {
                continue;
            }
            if (rx_frame.msg_type == RF_MSG_PONG &&
                rx_frame.src_addr == dst_addr &&
                rx_frame.dst_addr == OPENPERIPH_NODE_ADDR &&
                rx_frame.seq == seq) {
                return RF_LINK_PING_RESULT_OK;
            }
        }
    }

    return RF_LINK_PING_RESULT_TIMEOUT;
}
```

Handle the command in `Core/Inc/app_master.h`:

```c
static inline bool AppMaster_HandleRfPingCommand(const Packet_t *pkt)
{
    RfLinkPingResult_t result;
    uint8_t dst_addr;

    if (pkt == NULL || pkt->payload_len != 2U) {
        OpenPeriph_SendUsbNack(pkt != NULL ? pkt->id : 0U, 0x03U);
        return true;
    }

    dst_addr = pkt->payload[1];
    if (dst_addr == OPENPERIPH_NODE_ADDR) {
        OpenPeriph_SendUsbNack(pkt->id, 0x07U);
        return true;
    }

    result = RfLink_SendPingAndWaitForPong(dst_addr, pkt->id);
    if (result == RF_LINK_PING_RESULT_OK) {
        OpenPeriph_SendUsbAck(pkt->id);
    } else if (result == RF_LINK_PING_RESULT_TIMEOUT) {
        OpenPeriph_SendUsbNack(pkt->id, 0x06U);
    } else {
        OpenPeriph_SendUsbNack(pkt->id, 0x05U);
    }

    return true;
}
```

```c
static inline void AppMaster_HandleCommand(const Packet_t *pkt)
{
    if (pkt != NULL && pkt->payload_len >= 1U && pkt->payload[0] == CMD_RF_PING) {
        (void)AppMaster_HandleRfPingCommand(pkt);
        return;
    }
    if (!AppCommands_HandleLocalCommand(pkt)) {
        OpenPeriph_SendUsbNack(pkt->id, 0x04U);
    }
}
```

Handle ping in `Core/Inc/app_slave.h`:

```c
    if (frame.msg_type == RF_MSG_PING) {
        RfFrame_t pong = {
            .version = RF_FRAME_VERSION,
            .msg_type = RF_MSG_PONG,
            .dst_addr = frame.src_addr,
            .src_addr = OPENPERIPH_NODE_ADDR,
            .seq = frame.seq,
            .payload_len = 0U,
        };
        (void)RfLink_SendFrame(&pong);
        return;
    }
```

- [ ] **Step 4: Run the RF ping/pong tests again**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave
```

Expected:

- all three binaries exit successfully
- the master test covers ACK and timeout NACK behavior
- the slave test confirms a ping becomes a pong with swapped source and destination

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/usb_protocol.h Core/Inc/rf_frame.h Core/Inc/rf_link.h Core/Src/rf_link.c Core/Inc/app_master.h Core/Inc/app_slave.h tests/host/test_app_master_commands.c tests/host/test_rf_frame.c tests/host/test_app_slave.c
git commit -m "feat: add rf ping pong diagnostics"
```

## Task 4: Update The Host CLI And Final Verification

**Files:**
- Modify: `scripts/send_data.py`
- Modify: `tests/host/test_send_data.py`

- [ ] **Step 1: Write the failing Python tests**

Extend `tests/host/test_send_data.py`:

```python
    def test_status_reports_chip_identity(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_STATUS,
            bytes([1, 0, 0x0D, 0, 0, 0, 0, 1, 0x00, 0x14]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_get_status(ser)

        self.assertIn('CC1101 PARTNUM: 0x00', out.getvalue())
        self.assertIn('CC1101 VERSION: 0x14', out.getvalue())
```

```python
    def test_rf_ping_reports_success(self):
        response = send_data.build_packet(send_data.PKT_TYPE_ACK, b'')
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_rf_ping(ser, 0x22)

        self.assertIn('Sent RF_PING to 0x22', out.getvalue())
        self.assertIn('PONG from 0x22', out.getvalue())
```

```python
    def test_rf_ping_reports_timeout(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_NACK,
            bytes([0x00, 0x06]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_rf_ping(ser, 0x22)

        self.assertIn('No RF pong from node 0x22', out.getvalue())
```

- [ ] **Step 2: Run the failing Python tests**

Run:

```bash
PYTHONPATH=. python3 -m unittest discover -s tests/host -p 'test_send_data.py' -v
```

Expected:

- the suite fails because `send_rf_ping()` does not exist yet
- or the status output is missing `PARTNUM` and `VERSION`

- [ ] **Step 3: Implement the CLI updates**

Extend `scripts/send_data.py` constants:

```python
CMD_RF_PING = 0x07
```

Update `send_get_status()`:

```python
        if len(p) >= 10:
            print(f"  Firmware: v{p[0]}.{p[1]}")
            print(f"  CC1101 MARCSTATE: 0x{p[2]:02X}")
            print(f"  RX buffer used: {p[3] | (p[4] << 8)} bytes")
            print(f"  Error count: {p[5] | (p[6] << 8)}")
            print(f"  CC1101 PARTNUM: 0x{p[8]:02X}")
            print(f"  CC1101 VERSION: 0x{p[9]:02X}")
```

Add `send_rf_ping()`:

```python
def send_rf_ping(ser, dst_addr: int):
    payload = bytes([CMD_RF_PING, dst_addr & 0xFF])
    frame = build_packet(PKT_TYPE_COMMAND, payload)
    start = time.monotonic()
    ser.write(frame)
    print(f"Sent RF_PING to 0x{dst_addr:02X}")
    resp = read_response(ser)
    elapsed_ms = int((time.monotonic() - start) * 1000)

    if resp['valid'] and resp['type'] == PKT_TYPE_ACK:
        print(f"PONG from 0x{dst_addr:02X} (host RTT {elapsed_ms} ms)")
    elif resp['valid'] and resp['type'] == PKT_TYPE_NACK and len(resp['payload']) >= 2:
        reason = resp['payload'][1]
        if reason == 0x06:
            print(f"No RF pong from node 0x{dst_addr:02X}")
        elif reason == 0x07:
            print(f"Invalid RF ping destination: 0x{dst_addr:02X}")
        else:
            print(f"RF ping failed (reason=0x{reason:02X})")
    else:
        print("No valid RF ping response.")
```

Update the CLI parser and dispatch:

```python
    parser.add_argument('--rf-ping', type=lambda x: int(x, 0),
                        help='Send an RF ping to a destination node address')
```

```python
        elif args.rf_ping is not None:
            send_rf_ping(ser, args.rf_ping)
```

```python
            print("No action specified. Use --ping, --status, --rf-ping, --image, --email, --text, or --draw-text")
```

- [ ] **Step 4: Run the Python tests, host C tests, and firmware build**

Run:

```bash
PYTHONPATH=. python3 -m unittest discover -s tests/host -p 'test_send_data.py' -v
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_commands.c -o /tmp/test_app_commands && /tmp/test_app_commands
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_cc1101_radio_constants.c -o /tmp/test_cc1101_radio_constants && /tmp/test_cc1101_radio_constants
make -C Debug -j4
```

Expected:

- all host tests pass
- the Python CLI tests pass
- the firmware build succeeds

Manual hardware smoke after flashing:

```bash
python3 scripts/send_data.py --port /dev/ttyACM0 --status
python3 scripts/send_data.py --port /dev/ttyACM0 --rf-ping 0x22
python3 scripts/send_data.py --port /dev/ttyACM0 --draw-text "radio ok" --dst 0x22 --x 0 --y 0 --font 16 --clear-first
```

Expected:

- `--status` prints `PARTNUM` and `VERSION`
- `--rf-ping` prints a `PONG from 0x22` line
- `--draw-text` still succeeds after the 315 MHz retune

- [ ] **Step 5: Commit**

```bash
git add scripts/send_data.py tests/host/test_send_data.py
git commit -m "feat: add cli rf ping diagnostics"
```
