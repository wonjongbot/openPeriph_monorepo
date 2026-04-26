# RF Draw Staging Reliability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace fire-and-forget RF draw-text with a staged stop-and-wait transfer, expose retry telemetry to the host, and add bounded RF diagnostics that make later CC1101 tuning measurable.

**Architecture:** Keep `RfFrame_t` as the transport envelope and use its existing `seq` header byte as the draw transaction id. Add a small `rf_draw_protocol` codec for staged draw payloads, keep request/response waiting in `rf_link.c`, stage exactly one in-progress draw on the slave, and only ACK USB draw-text after the slave confirms commit success. Extend the Python host CLI to print retries and run repeated RF ping diagnostics with summarized loss metrics.

**Tech Stack:** STM32 HAL / C11 firmware, header-inline app modules, Python 3 host CLI, `pyserial`, host-side `cc` test binaries, CubeIDE-generated `Debug/makefile`

---

## File Map

- Create: `Core/Inc/rf_draw_protocol.h`
  - Define staged draw payload types, constants, error codes, and encode/decode helpers.
- Create: `Core/Src/rf_draw_protocol.c`
  - Implement payload encode/decode for `DRAW_START`, `DRAW_CHUNK`, `DRAW_ACK`, and `DRAW_ERROR`.
- Modify: `Core/Inc/rf_frame.h`
  - Add staged draw RF message types.
- Modify: `Core/Inc/rf_link.h`
  - Add exchange stats, timeout constants, and request/response helpers used by ping and staged draw.
- Modify: `Core/Src/rf_link.c`
  - Add bounded absolute-deadline waiting and retry telemetry.
- Modify: `Core/Inc/app_master.h`
  - Replace one-shot draw send with staged `START -> CHUNK -> COMMIT`.
  - Map RF outcomes to USB ACK/NACK and optional debug/status payloads.
- Modify: `Core/Inc/app_slave.h`
  - Add one-transaction staged draw state machine and reply path.
- Modify: `scripts/send_data.py`
  - Print retry statistics for draw-text and RF ping.
  - Add repeated RF ping diagnostic mode with summary stats.
- Modify: `tests/host/test_rf_frame.c`
  - Cover new staged draw RF message types.
- Create: `tests/host/test_rf_draw_protocol.c`
  - Cover staged draw payload encoding and decoding.
- Modify: `tests/host/test_rf_link.c`
  - Cover absolute timeout and retry telemetry.
- Modify: `tests/host/test_app_slave.c`
  - Cover staged start/chunk/commit behavior, duplicate chunk ACK, and reset-on-new-start.
- Modify: `tests/host/test_app_master_commands.c`
  - Cover staged draw orchestration, chunk retries, and draw failure mapping.
- Modify: `tests/host/test_send_data.py`
  - Cover retry reporting and repeated ping summary output.

## Verification Commands

- RF frame test:
  - `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame`
- RF draw protocol test:
  - `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_draw_protocol.c Core/Src/rf_draw_protocol.c -o /tmp/test_rf_draw_protocol && /tmp/test_rf_draw_protocol`
- RF link test:
  - `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_link.c Core/Src/rf_link.c Core/Src/rf_frame.c Core/Src/rf_draw_protocol.c -o /tmp/test_rf_link && /tmp/test_rf_link`
- Slave poll test:
  - `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave`
- Master command test:
  - `cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands`
- Python host CLI test:
  - `PYTHONPATH=. python3 -m unittest discover -s tests/host -p 'test_send_data.py' -v`
- Firmware build:
  - `make -C Debug -j4`

## Protocol Decisions To Implement

- Use `RfFrame_t.seq` as the staged draw transaction id. Do not duplicate transaction id in payloads.
- Add these RF message types:
  - `RF_MSG_DRAW_START = 0x01`
  - `RF_MSG_DRAW_CHUNK = 0x02`
  - `RF_MSG_DRAW_COMMIT = 0x03`
  - `RF_MSG_DRAW_ACK = 0x04`
  - `RF_MSG_DRAW_ERROR = 0x05`
  - Move existing ping/pong to later values to avoid overlap.
