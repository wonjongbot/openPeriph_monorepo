# Lucky Button Agent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the approved event-bridge flow where a slave-side active-low `GPIO_PIN_11` press pulses `PC14`, sends an RF trigger to the master, the master forwards a USB agent event, and a plain Python watcher renders one approved lucky-button mode back to the slave display.

**Architecture:** Firmware stays deterministic: the slave debounces the button and emits `RF_MSG_AGENT_TRIGGER`; the master runs a small idle RF pump and bridges valid trigger frames as `PKT_TYPE_AGENT_EVENT`. Host creativity lives in `scripts/lucky_button.py`, which owns the serial port, chooses `weather`, `art`, `news`, `repo`, or `fortune`, validates bounded ASCII lines, and draws through `EinkCanvas`.

**Tech Stack:** STM32 HAL / C11 firmware, header-inline app modules, host-side `cc` test binaries, Python 3 `unittest`, `pyserial`, existing `scripts/send_data.py` and `scripts/eink_canvas.py`, CubeIDE-generated `Debug/makefile`. Firmware builds should use the STM32 compiler installed under `/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins`.

**STM32 Toolchain:** Before running firmware builds, set `GNU_TOOLS` to the CubeIDE GNU tools plugin and prepend it to `PATH`:

```bash
GNU_TOOLS="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.100.202509120712/tools"
PATH="$GNU_TOOLS/bin:$PATH"
```

---

## File Structure

- Modify `Core/Inc/openperiph_config.h`
  - Add `OPENPERIPH_MASTER_ADDR` defaulting to `0x01U`.
- Modify `Core/Inc/rf_frame.h`
  - Add `RF_MSG_AGENT_TRIGGER = 0x0B`.
- Modify `Core/Inc/usb_protocol.h`
  - Add `PKT_TYPE_AGENT_EVENT = 0x85`.
- Modify `Core/Inc/app_slave.h`
  - Add polled debounce state for active-low `GPIO_PIN_11`.
  - Pulse `PC14` non-blockingly on each accepted press.
  - Send one `RF_MSG_AGENT_TRIGGER` frame per press.
- Modify `Core/Inc/app_master.h`
  - Add parser for `RF_MSG_AGENT_TRIGGER`.
  - Add idle RF event pump that emits `PKT_TYPE_AGENT_EVENT`.
- Modify `Core/Src/main.c`
  - Call the master idle RF event pump from the main loop.
- Modify `tests/host/test_app_slave.c`
  - Add HAL GPIO and tick fakes.
  - Cover debounce, LED pulse, and trigger frame payload.
- Modify `tests/host/test_app_master_commands.c`
  - Capture `OpenPeriph_SendUsbPacket`.
  - Cover valid and malformed agent trigger bridging.
- Modify `scripts/send_data.py`
  - Add `PKT_TYPE_AGENT_EVENT`.
  - Add `parse_agent_event_payload(payload)`.
- Create `scripts/lucky_button.py`
  - Long-running serial watcher with approved modes and `--agent none`.
- Create `tests/host/test_lucky_button.py`
  - Cover packet dispatch, mode selection, line validation, and fallback output.

---

### Task 1: Protocol Constants And Host Packet Helper

**Files:**
- Modify: `Core/Inc/openperiph_config.h`
- Modify: `Core/Inc/rf_frame.h`
- Modify: `Core/Inc/usb_protocol.h`
- Modify: `scripts/send_data.py`
- Test: `tests/host/test_send_data.py`

- [ ] **Step 1: Add the failing Python parser test**

Append this test method inside `class SendDataTests(unittest.TestCase)` in `tests/host/test_send_data.py`:

```python
    def test_parse_agent_event_payload(self):
        payload = bytes([0x22, 0x7A, 0x01, 0x34, 0x12])

        event = send_data.parse_agent_event_payload(payload)

        self.assertEqual(event, {
            'slave_addr': 0x22,
            'event_id': 0x7A,
            'press_type': 0x01,
            'uptime_ms': 0x1234,
        })

        with self.assertRaises(ValueError):
            send_data.parse_agent_event_payload(b'\x22')
```

- [ ] **Step 2: Run the Python test and verify it fails**

Run:

```bash
PYTHONPATH=. python3 -m unittest tests.host.test_send_data.SendDataTests.test_parse_agent_event_payload -v
```

