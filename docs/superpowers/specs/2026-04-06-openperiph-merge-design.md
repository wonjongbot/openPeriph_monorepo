# OpenPeriph Merge Design

## Summary

This spec defines phase 1 of the `openPeriph_monorepo` merge effort. The goal is to combine the existing USB CDC, RF, and e-ink proof-of-concept work into a single STM32CubeIDE project that can build both master and slave firmware variants from one codebase.

Phase 1 success criterion:

- One common STM32CubeIDE project builds both `MASTER` and `SLAVE` firmware.
- The host sends a USB CDC command to the master.
- The master converts that command into an addressed RF frame.
- The addressed slave receives the frame and renders plain text on the e-ink display.
- Non-target slaves ignore the packet.

This design treats the code in `ref/openPeriph_epd` and `ref/openPeriph_rf` as behavioral reference only. The merged implementation must live in the main repo and remain cleanly buildable within the existing STM32CubeIDE project layout.

## Goals

- Preserve the current STM32CubeIDE project structure and `.ioc` ownership of generated peripherals.
- Reimplement the needed RF and EPD functionality in the main repo.
- Use a single common board model where every board exposes USB, RF, and EPD-capable pins, even if role-specific firmware does not exercise every peripheral immediately.
- Centralize compile-time configuration in one header.
- Keep the code organized so multi-slave addressing, slave-to-master uplink events, and relay behavior can be added later without rewriting the foundations.

## Non-Goals for Phase 1

- Multi-hop relay behavior.
- Slave-to-master uplink events.
- A general-purpose RF network bus.
- Arbitrary graphics protocol beyond basic text rendering.
- Runtime role switching.
- Refactoring CubeMX-generated code outside stable user-code boundaries.

## Hardware Assumptions

- The target is a common PCB/board design for all nodes.
- `SPI1` is shared across RF and EPD devices.
- USB CDC is used by the master for host communication.
- RF and EPD are selected through distinct GPIO chip-select and sideband pins.
- The current monorepo `.ioc` already reserves the required GPIOs:
  - RF sideband and CS on `PC0`, `PC1`, `PC2`
  - EPD control pins on `PB0`, `PB1`, `PB2`, `PB10`
  - shared `SPI1` on `PA5`, `PA6`, `PA7`

## Architecture

The merged firmware will be a layered monolith:

1. `config`
Central compile-time role, node identity, defaults, and feature toggles.

2. `board`
Shared board pin/peripheral declarations and hardware glue for common peripherals.

3. `drivers`
Clean-room implementations of the RF and EPD drivers inside the main repo.

4. `services`
Reusable logic above raw drivers:
RF link service, display service, protocol encode/decode.

5. `app`
Thin role-specific orchestration for master and slave behavior.

`main.c` becomes a small bootstrap that initializes common services and dispatches into role-specific app logic.

## Project Structure

The STM32CubeIDE-generated project shape remains intact. New handwritten modules are added under `Core/Inc` and `Core/Src`.

### New headers

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

### New sources

- `Core/Src/openperiph_board.c`
- `Core/Src/app_protocol.c`
- `Core/Src/rf_frame.c`
- `Core/Src/rf_link.c`
- `Core/Src/display_service.c`
- `Core/Src/epd_port.c`
- `Core/Src/cc1101_radio.c`
- `Core/Src/app_master.c`
- `Core/Src/app_slave.c`

### Existing files that remain generated or lightly edited

- `openPeriph_monorepo.ioc`
- `USB_DEVICE/**`
- HAL/CMSIS sources
- `Core/Src/main.c` user sections only
- `Core/Inc/main.h` only if a small exported declaration is required

## Compile-Time Configuration

Configuration is centralized in `openperiph_config.h`.

Required values:

- `OPENPERIPH_ROLE`
- `OPENPERIPH_NODE_ADDR`
- RF channel default
- RF source address default
- feature toggles for display and USB host bridge
- debug/logging toggles

Recommended shape:

- `OPENPERIPH_ROLE_MASTER`
- `OPENPERIPH_ROLE_SLAVE`

This avoids scattering raw `#define ROLE_MASTER` style checks across unrelated modules. Role checks should stay mostly in app-layer code, with drivers and services remaining role-agnostic where possible.

## Shared SPI Strategy

Both RF and EPD use `SPI1`. To avoid conflicts:

- The project owns a single `SPI_HandleTypeDef hspi1`.
- Neither RF nor EPD code may reinitialize SPI dynamically.
- Device selection is handled only through their chip-select and sideband GPIOs.
- Services/drivers may assume SPI is already initialized by the CubeMX-generated setup path.

This is a hard requirement. The merge must not preserve the reference projects' pattern of behaving like isolated single-purpose applications that each own the peripheral stack.

## Protocol Design

Phase 1 uses two protocol layers:

### USB CDC framing

The existing USB protocol framing stays in place:

- sync bytes
- packet type
- sequence ID
- payload length
- payload
- CRC16
- end marker

The host script will be extended, not replaced.

### RF application frame

MCU-to-MCU RF traffic uses a distinct lightweight addressed frame format:

- `version`
- `msg_type`
- `dst_addr`
- `src_addr`
- `seq`
- `payload_len`
- `payload`

This frame is separate from USB framing. USB is the host transport; RF is the inter-node application transport.

## Phase 1 Application Command Set

Phase 1 RF messages are intentionally narrow:

- `RF_MSG_DRAW_TEXT`
- `RF_MSG_ACK`
- `RF_MSG_ERROR`