- `DRAW_START` payload layout:
  - `x` little-endian (2 bytes)
  - `y` little-endian (2 bytes)
  - `font_id` (1 byte)
  - `flags` (1 byte)
  - `total_text_len` (1 byte)
- `DRAW_CHUNK` payload layout:
  - `chunk_index` (1 byte)
  - `chunk_len` (1 byte)
  - `chunk_data` (`chunk_len` bytes)
- `DRAW_COMMIT` payload layout:
  - empty
- `DRAW_ACK` payload layout:
  - `phase` (1 byte: start/chunk/commit)
  - `value` (1 byte: chunk index for chunk ACK, `0` otherwise)
- `DRAW_ERROR` payload layout:
  - `phase` (1 byte)
  - `reason` (1 byte)
- Default reliability constants:
  - step retries: `8`
  - ping total timeout: `600 ms`
  - draw total timeout: `2000 ms`
  - per-attempt wait window: `75 ms`

## Task 1: Add Staged Draw Protocol Types And Codec

**Files:**
- Modify: `Core/Inc/rf_frame.h`
- Create: `Core/Inc/rf_draw_protocol.h`
- Create: `Core/Src/rf_draw_protocol.c`
- Modify: `tests/host/test_rf_frame.c`
- Create: `tests/host/test_rf_draw_protocol.c`

- [ ] **Step 1: Write the failing RF frame and staged payload tests**

Create `tests/host/test_rf_draw_protocol.c`:

```c
#include "rf_draw_protocol.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    RfDrawStart_t start = {
        .x = 12U,
        .y = 34U,
        .font_id = APP_FONT_16,
        .flags = APP_DRAW_FLAG_CLEAR_FIRST,
        .total_text_len = 5U,
    };
    RfDrawStart_t decoded_start;
    RfDrawChunk_t chunk = {
        .chunk_index = 1U,
        .chunk_len = 3U,
        .data = { 'a', 'b', 'c' },
    };
    RfDrawChunk_t decoded_chunk;
    uint8_t buf[RF_FRAME_MAX_PAYLOAD];
    size_t len;

    len = RfDrawProtocol_EncodeStart(&start, buf, sizeof(buf));
    assert(len == RF_DRAW_START_PAYLOAD_LEN);
    assert(RfDrawProtocol_DecodeStart(buf, len, &decoded_start));
    assert(decoded_start.x == 12U);
    assert(decoded_start.y == 34U);
    assert(decoded_start.font_id == APP_FONT_16);
    assert(decoded_start.flags == APP_DRAW_FLAG_CLEAR_FIRST);
    assert(decoded_start.total_text_len == 5U);

    len = RfDrawProtocol_EncodeChunk(&chunk, buf, sizeof(buf));
    assert(len == 5U);
    assert(RfDrawProtocol_DecodeChunk(buf, len, &decoded_chunk));
    assert(decoded_chunk.chunk_index == 1U);
    assert(decoded_chunk.chunk_len == 3U);
    assert(memcmp(decoded_chunk.data, "abc", 3U) == 0);

    assert(!RfDrawProtocol_DecodeChunk(buf, 2U, &decoded_chunk));
    return 0;
}
```

Extend `tests/host/test_rf_frame.c` with staged draw RF message coverage:

```c
    RfFrame_t frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_START,
        .dst_addr = 0x22U,
        .src_addr = 0x20U,
        .seq = 0x33U,
        .payload_len = 0U,
    };
```

- [ ] **Step 2: Run the tests to verify they fail**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_draw_protocol.c Core/Src/rf_draw_protocol.c -o /tmp/test_rf_draw_protocol && /tmp/test_rf_draw_protocol
```

Expected:

- `test_rf_frame` fails to compile until new RF message constants exist
- `test_rf_draw_protocol` fails to compile because `rf_draw_protocol.h` and `rf_draw_protocol.c` do not exist yet

- [ ] **Step 3: Add RF frame constants and the staged draw codec**

Update `Core/Inc/rf_frame.h`:

```c
typedef enum {
    RF_MSG_DRAW_START = 0x01,
    RF_MSG_DRAW_CHUNK = 0x02,
    RF_MSG_DRAW_COMMIT = 0x03,
    RF_MSG_DRAW_ACK = 0x04,
    RF_MSG_DRAW_ERROR = 0x05,
    RF_MSG_PING = 0x06,
    RF_MSG_PONG = 0x07,
} RfMessageType_t;
```

Create `Core/Inc/rf_draw_protocol.h`:

```c
#ifndef RF_DRAW_PROTOCOL_H
#define RF_DRAW_PROTOCOL_H