Expected: FAIL with an `AttributeError` that `send_data` has no attribute `parse_agent_event_payload`.

- [ ] **Step 3: Add protocol constants**

In `Core/Inc/openperiph_config.h`, add this block after `OPENPERIPH_NODE_ADDR`:

```c
#ifndef OPENPERIPH_MASTER_ADDR
#define OPENPERIPH_MASTER_ADDR 0x01U
#endif
```

In `Core/Inc/rf_frame.h`, add the trigger message after `RF_MSG_DRAW_TEXT`:

```c
    RF_MSG_AGENT_TRIGGER = 0x0B,
```

In `Core/Inc/usb_protocol.h`, add the USB event packet after `PKT_TYPE_RF_RX_DATA`:

```c
    PKT_TYPE_AGENT_EVENT  = 0x85,  /* Slave button event forwarded to host */
```

- [ ] **Step 4: Add the host parser**

In `scripts/send_data.py`, add the packet constant after `PKT_TYPE_STATUS`:

```python
PKT_TYPE_AGENT_EVENT = 0x85
```

Add this helper after `get_last_rf_ping_result()`:

```python
def parse_agent_event_payload(payload: bytes) -> dict:
    if len(payload) != 5:
        raise ValueError("agent event payload must be exactly 5 bytes")
    return {
        'slave_addr': payload[0],
        'event_id': payload[1],
        'press_type': payload[2],
        'uptime_ms': payload[3] | (payload[4] << 8),
    }
```

- [ ] **Step 5: Run the Python test and verify it passes**

Run:

```bash
PYTHONPATH=. python3 -m unittest tests.host.test_send_data.SendDataTests.test_parse_agent_event_payload -v
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add Core/Inc/openperiph_config.h Core/Inc/rf_frame.h Core/Inc/usb_protocol.h scripts/send_data.py tests/host/test_send_data.py
git commit -m "feat: add lucky button protocol constants"
```

---

### Task 2: Slave Button Debounce, LED Pulse, And RF Trigger

**Files:**
- Modify: `Core/Inc/app_slave.h`
- Modify: `tests/host/test_app_slave.c`

- [ ] **Step 1: Add HAL GPIO fakes and button tests**

In `tests/host/test_app_slave.c`, add these globals after the existing fake globals:

```c
static uint32_t g_tick;
static GPIO_PinState g_button_pin_state;
static GPIO_PinState g_led_pin_state;
static uint16_t g_last_gpio_write_pin;
static uint8_t g_radio_init_calls;
```

Add these fakes before `ResetFakes()`:

```c
uint32_t HAL_GetTick(void)
{
    return g_tick;
}

void HAL_Delay(uint32_t delay)
{
    g_tick += delay;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    (void)GPIOx;
    if (GPIO_Pin == GPIO_PIN_11) {
        return g_button_pin_state;
    }
    return GPIO_PIN_SET;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
    (void)GPIOx;
    g_last_gpio_write_pin = GPIO_Pin;
    if (GPIO_Pin == GPIO_PIN_14) {
        g_led_pin_state = PinState;
    }
}

bool Cc1101Radio_Init(void)
{
    ++g_radio_init_calls;
    return true;
}
```

In `ResetFakes()`, add:

```c
    g_tick = 0U;
    g_button_pin_state = GPIO_PIN_SET;
    g_led_pin_state = GPIO_PIN_RESET;
    g_last_gpio_write_pin = 0U;
    g_radio_init_calls = 0U;
    AppSlave_ResetButtonStateForTest();
```

Append these tests before `main()`:

