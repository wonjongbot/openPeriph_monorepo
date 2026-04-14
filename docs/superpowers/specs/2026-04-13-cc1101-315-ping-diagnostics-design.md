# CC1101 315 MHz Diagnostics And RF Ping Design

## Summary

This spec extends the existing OpenPeriph USB/RF bridge in three focused ways:

- retune the CC1101 radio profile from the current 433 MHz settings to 315 MHz
- extend `statusm` / `CMD_GET_STATUS` so it reports CC1101 identity registers (`PARTNUM` and `VERSION`) in addition to the current local status fields
- add a CLI-visible board-to-board RF ping/pong path so radio verification does not depend on trying an EPD command first

The implementation should stay inside the current architecture:

- USB host commands enter through `PKT_TYPE_COMMAND`
- the master firmware owns USB command handling
- RF inter-node traffic continues to use the existing `RfFrame_t` transport
- the slave continues to service RF traffic from its polling loop

## Goals

- Preserve the current host workflow based on `scripts/send_data.py`.
- Make `statusm` confirm that the CC1101 is responding over SPI, not just that the MCU is alive.
- Provide a single-terminal RF link check from the master host CLI.
- Ground the 315 MHz retune in the existing local RF reference plus TI primary documentation for frequency-dependent CC1101 fields.

## Non-Goals

- General RF discovery, enumeration, or network management.
- A generic asynchronous RF event bridge to the host.
- Runtime switching between 315 MHz and 433 MHz profiles.
- Reworking the proven packet format, CRC policy, or display command path.
- Supporting master-to-master ping in this phase.

## Current Context

Today the repo already has:

- a working local status command path in `AppCommands_HandleLocalCommand()`
- a CC1101 driver in `Core/Src/cc1101_radio.c`
- addressed RF frames in `rf_frame.*`
- master-side RF transmit for draw-text
- slave-side RF polling and draw-text execution
- a host script with `--ping`, `--status`, and draw-text helpers

The current status payload is 8 bytes:

- byte `0`: firmware major
- byte `1`: firmware minor
- byte `2`: `MARCSTATE`
- byte `3`: USB RX buffered bytes, low
- byte `4`: USB RX buffered bytes, high
- byte `5`: error count, low
- byte `6`: error count, high
- byte `7`: radio recovery status

The current RF profile is a 433 MHz GFSK profile copied from the local bring-up reference in `ref/openPeriph_rf/rf_demo/Core/Src/cc1101.c`.

## RF Reference Basis

The 315 MHz retune should not be done by ad hoc register guessing. The implementation should use these sources:

- Local behavioral reference:
  - `ref/openPeriph_rf/rf_demo/Core/Src/cc1101.c`
  - this captures the current working 433 MHz profile and the repo's proven packet-mode choices
- TI CC1101 datasheet:
  - https://www.ti.com/lit/ds/symlink/cc1101.pdf
  - use this for the carrier-frequency formula and for the note that `TEST0` is frequency-band dependent and should match SmartRF output for the chosen band
- TI DN013:
  - https://www.ti.com/lit/an/swra151a/swra151a.pdf
  - use this to validate that `PATABLE = 0xC0` remains a valid high-power setting at 315 MHz

### Frequency-Dependent Register Decisions

The current profile already has a working packet format, sync word, IF, bandwidth, modulation, and FIFO policy. For the first 315 MHz cut, preserve that proven structure and only change the fields that are band-dependent.

Required changes:

- `FREQ2/FREQ1/FREQ0`: retune to 315 MHz for a 26 MHz crystal
- `TEST0`: switch to the 300-348 MHz-band value instead of reusing the 433 MHz value
- `PATABLE`: keep the existing `0xC0` table because TI DN013 lists `0xC0` as the top standard 315 MHz power entry, around `+10.6 dBm`

Initial target values:

- `FREQ2 = 0x0C`
- `FREQ1 = 0x1D`
- `FREQ0 = 0x8A`
- `TEST0 = 0x0B`

Rationale:

- the CC1101 datasheet frequency formula with `fxosc = 26 MHz` gives a nearest 24-bit carrier word of `0x0C1D8A` for 315 MHz
- the datasheet explicitly says the correct `TEST0` value is frequency dependent and SmartRF-derived; for the lower 300-348 MHz band the implementation should move away from the current 433 MHz `0x09` value and use `0x0B`
- `TEST1 = 0x35` and `TEST2 = 0x81` remain unchanged unless later bench validation shows a band-specific issue

This is intentionally a minimal-band retune, not a full modem-profile redesign.

## Status Command Design

`CMD_GET_STATUS` remains a local command and still returns `PKT_TYPE_STATUS`.

The payload grows from 8 bytes to 10 bytes:

- byte `0`: firmware major
- byte `1`: firmware minor
- byte `2`: `MARCSTATE`
- byte `3`: USB RX buffered bytes, low
- byte `4`: USB RX buffered bytes, high
- byte `5`: error count, low
- byte `6`: error count, high
- byte `7`: radio recovery status
- byte `8`: CC1101 `PARTNUM`
- byte `9`: CC1101 `VERSION`

### CC1101 Identity Read Rules