#include "app_protocol.h"
#include "rf_frame.h"

#define RF_DRAW_START_PAYLOAD_LEN 7U
#define RF_DRAW_ACK_PAYLOAD_LEN 2U
#define RF_DRAW_ERROR_PAYLOAD_LEN 2U
#define RF_DRAW_CHUNK_HEADER_LEN 2U
#define RF_DRAW_CHUNK_MAX_DATA (RF_FRAME_MAX_PAYLOAD - RF_DRAW_CHUNK_HEADER_LEN)

typedef enum {
    RF_DRAW_PHASE_START = 1U,
    RF_DRAW_PHASE_CHUNK = 2U,
    RF_DRAW_PHASE_COMMIT = 3U,
} RfDrawPhase_t;

typedef enum {
    RF_DRAW_ERROR_REASON_BAD_STATE = 1U,
    RF_DRAW_ERROR_REASON_BAD_CHUNK = 2U,
    RF_DRAW_ERROR_REASON_LENGTH = 3U,
    RF_DRAW_ERROR_REASON_RENDER = 4U,
} RfDrawErrorReason_t;

typedef struct {
    uint16_t x;
    uint16_t y;
    uint8_t font_id;
    uint8_t flags;
    uint8_t total_text_len;
} RfDrawStart_t;

typedef struct {
    uint8_t chunk_index;
    uint8_t chunk_len;
    uint8_t data[RF_DRAW_CHUNK_MAX_DATA];
} RfDrawChunk_t;

typedef struct {
    uint8_t phase;
    uint8_t value;
} RfDrawAck_t;

typedef struct {
    uint8_t phase;
    uint8_t reason;
} RfDrawError_t;