```c
static void TestButtonPressDebouncesAndSendsAgentTrigger(void)
{
    ResetFakes();
    g_send_frame_result = true;
    g_button_pin_state = GPIO_PIN_RESET;

    AppSlave_Poll();
    assert(g_sent_frame.msg_type == 0U);

    g_tick = 31U;
    AppSlave_Poll();

    assert(g_sent_frame.msg_type == RF_MSG_AGENT_TRIGGER);
    assert(g_sent_frame.dst_addr == OPENPERIPH_MASTER_ADDR);
    assert(g_sent_frame.src_addr == OPENPERIPH_NODE_ADDR);
    assert(g_sent_frame.payload_len == 4U);
    assert(g_sent_frame.payload[0] == 1U);
    assert(g_sent_frame.payload[1] == 1U);
    assert(g_led_pin_state == GPIO_PIN_SET);
    assert(g_last_gpio_write_pin == GPIO_PIN_14);
}

static void TestButtonHeldLowOnlyTriggersOnceUntilReleased(void)
{
    ResetFakes();
    g_send_frame_result = true;
    g_button_pin_state = GPIO_PIN_RESET;
    g_tick = 31U;
    AppSlave_Poll();

    memset(&g_sent_frame, 0, sizeof(g_sent_frame));
    g_tick = 80U;
    AppSlave_Poll();
    assert(g_sent_frame.msg_type == 0U);

    g_button_pin_state = GPIO_PIN_SET;
    AppSlave_Poll();
    g_button_pin_state = GPIO_PIN_RESET;
    g_tick = 120U;
    AppSlave_Poll();
    g_tick = 151U;
    AppSlave_Poll();

    assert(g_sent_frame.msg_type == RF_MSG_AGENT_TRIGGER);
    assert(g_sent_frame.payload[0] == 2U);
}

static void TestButtonLedPulseTurnsOffAfterDeadline(void)
{
    ResetFakes();
    g_send_frame_result = true;
    g_button_pin_state = GPIO_PIN_RESET;
    g_tick = 31U;
    AppSlave_Poll();
    assert(g_led_pin_state == GPIO_PIN_SET);

    g_tick = 130U;
    AppSlave_Poll();
    assert(g_led_pin_state == GPIO_PIN_SET);

    g_tick = 132U;
    AppSlave_Poll();
    assert(g_led_pin_state == GPIO_PIN_RESET);
}
```

Add these calls at the start of `main()`:

```c
    TestButtonPressDebouncesAndSendsAgentTrigger();
    TestButtonHeldLowOnlyTriggersOnceUntilReleased();
    TestButtonLedPulseTurnsOffAfterDeadline();
```

- [ ] **Step 2: Run the slave test and verify it fails**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave
```

Expected: FAIL at compile time because `AppSlave_ResetButtonStateForTest` is undefined and `RF_MSG_AGENT_TRIGGER` behavior is not implemented yet.

- [ ] **Step 3: Implement button state and trigger send**

In `Core/Inc/app_slave.h`, add these constants and state after `APP_SLAVE_RADIO_WATCHDOG_MS`:

```c
#define APP_SLAVE_BUTTON_DEBOUNCE_MS 30U
#define APP_SLAVE_BUTTON_LED_PULSE_MS 100U
#define APP_SLAVE_BUTTON_PRESS_SINGLE 1U

typedef struct {
    GPIO_PinState last_sample;
    bool waiting_for_debounce;
    bool armed;
    uint32_t debounce_start_tick;
    uint32_t led_until_tick;
    uint8_t next_event_id;
} AppSlaveButtonState_t;

static AppSlaveButtonState_t g_app_slave_button_state = {
    .last_sample = GPIO_PIN_SET,
    .waiting_for_debounce = false,
    .armed = true,
    .debounce_start_tick = 0U,
    .led_until_tick = 0U,
    .next_event_id = 1U,
};
```

Add this helper after `AppSlave_ClearDrawState()`:

```c
static inline void AppSlave_ResetButtonStateForTest(void)
{
    g_app_slave_button_state.last_sample = GPIO_PIN_SET;
    g_app_slave_button_state.waiting_for_debounce = false;
    g_app_slave_button_state.armed = true;
    g_app_slave_button_state.debounce_start_tick = 0U;
    g_app_slave_button_state.led_until_tick = 0U;
    g_app_slave_button_state.next_event_id = 1U;
}
```

Add these helpers before `AppSlave_Init()`:

```c
static inline void AppSlave_SendAgentTrigger(uint8_t event_id)
{
    RfFrame_t trigger = {0};
    uint32_t tick = HAL_GetTick();

    trigger.version = RF_FRAME_VERSION;
    trigger.msg_type = RF_MSG_AGENT_TRIGGER;
    trigger.dst_addr = OPENPERIPH_MASTER_ADDR;
    trigger.src_addr = OPENPERIPH_NODE_ADDR;
    trigger.seq = event_id;
    trigger.payload_len = 4U;
    trigger.payload[0] = event_id;
    trigger.payload[1] = APP_SLAVE_BUTTON_PRESS_SINGLE;
    trigger.payload[2] = (uint8_t)(tick & 0xFFU);
    trigger.payload[3] = (uint8_t)((tick >> 8) & 0xFFU);

    (void)RfLink_SendFrame(&trigger);
}