No relay, discovery, enumeration, or uplink event types are included in phase 1.

## DRAW_TEXT Payload

The first application payload supports flexible but bounded text placement:

- `x` as `uint16_t`
- `y` as `uint16_t`
- `font_id` as `uint8_t`
- `flags` as `uint8_t`
- `text_len` as `uint8_t`
- `text[text_len]`

Defined phase 1 flags:

- bit `0`: `clear_first`
- bit `1`: `full_refresh`

This keeps the protocol useful without forcing a full drawing language into the first merge. Future graphics commands can be added as separate message types.

## End-to-End Data Flow

1. PC sends a framed USB CDC packet to the master.
2. The master parses the USB payload into an internal application command.
3. The master builds an addressed RF frame using that command.
4. RF packets are broadcast over the air.
5. Each slave checks the RF destination address against `OPENPERIPH_NODE_ADDR`.
6. Non-target slaves discard the frame.
7. The target slave decodes the command payload and calls the display service.
8. The display service renders the text on the e-ink panel using the configured position, font, and flags.

## Display Service Policy

The display service owns the rendering policy for phase 1.

Responsibilities:

- initialize the e-ink panel
- map `font_id` to supported font tables
- apply `clear_first`
- decide whether to trigger a full refresh
- render plain text at `x/y`

Phase 1 keeps the display behavior conservative:

- support text rendering only
- prioritize deterministic updates over high performance
- use full refresh semantics when requested instead of trying to optimize partial updates prematurely

## RF Link Service Policy

The RF link service sits above the radio driver and below the app layer.

Responsibilities:

- initialize the radio with proven settings derived from reference behavior
- build/send RF frames
- poll for received frames
- validate destination addresses
- expose parsed messages upward to app logic

The phase 1 service does not attempt to implement a generic routed network. It is a single-hop addressed link.

## App Roles

### Master app

Responsibilities:

- read decoded USB command packets
- validate command parameters
- build addressed RF messages
- transmit RF messages
- return status, ACK, or error feedback over USB as needed

### Slave app

Responsibilities:

- initialize RF receive path
- initialize display service
- accept only frames addressed to the local node
- execute supported commands
- ignore unsupported or misaddressed packets safely

## Reference-Code Policy

The reference projects under `ref/` are not linked into the main firmware. They are used only to guide:

- pin expectations
- radio register settings
- known-good timing or initialization order
- e-ink driver behavior

The new implementation in the main repo should be organized, renamed, and simplified for the merged architecture rather than transplanted verbatim.

## Bring-Up Order

Implementation and verification should proceed bottom-up:

1. Add config, board, and module skeletons.
2. Reimplement RF driver and verify basic addressed RF TX/RX between two boards.
3. Reimplement EPD service and verify local text draw on one board.
4. Connect slave RF receive path to local `DRAW_TEXT`.
5. Connect master USB command path to RF send path.
6. Verify end-to-end host-to-display flow.

This order minimizes the number of moving parts involved in each failure.

## Verification Strategy

Phase 1 should verify these checkpoints explicitly:

### Build verification

- master build compiles from the same STM32CubeIDE project
- slave build compiles from the same STM32CubeIDE project
- command-line build is preferred where possible using generated build files, as long as that does not require project restructuring

### RF verification

- radio initializes correctly
- two boards exchange addressed packets reliably enough for bench testing
- non-target address filtering works

### Display verification

- panel initializes
- local text rendering works with fixed input
- `clear_first` and `full_refresh` flags behave predictably

### End-to-end verification

- host script sends `dst/x/y/font/flags/text`
- master emits RF frame
- addressed slave updates the e-ink display
- non-target nodes ignore the message

## Risks and Mitigations

### SPI ownership conflicts

Risk:
RF and EPD code may each assume they control SPI configuration and transaction boundaries.

Mitigation:
One shared project-owned SPI handle. No runtime SPI reinit in services or drivers.

### CubeMX regeneration damage

Risk:
Custom code may be overwritten if it is inserted into generated regions.

Mitigation:
Keep handwritten logic in new files and user-code sections only.

### Scope creep in protocol design

Risk:
Relay, uplink, and generic networking needs can bloat phase 1.

Mitigation:
Limit phase 1 to addressed `DRAW_TEXT`, `ACK`, and `ERROR`.

### Driver transplant mess

Risk:
Directly copying reference projects will preserve assumptions from standalone demos and make the monorepo brittle.

Mitigation:
Rewrite modules into the main repo with clear service boundaries.

### EPD update complexity

Risk:
Partial-refresh and advanced drawing behavior can introduce display-specific bugs early.

Mitigation:
Support text rendering with conservative refresh policy first.

## Future Extension Path

The chosen architecture should support phase 2 without structural rewrite:

- multiple slave addresses
- slave-to-master event messages
- relay behavior such as `slave2 -> master -> slave1`
- new application message types for additional peripherals
- repurposed SPI-attached subcomponents on boards that share the same base hardware

These are intentionally deferred, but the frame format and module boundaries are designed so they can be added incrementally.

## Definition of Done

Phase 1 is complete when:

- one STM32CubeIDE project builds both master and slave variants
- role and node address come from a central config header
- the host can send a `DRAW_TEXT` style command over USB CDC
- the master converts it to an addressed RF frame
- the addressed slave renders the requested text on the e-ink display
- non-target slaves ignore the packet
- the repo remains organized and STM32IDE-compatible
