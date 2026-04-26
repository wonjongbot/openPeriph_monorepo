# Lucky Button Agent Design

## Summary

Add a slave-side "I'm feeling lucky" button path using the new active-low
`GPIO_PIN_11` input. A valid button press on the slave sends an RF event to the
master. The master forwards that event over USB to a long-running Python script.
The script chooses from a small set of approved modes, optionally calls an AI
agent, and draws the result back to the slave e-ink display through the existing
staged draw API.

The first implementation is intentionally small:

1. single press only
2. visible LED feedback on the slave for each accepted press
3. one RF event type from slave to master
4. one USB event type from master to host
5. one plain Python watcher script, not a system service
6. approved modes: `weather`, `art`, `news`, `repo`, `fortune`

## Goals

- Prove end-to-end slave-originated input:
  `button -> slave RF -> master USB -> host script -> slave display`.
- Make the button visibly testable without a serial console by pulsing or
  toggling an available LED on each debounced press.
- Keep firmware responsibilities simple and deterministic.
- Keep AI behavior host-side, replaceable, and bounded.
- Reuse the existing reliable draw path exposed by `scripts/eink_canvas.py`.
- Allow hardware testing without requiring an AI account or network access.

## Non-Goals

- Running a `systemd`, launchd, or background OS-managed daemon.
- Letting the STM32 call an AI service directly.
- Letting an AI agent write arbitrary serial bytes to the display.
- Supporting double press, long press, or mode cycling in the first version.
- Building a general bidirectional RF transport layer.
- Supporting simultaneous use of `send_data.py` while the watcher owns the
  serial port.

## Current Context

The repo already has most of the required infrastructure:

- `Core/Src/main.c` configures `PC11 / GPIO_PIN_11` as input with pull-up.
- `Core/Inc/app_slave.h` owns slave polling and handles RF frames addressed to
  the local node.
- `Core/Inc/app_master.h` owns master handling for USB commands and reliable RF
  draw exchanges.
- `Core/Inc/rf_frame.h` defines the RF message namespace.
- `Core/Inc/usb_protocol.h` defines the USB packet namespace.
- `scripts/send_data.py` implements the host USB packet framing.
- `scripts/eink_canvas.py` provides a high-level staged draw API for the e-ink
  display.

The missing pieces are:

- debounced button handling in the slave application
- an RF event frame for slave-originated button triggers
- master idle RF polling for unsolicited slave events
- a USB event packet that the host can observe
- a host watcher script that reacts to those events

## Design Options

### Option 1: Event Bridge

The slave sends a semantic RF event to the master. The master forwards it over
USB. A Python script watches USB and performs the selected lucky-button mode.

Pros:

- proves the real hardware button path
- keeps AI and internet access on the host
- works with the existing display drawing stack
- easy to test in pieces
- modes can evolve without reflashing firmware

Cons:

- requires the master to listen for unsolicited RF frames while idle
- the watcher script must own the serial port while running

### Option 2: Host Polling

The Python script periodically asks the master if the slave reported a button
press.

Pros:

- USB direction stays request/response shaped

Cons:

- worse latency
- master needs event caching state
- more awkward to reason about missed or repeated presses

### Option 3: Host-Only Prototype

Use a keyboard press or command-line trigger on the host and skip the slave
button in the first pass.

Pros:

- fastest way to iterate on creative modes

Cons:

- does not validate the new GPIO or slave-to-master RF event path

## Recommendation

Implement Option 1.

The event bridge gives the project the intended physical interaction while
keeping the risky and experimental behavior on the host. Firmware reports that a
button was pressed; Python decides what to do; the existing draw API sends the
result back to the display.

## Slave Behavior

### Button Input

Use `PC11 / GPIO_PIN_11` as an active-low input. A press is recognized when the
pin transitions from high to low and remains low through a debounce window.

Initial debounce policy:

- sample from `AppSlave_Poll()`
- debounce window: 30 ms
- trigger once per press
- re-arm only after the input returns high

This avoids adding EXTI interrupt complexity for the first version and keeps the
button logic in the same cooperative polling model as RF receive.

### LED Feedback

On every accepted debounced press, the slave pulses or toggles one available LED
so the button can be validated without RF or USB logs.

The preferred LED choice is an already-known visible LED from the current board
setup. `main.c` currently blinks `PC14` during startup, so `PC14` is the first
candidate if it is visibly connected on the slave hardware. If hardware testing
shows `PC14` is not visible or is already reserved, choose one of the configured
output pins that is visibly connected and not required by the display or radio.

The LED action should be short and non-blocking:

- record a `button_led_until_tick`
- set LED on when the press is accepted
- turn LED off from polling after about 100 ms

Avoid a blocking `HAL_Delay()` in the press path so RF polling remains
responsive.