static inline void AppSlave_AcceptButtonPress(void)
{
    uint8_t event_id = g_app_slave_button_state.next_event_id;

    g_app_slave_button_state.next_event_id = (uint8_t)(event_id + 1U);
    if (g_app_slave_button_state.next_event_id == 0U) {
        g_app_slave_button_state.next_event_id = 1U;
    }
    g_app_slave_button_state.armed = false;
    g_app_slave_button_state.waiting_for_debounce = false;
    g_app_slave_button_state.led_until_tick = HAL_GetTick() + APP_SLAVE_BUTTON_LED_PULSE_MS;
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_SET);
    AppSlave_SendAgentTrigger(event_id);
}

static inline void AppSlave_PollButton(void)
{
    GPIO_PinState sample = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_11);
    uint32_t now = HAL_GetTick();

    if ((g_app_slave_button_state.led_until_tick != 0U) &&
        ((int32_t)(now - g_app_slave_button_state.led_until_tick) >= 0)) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_RESET);
        g_app_slave_button_state.led_until_tick = 0U;
    }

    if (sample == GPIO_PIN_SET) {
        g_app_slave_button_state.armed = true;
        g_app_slave_button_state.waiting_for_debounce = false;
        g_app_slave_button_state.last_sample = sample;
        return;
    }

    if ((sample == GPIO_PIN_RESET) && g_app_slave_button_state.armed) {
        if (!g_app_slave_button_state.waiting_for_debounce ||
            (g_app_slave_button_state.last_sample != sample)) {
            g_app_slave_button_state.waiting_for_debounce = true;
            g_app_slave_button_state.debounce_start_tick = now;
            g_app_slave_button_state.last_sample = sample;
            return;
        }

        if ((now - g_app_slave_button_state.debounce_start_tick) >= APP_SLAVE_BUTTON_DEBOUNCE_MS) {
            AppSlave_AcceptButtonPress();
        }
    }
}
```

At the start of `AppSlave_Poll()`, before RF receive, add:

```c
    AppSlave_PollButton();
```

- [ ] **Step 4: Run the slave test and verify it passes**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave
```

Expected: PASS with exit code 0.

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/app_slave.h tests/host/test_app_slave.c
git commit -m "feat: send lucky button rf trigger"
```

---

### Task 3: Master RF Event Bridge To USB

**Files:**
- Modify: `Core/Inc/app_master.h`
- Modify: `Core/Src/main.c`
- Modify: `tests/host/test_app_master_commands.c`

- [ ] **Step 1: Add master bridge tests**

In `tests/host/test_app_master_commands.c`, add these globals near the existing USB capture globals:

```c
static PacketType_t g_last_usb_packet_type;
static uint8_t g_last_usb_packet_payload[16];
static uint16_t g_last_usb_packet_len;
```

Replace the existing `OpenPeriph_SendUsbPacket` fake with:

```c
void OpenPeriph_SendUsbPacket(PacketType_t type, const uint8_t *payload, uint16_t len)
{
    g_last_usb_packet_type = type;
    g_last_usb_packet_len = len;
    memset(g_last_usb_packet_payload, 0, sizeof(g_last_usb_packet_payload));
    if ((payload != NULL) && (len <= sizeof(g_last_usb_packet_payload))) {
        memcpy(g_last_usb_packet_payload, payload, len);
    }
}
```

In `ResetCaptures()`, add:

```c
    g_last_usb_packet_type = 0U;
    memset(g_last_usb_packet_payload, 0, sizeof(g_last_usb_packet_payload));
    g_last_usb_packet_len = 0U;
```

Add this helper after `ScriptDrawResponse()`:

```c
static void ScriptAgentTrigger(uint8_t src_addr,
                               uint8_t seq,
                               const uint8_t *payload,
                               uint8_t payload_len)
{
    ScriptDrawResponse(RF_MSG_AGENT_TRIGGER, src_addr, seq, payload, payload_len);
}
```

Append these tests before `main()`:

```c
static void TestMasterPollBridgesAgentTriggerToUsb(void)
{
    const uint8_t payload[4] = { 0x7AU, 0x01U, 0x34U, 0x12U };

    ResetCaptures();
    ScriptAgentTrigger(0x22U, 0x7AU, payload, sizeof(payload));

    AppMaster_PollRfEvents();

    assert(g_last_usb_packet_type == PKT_TYPE_AGENT_EVENT);
    assert(g_last_usb_packet_len == 5U);
    assert(g_last_usb_packet_payload[0] == 0x22U);
    assert(g_last_usb_packet_payload[1] == 0x7AU);
    assert(g_last_usb_packet_payload[2] == 0x01U);
    assert(g_last_usb_packet_payload[3] == 0x34U);
    assert(g_last_usb_packet_payload[4] == 0x12U);
}