- add a narrow driver API for chip identity reads instead of exposing a generic raw-register interface
- read `PARTNUM` and `VERSION` directly from the CC1101 when servicing `CMD_GET_STATUS`
- do not cache these values at boot; `statusm` should reflect a live SPI transaction
- if the identity read helper fails, fill both bytes with `0xFF` so the failure is visually obvious and does not alias valid CC1101 values

Expected healthy output for a normal CC1101 is typically:

- `PARTNUM = 0x00`
- `VERSION = 0x14`

The host script should print these values explicitly after the existing `MARCSTATE` line.

## RF Ping/Pong Design

### USB Command

Add a new command:

- `CMD_RF_PING = 0x07`

Payload shape:

- byte `0`: `CMD_RF_PING`
- byte `1`: destination node address

No extra payload fields are needed for the first cut. The existing USB packet ID should be reused as the RF sequence number.

### RF Message Types

Extend `RfMessageType_t` with:

- `RF_MSG_PING = 0x02`
- `RF_MSG_PONG = 0x03`

The ping and pong frames use only the RF frame header:

- `version = RF_FRAME_VERSION`
- `msg_type = RF_MSG_PING` or `RF_MSG_PONG`
- `dst_addr` and `src_addr` populated normally
- `seq = USB packet id`
- `payload_len = 0`

No payload is required because the target, source, and correlation ID already exist in the frame header.

### Master Flow

When the master receives `CMD_RF_PING`:

1. validate that the command payload is 2 bytes
2. reject `dst_addr == OPENPERIPH_NODE_ADDR` as an invalid self-ping
3. build and send an `RF_MSG_PING` frame to the requested destination
4. immediately switch back to RX and poll `RfLink_TryReceiveFrame()` in a bounded wait loop
5. accept success only when all of these match:
   - `msg_type == RF_MSG_PONG`
   - `src_addr == requested destination`
   - `dst_addr == OPENPERIPH_NODE_ADDR`
   - `seq == USB packet id`
6. return `PKT_TYPE_ACK` only after the matching pong is received
7. return `PKT_TYPE_NACK` on timeout or RF send failure

This command is intentionally synchronous. It avoids adding a background RF receive dispatcher on the master just to support this diagnostic.

### Slave Flow

The slave polling path should handle ping in addition to draw-text:

1. poll for an RF frame as it does today
2. ignore frames not addressed to the local node
3. if `msg_type == RF_MSG_PING`, immediately transmit `RF_MSG_PONG` back to the source with the same `seq`
4. if `msg_type == RF_MSG_DRAW_TEXT`, keep the current display behavior
5. ignore unknown message types

The pong response should not touch the display path and should not emit any host-visible output on the slave.

### Timeout And Retry Policy

Use a small bounded retry loop so the CLI test is robust without becoming slow:

- retries: `3`
- per-attempt pong wait: `75 ms`

If all retries fail, the master should send `PKT_TYPE_NACK`. The host tool can then print a clear timeout message such as `No RF pong from node 0x02`.

This mirrors the spirit of the existing RF bring-up reference, which already uses retry-based ping/pong on the bench.

## Host CLI Changes

Extend `scripts/send_data.py` with:

- a new argument: `--rf-ping <dst>`
- a new helper that sends `CMD_RF_PING`
- output that distinguishes:
  - immediate command failure
  - RF timeout / no pong
  - successful pong

Recommended output shape:

- `Sent RF_PING to 0x02`
- `PONG from 0x02 (host RTT 18 ms)`

The RTT can be measured on the host side by timing the interval between the USB command write and the final `ACK`.

The existing `--ping` command should remain unchanged and continue to mean `MCU alive over USB`, not `radio link alive`.

## Error Handling

- If RF init failed at boot, `CMD_RF_PING` should fail immediately with `NACK`.
- If `RfLink_SendFrame()` fails, `CMD_RF_PING` should fail immediately with `NACK`.
- If unrelated RF frames arrive while the master is waiting for pong, ignore them and keep waiting until timeout.
- If the radio falls into RX overflow or TX underflow during status reads or ping handling, preserve the existing recovery behavior before reporting status or retrying.
- The status command should never hard-fail solely because the CC1101 identity read fails; it should still return the rest of the local status report plus sentinel identity bytes.

## Testing

Add or extend host-side tests for:

- `AppCommands_HandleLocalCommand()` returning a 10-byte status payload with part/version appended
- status identity read failure mapping to `0xFF/0xFF`
- `AppMaster_HandleUsbPacket()` RF ping success path
- `AppMaster_HandleUsbPacket()` RF ping timeout path
- `AppSlave_Poll()` replying to `RF_MSG_PING` with `RF_MSG_PONG`
- `scripts/send_data.py` printing the extended status fields
- `scripts/send_data.py` printing RF ping success and failure messages

Hardware validation after implementation:

- run `statusm` and confirm `PARTNUM` and `VERSION` are reported
- run the new RF ping command from the master host to a slave node and confirm pong
- verify that the existing draw-text RF path still works after the 315 MHz retune

## Risks And Assumptions

- This spec assumes the new board already uses a 315 MHz-capable CC1101 module and matching antenna/front-end network.
- If the module exposes extra control pins such as `PA_EN`, `LNA_EN`, or `HGM`, this work does not add support for them.
- The current modem settings are preserved intentionally to minimize change. If 315 MHz bench results are weak, the next tuning pass should use TI SmartRF-generated modem values for the exact desired data rate and bandwidth rather than continuing to hand-edit registers.