### RF Trigger Frame

Add a new RF message:

```c
RF_MSG_AGENT_TRIGGER = 0x0B
```

Payload:

```text
event_id      uint8
press_type    uint8  // 1 = single press
uptime_lo     uint8
uptime_hi     uint8
```

For v1, `press_type` is always `1`. `event_id` increments on each accepted
press and wraps at 255. The uptime field is the low 16 bits of `HAL_GetTick()`
and gives the host a little event context and timing entropy.

Destination is the configured master address. Add an explicit
`OPENPERIPH_MASTER_ADDR` config constant with default `0x01U`, matching the
documented master quickstart address. Source is the slave node address. The RF
frame uses the same frame version and addressing rules as existing RF messages.

## Master Behavior

The master needs a small idle RF event pump in addition to existing USB command
handling. When it is not in the middle of a blocking draw exchange, it should
try to receive RF frames and handle `RF_MSG_AGENT_TRIGGER`.

For a valid trigger frame addressed to the master, emit a USB packet:

```c
PKT_TYPE_AGENT_EVENT = 0x85
```

USB payload:

```text
slave_addr    uint8
event_id      uint8
press_type    uint8
uptime_lo     uint8
uptime_hi     uint8
```

The master does not call an agent and does not choose a mode. It only bridges
the hardware event to the host.

Unrecognized RF frames should continue to be ignored unless they are part of an
active request/response exchange. This keeps the master tolerant of stale draw
ACKs or unrelated RF traffic.

## Host Watcher Script

Add a plain long-running script:

```bash
python scripts/lucky_button.py --port /dev/tty... --dst 0x22 --mode random
```

The script owns the serial port while running. Users should not run
`send_data.py` against the same master device at the same time.

Responsibilities:

- open the master USB CDC serial port
- parse packets using the same framing as `send_data.py`
- wait for `PKT_TYPE_AGENT_EVENT`
- choose a mode from the approved set
- generate bounded display content
- render it to the slave via `EinkCanvas`
- print concise logs for each event and mode result

Modes:

- `weather`: fetch or synthesize a compact local weather card
- `art`: generate ASCII art or a small text poster
- `news`: fetch one or a few current headlines and summarize them
- `repo`: inspect the local repo state and produce a playful status card
- `fortune`: generate a short fortune using time and button event data

Mode selection:

- `--mode random` chooses one approved mode per press
- `--mode weather|art|news|repo|fortune` pins the script to one mode for demos

The first implementation should support `--agent none` so hardware can be
tested without network or AI credentials. AI-backed modes can be added behind
explicit flags such as `--agent codex` or `--agent claude`.

## AI Boundary

The AI agent must receive and return a constrained text contract rather than
direct access to the serial port.

Suggested contract:

```text
Return 5 to 12 ASCII lines.
Each line must be at most 58 characters.
Do not include terminal control codes.
Do not include Markdown fences.
```

The watcher validates or wraps the result before drawing. If the agent fails,
times out, or returns unusable content, the script falls back to a deterministic
local message for that mode.

This gives the agent room to be creative while keeping the hardware protocol
stable.

## Error Handling

- Button bounce does not create multiple events because the slave triggers once
  per debounced low interval.
- If RF send fails on the slave, the LED still confirms local button detection.
- If the master receives a malformed trigger payload, it ignores the frame.
- If the watcher is not running, the master may still emit the USB event, but no
  host action occurs.
- If display rendering fails, the watcher logs the failure and waits for the
  next button event.
- If a weather or news fetch fails, the watcher falls back to local fortune or
  art output.

## Testing

Host tests should cover:

- RF frame encode/decode accepts the new message type.
- Slave button debounce emits exactly one trigger per press.
- Slave LED pulse state turns on for an accepted press and turns off after the
  configured duration.
- Master bridges a valid `RF_MSG_AGENT_TRIGGER` to `PKT_TYPE_AGENT_EVENT`.
- Master ignores malformed trigger payloads.
- `lucky_button.py` parses agent events and dispatches each approved mode.
- `lucky_button.py --agent none` can produce bounded ASCII lines without network
  access.

Hardware smoke test:

1. flash slave firmware
2. press the button and confirm the LED pulse
3. flash master firmware and run `scripts/lucky_button.py`
4. press the slave button
5. confirm the script logs one event
6. confirm the slave display updates with one approved mode result

## Implementation Defaults

- Use `PC14` as the v1 button feedback LED because startup blink already uses
  it and it is known to be configured as an output in `main.c`.
- Add `OPENPERIPH_MASTER_ADDR` with default `0x01U`. The slave sends
  `RF_MSG_AGENT_TRIGGER` to that address.
- Keep `--mode random --agent none` as the default demo behavior. Add AI-backed
  mode selection only after the hardware event path is proven.