static void TestMasterPollIgnoresMalformedAgentTrigger(void)
{
    const uint8_t payload[3] = { 0x7AU, 0x01U, 0x34U };

    ResetCaptures();
    ScriptAgentTrigger(0x22U, 0x7AU, payload, sizeof(payload));

    AppMaster_PollRfEvents();

    assert(g_last_usb_packet_len == 0U);
}
```

Add these calls in `main()`:

```c
    TestMasterPollBridgesAgentTriggerToUsb();
    TestMasterPollIgnoresMalformedAgentTrigger();
```

- [ ] **Step 2: Run the master test and verify it fails**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
```

Expected: FAIL at compile time because `AppMaster_PollRfEvents` is undefined.

- [ ] **Step 3: Implement master RF event polling**

In `Core/Inc/app_master.h`, add this helper before `AppMaster_HandleUsbPacket()`:

```c
static inline void AppMaster_HandleAgentTriggerFrame(const RfFrame_t *frame)
{
    uint8_t payload[5];

    if ((frame == NULL) || (frame->payload_len != 4U)) {
        return;
    }
    if ((frame->version != RF_FRAME_VERSION) ||
        (frame->msg_type != RF_MSG_AGENT_TRIGGER) ||
        (frame->dst_addr != OPENPERIPH_NODE_ADDR)) {
        return;
    }

    payload[0] = frame->src_addr;
    payload[1] = frame->payload[0];
    payload[2] = frame->payload[1];
    payload[3] = frame->payload[2];
    payload[4] = frame->payload[3];
    OpenPeriph_SendUsbPacket(PKT_TYPE_AGENT_EVENT, payload, sizeof(payload));
}

static inline void AppMaster_PollRfEvents(void)
{
    RfFrame_t frame;

    if (!RfLink_TryReceiveFrame(&frame)) {
        return;
    }
    if (frame.msg_type == RF_MSG_AGENT_TRIGGER) {
        AppMaster_HandleAgentTriggerFrame(&frame);
    }
}
```

In `Core/Src/main.c`, add this call near the end of the main loop, before the slave-only poll block:

```c
#if OPENPERIPH_ROLE == OPENPERIPH_ROLE_MASTER
    AppMaster_PollRfEvents();
#endif
```

- [ ] **Step 4: Run the master test and verify it passes**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
```

Expected: PASS with exit code 0.

- [ ] **Step 5: Commit**

```bash
git add Core/Inc/app_master.h Core/Src/main.c tests/host/test_app_master_commands.c
git commit -m "feat: bridge lucky button events to usb"
```

---

### Task 4: Host Lucky Button Watcher Core

**Files:**
- Create: `scripts/lucky_button.py`
- Create: `tests/host/test_lucky_button.py`

- [ ] **Step 1: Add the watcher unit tests**

Create `tests/host/test_lucky_button.py`:

```python
#!/usr/bin/env python3

import os
import sys
import unittest
from unittest.mock import MagicMock, patch

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(REPO_ROOT, 'scripts'))

import lucky_button
import send_data