size_t RfDrawProtocol_EncodeStart(const RfDrawStart_t *start, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeStart(const uint8_t *buf, size_t len, RfDrawStart_t *out_start);
size_t RfDrawProtocol_EncodeChunk(const RfDrawChunk_t *chunk, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeChunk(const uint8_t *buf, size_t len, RfDrawChunk_t *out_chunk);
size_t RfDrawProtocol_EncodeAck(const RfDrawAck_t *ack, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeAck(const uint8_t *buf, size_t len, RfDrawAck_t *out_ack);
size_t RfDrawProtocol_EncodeError(const RfDrawError_t *error, uint8_t *out_buf, size_t out_capacity);
bool RfDrawProtocol_DecodeError(const uint8_t *buf, size_t len, RfDrawError_t *out_error);

#endif
```

Create `Core/Src/rf_draw_protocol.c` with straightforward fixed-layout codecs.

- [ ] **Step 4: Run the tests to verify they pass**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_draw_protocol.c Core/Src/rf_draw_protocol.c -o /tmp/test_rf_draw_protocol && /tmp/test_rf_draw_protocol
```

Expected:

- both binaries exit `0`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/rf_frame.h Core/Inc/rf_draw_protocol.h Core/Src/rf_draw_protocol.c tests/host/test_rf_frame.c tests/host/test_rf_draw_protocol.c
git commit -m "feat: add staged rf draw protocol"
```

## Task 2: Add Bounded RF Exchange Helpers And Telemetry

**Files:**
- Modify: `Core/Inc/rf_link.h`
- Modify: `Core/Src/rf_link.c`
- Modify: `tests/host/test_rf_link.c`

- [ ] **Step 1: Write the failing RF link tests**

Extend `tests/host/test_rf_link.c` with telemetry and absolute-timeout coverage:

```c
    RfLinkExchangeStats_t stats = {0};

    ResetFakes();
    ScriptRxFrame(RF_MSG_PONG, OPENPERIPH_NODE_ADDR, 0x22U, 0x33U, NULL, 0U);
    result = RfLink_SendPingAndWaitForPong(0x22U, 0x33U, &stats);
    assert(result == RF_LINK_PING_RESULT_OK);
    assert(stats.retries_used == 0U);
    assert(stats.attempts_used == 1U);

    ResetFakes();
    g_tick = 700U;
    result = RfLink_SendPingAndWaitForPong(0x22U, 0x33U, &stats);
    assert(result == RF_LINK_PING_RESULT_TIMEOUT);
```

- [ ] **Step 2: Run the RF link test to verify it fails**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_link.c Core/Src/rf_link.c Core/Src/rf_frame.c Core/Src/rf_draw_protocol.c -o /tmp/test_rf_link && /tmp/test_rf_link
```

Expected:

- compile failure because `RfLinkExchangeStats_t` and the new ping signature do not exist yet

- [ ] **Step 3: Implement telemetry-bearing bounded exchange helpers**

Update `Core/Inc/rf_link.h`:

```c
typedef struct {
    uint8_t attempts_used;
    uint8_t retries_used;
    uint16_t elapsed_ms;
    uint8_t remote_phase;
    uint8_t remote_reason;
} RfLinkExchangeStats_t;

#define RF_LINK_MAX_RETRIES 8U
#define RF_LINK_ATTEMPT_TIMEOUT_MS 75U
#define RF_LINK_PING_TOTAL_TIMEOUT_MS 600U
#define RF_LINK_DRAW_TOTAL_TIMEOUT_MS 2000U

RfLinkPingResult_t RfLink_SendPingAndWaitForPong(uint8_t dst_addr,
                                                 uint8_t seq,
                                                 RfLinkExchangeStats_t *out_stats);
```

Add helpers in `Core/Src/rf_link.c`:

```c
static void RfLink_ResetStats(RfLinkExchangeStats_t *stats)
{
    if (stats != NULL) {
        stats->attempts_used = 0U;
        stats->retries_used = 0U;
        stats->elapsed_ms = 0U;
        stats->remote_phase = 0U;
        stats->remote_reason = 0U;
    }
}

static void RfLink_FinalizeStats(RfLinkExchangeStats_t *stats,
                                 uint32_t start_tick,
                                 uint8_t attempts_used)
{
    if (stats != NULL) {
        stats->attempts_used = attempts_used;
        stats->retries_used = (attempts_used > 0U) ? (uint8_t)(attempts_used - 1U) : 0U;
        stats->elapsed_ms = (uint16_t)(HAL_GetTick() - start_tick);
    }
}
```

Use one absolute start tick in `RfLink_SendPingAndWaitForPong()` and stop when either:

- attempts reaches `RF_LINK_MAX_RETRIES`
- `HAL_GetTick() - absolute_start >= RF_LINK_PING_TOTAL_TIMEOUT_MS`

- [ ] **Step 4: Run the RF link test to verify it passes**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_link.c Core/Src/rf_link.c Core/Src/rf_frame.c Core/Src/rf_draw_protocol.c -o /tmp/test_rf_link && /tmp/test_rf_link
```

Expected:

- binary exits `0`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/rf_link.h Core/Src/rf_link.c tests/host/test_rf_link.c
git commit -m "feat: add bounded rf exchange telemetry"
```

## Task 3: Implement Slave-Side Staged Draw State Machine

**Files:**
- Modify: `Core/Inc/app_slave.h`
- Modify: `tests/host/test_app_slave.c`

- [ ] **Step 1: Write the failing slave tests**

Extend `tests/host/test_app_slave.c` to cover staged draw:

```c
    uint8_t start_payload[] = { 0x0CU, 0x00U, 0x22U, 0x00U, APP_FONT_16, APP_DRAW_FLAG_CLEAR_FIRST, 0x05U };
    uint8_t chunk_payload[] = { 0x00U, 0x05U, 'H', 'e', 'l', 'l', 'o' };

    ResetFakes();
    g_try_receive_result = true;
    g_is_for_local_result = true;
    g_send_frame_result = true;
    g_received_frame.msg_type = RF_MSG_DRAW_START;
    g_received_frame.dst_addr = OPENPERIPH_NODE_ADDR;
    g_received_frame.src_addr = 0x22U;
    g_received_frame.seq = 0x5AU;
    g_received_frame.payload_len = sizeof(start_payload);
    memcpy(g_received_frame.payload, start_payload, sizeof(start_payload));
    AppSlave_Poll();
    assert(g_sent_frame.msg_type == RF_MSG_DRAW_ACK);

    g_received_frame.msg_type = RF_MSG_DRAW_CHUNK;
    g_received_frame.payload_len = sizeof(chunk_payload);
    memcpy(g_received_frame.payload, chunk_payload, sizeof(chunk_payload));
    AppSlave_Poll();
    assert(g_sent_frame.msg_type == RF_MSG_DRAW_ACK);
```

Add duplicate re-ACK and new-start-resets-old-state assertions.

- [ ] **Step 2: Run the slave test to verify it fails**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave
```

Expected:

- assertions fail because `AppSlave_Poll()` only handles ping and one-shot draw

- [ ] **Step 3: Implement one-transaction staged draw on the slave**

In `Core/Inc/app_slave.h`, add a small static state object:

```c
typedef struct {
    bool active;
    uint8_t src_addr;
    uint8_t seq;
    uint16_t x;
    uint16_t y;
    uint8_t font_id;
    uint8_t flags;
    uint8_t total_len;
    uint8_t used_len;
    uint8_t next_chunk_index;
    uint8_t last_chunk_index;
    uint8_t text[APP_TEXT_MAX_LEN];
} AppSlaveDrawState_t;

static AppSlaveDrawState_t s_draw_state = {0};
```

Handle staged messages:

```c
    if (frame.msg_type == RF_MSG_DRAW_START) {
        RfDrawStart_t start;
        if (!RfDrawProtocol_DecodeStart(frame.payload, frame.payload_len, &start)) {
            AppSlave_SendDrawError(frame.src_addr, frame.seq, RF_DRAW_PHASE_START, RF_DRAW_ERROR_REASON_LENGTH);
            return;
        }
        memset(&s_draw_state, 0, sizeof(s_draw_state));
        s_draw_state.active = true;
        s_draw_state.src_addr = frame.src_addr;
        s_draw_state.seq = frame.seq;
        s_draw_state.x = start.x;
        s_draw_state.y = start.y;
        s_draw_state.font_id = start.font_id;
        s_draw_state.flags = start.flags;
        s_draw_state.total_len = start.total_text_len;
        AppSlave_SendDrawAck(frame.src_addr, frame.seq, RF_DRAW_PHASE_START, 0U);
        return;
    }
```

Implement chunk, duplicate chunk, and commit handling before the existing ping branch returns.

- [ ] **Step 4: Run the slave test to verify it passes**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave
```

Expected:

- binary exits `0`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/app_slave.h tests/host/test_app_slave.c
git commit -m "feat: stage draw text on slave"
```

## Task 4: Implement Master-Side Staged Draw And USB Result Mapping

**Files:**
- Modify: `Core/Inc/app_master.h`
- Modify: `tests/host/test_app_master_commands.c`

- [ ] **Step 1: Write the failing master tests**

Extend `tests/host/test_app_master_commands.c` with staged draw orchestration:

```c
static bool g_send_request_result;
static uint8_t g_send_request_calls;
static RfLinkExchangeStats_t g_last_stats;

bool RfLink_SendFrameAndAwait(uint8_t dst_addr,
                              const RfFrame_t *request,
                              uint8_t expected_msg_type,
                              uint8_t expected_phase,
                              uint32_t total_timeout_ms,
                              RfLinkExchangeStats_t *out_stats)
{
    (void)dst_addr;
    (void)request;
    (void)expected_msg_type;
    (void)expected_phase;
    ++g_send_request_calls;
    if (out_stats != NULL) {
        *out_stats = g_last_stats;
    }
    return g_send_request_result;
}
```

Test that a `PKT_TYPE_DRAW_TEXT` packet:

- sends `DRAW_START`
- sends the right number of `DRAW_CHUNK` frames
- sends `DRAW_COMMIT`
- only emits USB ACK after commit succeeds

- [ ] **Step 2: Run the master test to verify it fails**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
```

Expected:

- compile failure until the new helper signature exists, or assertion failure because draw-text still uses one RF frame

- [ ] **Step 3: Implement staged draw orchestration in the master**

Add helper functions to `Core/Inc/app_master.h`:

```c
static inline bool AppMaster_SendDrawStart(const AppDrawTextCommand_t *cmd, uint8_t seq)
{
    RfDrawStart_t start = {
        .x = cmd->x,
        .y = cmd->y,
        .font_id = cmd->font_id,
        .flags = cmd->flags,
        .total_text_len = cmd->text_len,
    };
    RfFrame_t frame = {
        .version = RF_FRAME_VERSION,
        .msg_type = RF_MSG_DRAW_START,
        .dst_addr = cmd->dst_addr,
        .src_addr = OPENPERIPH_NODE_ADDR,
        .seq = seq,
    };
    frame.payload_len = (uint8_t)RfDrawProtocol_EncodeStart(&start, frame.payload, sizeof(frame.payload));
    return frame.payload_len == RF_DRAW_START_PAYLOAD_LEN;
}
```

Implement the full send path:

```c
static inline bool AppMaster_SendDrawTextReliable(const Packet_t *pkt,
                                                  uint8_t *out_nack_reason,
                                                  RfLinkExchangeStats_t *out_stats)
{
    AppDrawTextCommand_t cmd;
    uint8_t chunk_index = 0U;
    size_t offset = 0U;

    if ((pkt == NULL) || !AppProtocol_DecodeDrawText(pkt->payload, pkt->payload_len, &cmd)) {
        return false;
    }

    if (!AppMaster_ExchangeDrawStart(&cmd, pkt->id, out_stats)) {
        *out_nack_reason = 0x05U;
        return false;
    }

    while (offset < cmd.text_len) {
        size_t remaining = (size_t)cmd.text_len - offset;
        size_t chunk_len = (remaining > RF_DRAW_CHUNK_MAX_DATA) ? RF_DRAW_CHUNK_MAX_DATA : remaining;
        if (!AppMaster_ExchangeDrawChunk(&cmd, pkt->id, chunk_index, &cmd.text[offset], (uint8_t)chunk_len, out_stats, out_nack_reason)) {
            return false;
        }
        offset += chunk_len;
        ++chunk_index;
    }

    return AppMaster_ExchangeDrawCommit(&cmd, pkt->id, out_stats, out_nack_reason);
}
```

Change `AppMaster_HandleUsbPacket()` so `PKT_TYPE_DRAW_TEXT` only USB-ACKs after `AppMaster_SendDrawTextReliable()` succeeds.

- [ ] **Step 4: Run the master test to verify it passes**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
```

Expected:

- binary exits `0`

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/app_master.h tests/host/test_app_master_commands.c
git commit -m "feat: make rf draw text reliable"
```

## Task 5: Expose Retry Telemetry In The Host Script

**Files:**
- Modify: `scripts/send_data.py`
- Modify: `tests/host/test_send_data.py`

- [ ] **Step 1: Write the failing Python tests**

Extend `tests/host/test_send_data.py` with:

```python
    def test_rf_ping_success_reports_retry_count(self):
        response = send_data.build_packet(send_data.PKT_TYPE_ACK, bytes([0x00, 0x01, 0x2A, 0x00]))
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_rf_ping(ser, 0x22)

        self.assertIn('retry', out.getvalue())
```

Add a repeated ping summary test:

```python
    def test_rf_ping_bench_reports_summary(self):
        out = io.StringIO()
        stats = [
            {'ok': True, 'retries': 0, 'elapsed_ms': 12},
            {'ok': False, 'retries': 8, 'elapsed_ms': 600},
        ]

        with contextlib.redirect_stdout(out):
            send_data.print_rf_ping_bench_summary(stats)

        self.assertIn('2 attempts', out.getvalue())
        self.assertIn('1 success', out.getvalue())
        self.assertIn('1 failed', out.getvalue())
```

- [ ] **Step 2: Run the Python tests to verify they fail**

Run:

```bash
PYTHONPATH=. python3 -m unittest discover -s tests/host -p 'test_send_data.py' -v
```

Expected:

- failures because retry-reporting fields and bench-summary helpers do not exist yet

- [ ] **Step 3: Update the host script**

In `scripts/send_data.py`, parse ACK/NACK payloads for retry telemetry. For successful RF operations print:

```python
print(f"PONG from 0x{dst_addr:02X}: {retries} retries, {rtt_ms} ms")
```

Add repeated ping mode:

```python
parser.add_argument('--rf-ping-bench', type=lambda x: int(x, 0),
                    help='Run repeated RF ping diagnostics against the given destination')
parser.add_argument('--count', type=int, default=20,
                    help='Number of repeated RF diagnostic attempts')
```

Add summary helper:

```python
def print_rf_ping_bench_summary(results):
    total = len(results)
    success = sum(1 for item in results if item['ok'])
    failed = total - success
    retries = sum(item['retries'] for item in results)
    failure_rate = (failed / total * 100.0) if total else 0.0
    print(f"{total} attempts, {success} success, {failed} failed, {retries} total retries, failure rate {failure_rate:.1f}%")
```

- [ ] **Step 4: Run the Python tests to verify they pass**

Run:

```bash
PYTHONPATH=. python3 -m unittest discover -s tests/host -p 'test_send_data.py' -v
```

Expected:

- all tests pass

- [ ] **Step 5: Commit**

```bash
git add scripts/send_data.py tests/host/test_send_data.py
git commit -m "feat: report rf retry diagnostics"
```

## Task 6: Full Verification And Firmware Integration

**Files:**
- Modify as needed based on build failures in previously touched files

- [ ] **Step 1: Run all host-side tests**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_frame.c Core/Src/rf_frame.c -o /tmp/test_rf_frame && /tmp/test_rf_frame
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_draw_protocol.c Core/Src/rf_draw_protocol.c -o /tmp/test_rf_draw_protocol && /tmp/test_rf_draw_protocol
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_rf_link.c Core/Src/rf_link.c Core/Src/rf_frame.c Core/Src/rf_draw_protocol.c -o /tmp/test_rf_link && /tmp/test_rf_link
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
PYTHONPATH=. python3 -m unittest discover -s tests/host -p 'test_send_data.py' -v
```

Expected:

- every binary exits `0`
- Python unittest reports `OK`

- [ ] **Step 2: Build firmware**

Run:

```bash
make -C Debug -j4
```

Expected:

- build completes without compiler errors

- [ ] **Step 3: Bench-check the new transport**

Run examples:

```bash
python3 scripts/send_data.py --port /dev/ttyACM0 --rf-ping 0x22
python3 scripts/send_data.py --port /dev/ttyACM0 --rf-ping-bench 0x22 --count 100
python3 scripts/send_data.py --port /dev/ttyACM0 --draw-text "Hello world" --dst 0x22 --x 0 --y 0 --font 16 --clear-first
```

Expected:

- normal ping prints retries and elapsed time
- bench ping prints aggregate success/failure and retry counts
- draw-text only USB-ACKs after staged commit succeeds

- [ ] **Step 4: Commit the integrated feature**

```bash
git add Core/Inc/rf_link.h Core/Src/rf_link.c Core/Inc/app_master.h Core/Inc/app_slave.h scripts/send_data.py tests/host/test_rf_link.c tests/host/test_app_slave.c tests/host/test_app_master_commands.c tests/host/test_send_data.py
git commit -m "feat: harden rf draw transport and diagnostics"
```

## Post-Implementation RF Tuning Checklist

Run this after the staged transport lands so any remaining loss is easier to attribute:

1. Record `rf-ping-bench` results before any CC1101 register change.
2. Record staged draw results with:
   - 1 character
   - 5 characters
   - 20 characters
   - 40 characters
3. Compare retry rate and failure rate across:
   - current hardware
   - boards with the capacitor mismatch corrected
4. If tiny staged chunks still fail often:
   - verify `FREQ2/FREQ1/FREQ0`, `TEST0`, `PATABLE`, and sync settings against TI documentation
   - confirm antenna and front-end population match the intended 315 MHz design
   - verify crystal and load-capacitor values on both boards
5. Keep one log table with:
   - date
   - board pair
   - capacitor population
   - firmware commit
   - ping success rate
   - draw success rate
   - average retries