class LuckyButtonTests(unittest.TestCase):
    def test_sanitize_lines_bounds_ascii_width_and_count(self):
        raw = ["hello", "snowman \u2603", "x" * 80]

        lines = lucky_button.sanitize_lines(raw, max_lines=2, max_width=10)

        self.assertEqual(lines, ["hello", "snowman ?", "xxxxxxxxxx"])

    def test_generate_mode_lines_supports_approved_modes_without_agent(self):
        event = {
            'slave_addr': 0x22,
            'event_id': 7,
            'press_type': 1,
            'uptime_ms': 1234,
        }

        for mode in lucky_button.APPROVED_MODES:
            with self.subTest(mode=mode):
                lines = lucky_button.generate_mode_lines(mode, event, agent='none')
                self.assertGreaterEqual(len(lines), 2)
                self.assertTrue(all(isinstance(line, str) for line in lines))
                self.assertTrue(all(len(line) <= lucky_button.MAX_LINE_WIDTH for line in lines))

    def test_random_mode_chooses_approved_mode(self):
        event = {
            'slave_addr': 0x22,
            'event_id': 1,
            'press_type': 1,
            'uptime_ms': 1,
        }

        with patch('lucky_button.random.choice', return_value='fortune'):
            selected, lines = lucky_button.resolve_mode('random', event, agent='none')

        self.assertEqual(selected, 'fortune')
        self.assertGreaterEqual(len(lines), 2)

    def test_handle_packet_draws_agent_event(self):
        payload = bytes([0x22, 0x09, 0x01, 0x34, 0x12])
        packet = {
            'valid': True,
            'type': send_data.PKT_TYPE_AGENT_EVENT,
            'payload': payload,
        }
        canvas = MagicMock()

        handled = lucky_button.handle_packet(packet, canvas, mode='fortune', agent='none')

        self.assertTrue(handled)
        canvas.clear.assert_called_once()
        self.assertGreaterEqual(canvas.draw_multiline.call_count, 1)
        canvas.flush.assert_called_once_with(full_refresh=True)

    def test_handle_packet_ignores_non_agent_event(self):
        packet = {
            'valid': True,
            'type': send_data.PKT_TYPE_ACK,
            'payload': b'',
        }
        canvas = MagicMock()

        handled = lucky_button.handle_packet(packet, canvas, mode='fortune', agent='none')

        self.assertFalse(handled)
        canvas.clear.assert_not_called()


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the watcher tests and verify they fail**

Run:

```bash
PYTHONPATH=. python3 -m unittest tests.host.test_lucky_button -v
```

Expected: FAIL with `ModuleNotFoundError: No module named 'lucky_button'`.

- [ ] **Step 3: Implement `scripts/lucky_button.py`**

Create `scripts/lucky_button.py`:

```python
#!/usr/bin/env python3

import argparse
import datetime as _dt
import os
import random
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))

import send_data
from eink_canvas import EinkCanvas


APPROVED_MODES = ('weather', 'art', 'news', 'repo', 'fortune')
MAX_LINE_WIDTH = 58
MAX_LINES = 12


def sanitize_lines(lines, max_lines=MAX_LINES, max_width=MAX_LINE_WIDTH):
    cleaned = []
    for line in lines:
        text = ''.join(ch if 32 <= ord(ch) <= 126 else '?' for ch in str(line))
        while len(text) > max_width:
            cleaned.append(text[:max_width])
            text = text[max_width:]
            if len(cleaned) >= max_lines:
                return cleaned
        cleaned.append(text)
        if len(cleaned) >= max_lines:
            return cleaned
    return cleaned or ["Lucky button", "No content generated"]


def _repo_summary():
    try:
        status = subprocess.run(
            ['git', 'status', '--short'],
            cwd=os.path.abspath(os.path.join(os.path.dirname(__file__), '..')),
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ["Repo pulse", "git status unavailable"]

    lines = [line for line in status.stdout.splitlines() if line.strip()]
    if not lines:
        return ["Repo pulse", "Working tree is clean", "Button found calm seas"]
    return ["Repo pulse", f"{len(lines)} changed paths"] + lines[:8]


def generate_mode_lines(mode, event, agent='none'):
    now = _dt.datetime.now().strftime('%Y-%m-%d %H:%M')
    if mode == 'weather':
        return sanitize_lines([
            "Weather card",
            now,
            "Network-free demo mode",
            "Look outside, then press again",
            f"event #{event['event_id']} from 0x{event['slave_addr']:02X}",
        ])
    if mode == 'art':
        return sanitize_lines([
            "Button sketch",
            "   +------+",
            "  / luck /|",
            " +------+ |",
            " |  AI  | +",
            " | draws|/",
            " +------+",
        ])
    if mode == 'news':
        return sanitize_lines([
            "Tiny headline desk",
            now,
            "No network fetch in --agent none",
            "Today's scoop: hardware talks back",
            f"uptime low16: {event['uptime_ms']} ms",
        ])
    if mode == 'repo':
        return sanitize_lines(_repo_summary())
    if mode == 'fortune':
        fortunes = [
            "A small input becomes a large adventure.",
            "The next packet carries unreasonable optimism.",
            "A clean debounce is a clean conscience.",
            "Today favors patient radios.",
        ]
        pick = fortunes[(event['event_id'] + event['uptime_ms']) % len(fortunes)]
        return sanitize_lines([
            "Fortune",
            now,
            pick,
            f"event #{event['event_id']}",
        ])
    raise ValueError(f"unsupported mode: {mode}")


def resolve_mode(mode, event, agent='none'):
    selected = random.choice(APPROVED_MODES) if mode == 'random' else mode
    if selected not in APPROVED_MODES:
        raise ValueError(f"unsupported mode: {selected}")
    return selected, generate_mode_lines(selected, event, agent=agent)


def draw_lines(canvas, lines):
    canvas.clear()
    canvas.draw_multiline(lines, x_start=5, y_start=5, font=16, clear_first_line=False)
    canvas.flush(full_refresh=True)


def handle_packet(packet, canvas, mode='random', agent='none'):
    if not packet.get('valid') or packet.get('type') != send_data.PKT_TYPE_AGENT_EVENT:
        return False

    event = send_data.parse_agent_event_payload(packet.get('payload', b''))
    selected, lines = resolve_mode(mode, event, agent=agent)
    print(f"event {event['event_id']} from 0x{event['slave_addr']:02X}: {selected}")
    draw_lines(canvas, lines)
    return True


def run(port, dst, baud=115200, mode='random', agent='none'):
    try:
        import serial
    except ModuleNotFoundError:
        print("pyserial is required. Install with: pip install pyserial")
        return 1

    with serial.Serial(port, baud, timeout=1) as ser:
        time.sleep(0.5)
        send_data.read_response(ser, timeout=0.2)
        with EinkCanvas(port=port, dst=dst, baud=baud) as canvas:
            while True:
                packet = send_data.read_response(ser, timeout=1.0)
                try:
                    handle_packet(packet, canvas, mode=mode, agent=agent)
                except ValueError as exc:
                    print(f"ignored malformed agent event: {exc}")


def main():
    parser = argparse.ArgumentParser(description="Watch master USB for lucky-button events")
    parser.add_argument('--port', required=True)
    parser.add_argument('--dst', type=lambda x: int(x, 0), default=0x22)
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--mode', choices=('random',) + APPROVED_MODES, default='random')
    parser.add_argument('--agent', choices=('none', 'codex', 'claude'), default='none')
    args = parser.parse_args()

    return run(args.port, args.dst, baud=args.baud, mode=args.mode, agent=args.agent)


if __name__ == '__main__':
    raise SystemExit(main())
```

- [ ] **Step 4: Run the watcher tests and verify they pass**

Run:

```bash
PYTHONPATH=. python3 -m unittest tests.host.test_lucky_button -v
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add scripts/lucky_button.py tests/host/test_lucky_button.py
git commit -m "feat: add lucky button host watcher"
```

---

### Task 5: Fix Watcher Serial Ownership

**Files:**
- Modify: `scripts/lucky_button.py`
- Modify: `tests/host/test_lucky_button.py`

Reason: `EinkCanvas` currently opens the serial port itself. The watcher cannot also keep a separate `serial.Serial` instance open on the same port. This task refactors the watcher to support one serial owner by reading from `canvas._ser`.

- [ ] **Step 1: Add a run-loop test that proves one canvas owns serial**

Append this test to `tests/host/test_lucky_button.py`:

```python
    def test_run_once_reads_from_canvas_serial(self):
        packet = {
            'valid': True,
            'type': send_data.PKT_TYPE_AGENT_EVENT,
            'payload': bytes([0x22, 0x01, 0x01, 0x00, 0x00]),
        }
        canvas = MagicMock()
        canvas._ser = object()

        with patch('lucky_button.EinkCanvas') as canvas_cls:
            canvas_cls.return_value.__enter__.return_value = canvas
            with patch('lucky_button.send_data.read_response', return_value=packet) as read_response:
                lucky_button.run_once('/dev/fake', 0x22, mode='fortune', agent='none')

        canvas_cls.assert_called_once_with(port='/dev/fake', dst=0x22, baud=115200)
        read_response.assert_called_once_with(canvas._ser, timeout=1.0)
        canvas.flush.assert_called_once_with(full_refresh=True)
```

- [ ] **Step 2: Run the specific test and verify it fails**

Run:

```bash
PYTHONPATH=. python3 -m unittest tests.host.test_lucky_button.LuckyButtonTests.test_run_once_reads_from_canvas_serial -v
```

Expected: FAIL with `AttributeError` because `run_once` does not exist.

- [ ] **Step 3: Refactor watcher run functions**

In `scripts/lucky_button.py`, replace `run()` with these functions:

```python
def run_once(port, dst, baud=115200, mode='random', agent='none'):
    with EinkCanvas(port=port, dst=dst, baud=baud) as canvas:
        packet = send_data.read_response(canvas._ser, timeout=1.0)
        handle_packet(packet, canvas, mode=mode, agent=agent)


def run(port, dst, baud=115200, mode='random', agent='none'):
    try:
        import serial  # noqa: F401
    except ModuleNotFoundError:
        print("pyserial is required. Install with: pip install pyserial")
        return 1

    with EinkCanvas(port=port, dst=dst, baud=baud) as canvas:
        send_data.read_response(canvas._ser, timeout=0.2)
        while True:
            packet = send_data.read_response(canvas._ser, timeout=1.0)
            try:
                handle_packet(packet, canvas, mode=mode, agent=agent)
            except ValueError as exc:
                print(f"ignored malformed agent event: {exc}")
```

- [ ] **Step 4: Run watcher tests**

Run:

```bash
PYTHONPATH=. python3 -m unittest tests.host.test_lucky_button -v
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add scripts/lucky_button.py tests/host/test_lucky_button.py
git commit -m "fix: keep lucky button watcher on one serial port"
```

---

### Task 6: Full Verification And Firmware Build

**Files:**
- No source edits expected.

- [ ] **Step 1: Run C host tests**

Run:

```bash
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_slave.c -o /tmp/test_app_slave && /tmp/test_app_slave
cc -std=c11 -Wall -Wextra -I Core/Inc tests/host/test_app_master_commands.c -o /tmp/test_app_master_commands && /tmp/test_app_master_commands
```

Expected: both commands exit 0.

- [ ] **Step 2: Run Python host tests**

Run:

```bash
PYTHONPATH=. python3 -m unittest tests.host.test_send_data tests.host.test_lucky_button tests.host.test_eink_canvas -v
```

Expected: `OK`.

- [ ] **Step 3: Build firmware**

Run with the CubeIDE STM32 toolchain on `PATH`:

```bash
GNU_TOOLS="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.100.202509120712/tools"
PATH="$GNU_TOOLS/bin:$PATH" make -C Debug -j4
```

Expected: firmware build completes and produces `Debug/openPeriph_monorepo.elf`.

- [ ] **Step 4: Check worktree scope**

Run:

```bash
git status --short
```

Expected: only intentional lucky-button files are modified or already committed. Pre-existing unrelated worktree changes may still appear; do not revert them.

- [ ] **Step 5: Final implementation commit if needed**

If Task 6 produced any source or test changes, commit them:

```bash
git add Core/Inc/openperiph_config.h Core/Inc/rf_frame.h Core/Inc/usb_protocol.h Core/Inc/app_slave.h Core/Inc/app_master.h Core/Src/main.c scripts/send_data.py scripts/lucky_button.py tests/host/test_send_data.py tests/host/test_app_slave.c tests/host/test_app_master_commands.c tests/host/test_lucky_button.py
git commit -m "test: verify lucky button agent flow"
```

Expected: either a commit is created for intentional verification changes, or Git reports nothing to commit.

---

## Self-Review

- Spec coverage:
  - Slave active-low button: Task 2.
  - LED feedback on `PC14`: Task 2.
  - `RF_MSG_AGENT_TRIGGER`: Task 1 and Task 2.
  - `PKT_TYPE_AGENT_EVENT`: Task 1 and Task 3.
  - Master idle bridge: Task 3.
  - Plain Python watcher: Task 4 and Task 5.
  - Approved modes: Task 4.
  - `--agent none` default: Task 4.
  - Single serial owner: Task 5.
  - Verification: Task 6.
- Placeholder scan:
  - No unresolved marker text or unspecified implementation steps.
- Type consistency:
  - RF trigger payload is 4 bytes: `event_id`, `press_type`, `uptime_lo`, `uptime_hi`.
  - USB agent event payload is 5 bytes: `slave_addr` plus the RF payload.
  - Python parser returns `slave_addr`, `event_id`, `press_type`, and `uptime_ms`.
