# RF Draw Staging Reliability Design

## Summary

This design changes the RF draw-text path from a single fire-and-forget frame into a staged stop-and-wait transfer that is more tolerant of an unreliable CC1101 link.

The new flow is:

1. the master sends `DRAW_START`
2. the slave clears any previous staged draw transaction and ACKs
3. the master sends one `DRAW_CHUNK` at a time and waits for an ACK after each chunk
4. the master sends `DRAW_COMMIT`
5. the slave renders the staged text to the EPD and returns a final ACK or ERROR

This keeps only one RF packet in flight at a time, makes retransmission deterministic, and gives the master a bounded way to recover from lost packets or lost ACKs.

## Goals

- Improve draw-text reliability on a marginal RF link without depending on hardware changes first.
- Make the draw-text path observable end to end instead of ACKing USB as soon as the master transmits one RF frame.
- Ensure a new draw transaction can always replace a stuck partial transaction on the slave.
- Bound all retries so the master never blocks forever waiting on RF.
- Preserve the existing `rf_ping` diagnostic while moving it to the same bounded-request pattern.
- Make host-side diagnostics expose retry behavior clearly enough to guide later RF tuning and hardware triage.

## Non-Goals

- Sliding-window or multi-packet-in-flight transport.
- General reliable RF transport for arbitrary application types.
- Supporting more than one staged draw transaction on the slave at a time.
- Solving root-cause RF weakness in hardware or CC1101 tuning in the same patch.
- Building a production-grade RF benchmark suite beyond a focused bench diagnostic helper.

## Current Context

Today the repo behaves like this:

- `rf_ping` already retries with a short per-attempt timeout in `rf_link.c`
- `PKT_TYPE_DRAW_TEXT` is turned into one RF frame in `AppMaster_SendDrawText()`
- the master sends USB ACK immediately after RF transmit success
- the slave either draws the text or silently drops the packet if RF receive or draw validation fails

The current application text limit is `40` ASCII bytes in `app_protocol.h`, so even the largest supported draw-text request already fits in one CC1101 packet. The observed failures therefore point more strongly at RF delivery quality than at a packet-size overflow in the existing code.

## Design Options

### Option 1: Staged Stop-And-Wait Transfer

Use `DRAW_START`, `DRAW_CHUNK`, and `DRAW_COMMIT`, with exactly one RF packet in flight and an ACK after each step.

Pros:

- deterministic recovery after lost data or lost ACK
- straightforward to test in the existing codebase
- matches the user's desired "TCP-like" one-packet-at-a-time behavior without implementing full TCP complexity
- lets the slave discard stale state when a new transfer starts

Cons:

- slower than a pipelined transfer
- requires a small amount of slave-side staging state

### Option 2: Single-Frame Draw With End-To-End ACK

Keep one RF draw frame but add a response from the slave after rendering.

Pros:

- smallest code change

Cons:

- does not reduce per-frame RF loss sensitivity
- still loses the whole command when one RF frame is corrupted

### Option 3: Windowed Multi-Packet Transfer

Allow more than one in-flight RF chunk.

Pros:

- better throughput

Cons:

- unnecessary complexity for short text draws
- harder to validate and easier to get wrong on a weak link

### Recommendation

Implement Option 1. Reliability and recoverability matter more than throughput for this path.

## RF Protocol Changes

### New RF Message Types

Extend `RfMessageType_t` with draw-staging message types:

- `RF_MSG_DRAW_START`
- `RF_MSG_DRAW_CHUNK`
- `RF_MSG_DRAW_COMMIT`
- `RF_MSG_DRAW_ACK`
- `RF_MSG_DRAW_ERROR`

Existing `RF_MSG_PING` and `RF_MSG_PONG` remain.

### Transaction Model

Each staged draw uses one transaction id. The master should derive it from the USB packet id, which is already available and naturally unique enough for one in-flight draw command.

The slave maintains exactly one active staged draw transaction:

- receiving `DRAW_START` always discards any previous staged transaction state
- `DRAW_CHUNK` and `DRAW_COMMIT` are accepted only for the currently active transaction id
- a retransmitted `DRAW_CHUNK` for the current transaction and last accepted chunk index is treated as a duplicate and re-ACKed
- any unexpected message for the active transaction returns `DRAW_ERROR`

This prevents deadlock if the previous transfer never completed and the host starts over.

### `DRAW_START` Payload

`DRAW_START` carries the fixed draw metadata plus total text length:

- destination address
- transaction id
- `x`
- `y`
- `font_id`
- `flags`
- total text length

The slave validates:

- destination matches the local node
- total text length is `1..APP_TEXT_MAX_LEN`
- coordinates and font look structurally valid

If valid, the slave clears staged state, stores the metadata, resets the chunk cursor to zero, and returns `DRAW_ACK`.

### `DRAW_CHUNK` Payload

`DRAW_CHUNK` carries:

- transaction id
- chunk index
- chunk length
- chunk bytes

The slave validates:

- transaction id matches the active staged transfer
- chunk index is either the next expected index or a duplicate of the most recently accepted index
- chunk data does not overflow the staged text buffer
- cumulative staged byte count does not exceed the total declared in `DRAW_START`

Behavior:

- next expected chunk: copy data into the staged buffer and ACK
- duplicate of the most recent accepted chunk: do not rewrite state, simply ACK again
- any other index mismatch: send `DRAW_ERROR`

This makes lost ACK recovery safe: the master can resend the same chunk and the slave will not wedge or advance incorrectly.

### `DRAW_COMMIT` Payload

`DRAW_COMMIT` carries:

- transaction id

The slave validates:

- transaction id matches the active transfer
- staged byte count exactly matches total text length

If valid, the slave constructs `AppDrawTextCommand_t` from staged metadata and buffered text, calls `DisplayService_DrawText()`, and then:

- on success: send `DRAW_ACK` and clear staged state
- on draw failure: send `DRAW_ERROR` and clear staged state

If the staged transfer is incomplete, the slave returns `DRAW_ERROR` and clears staged state.

## Reliability Policy

### Per-Step Retry Budget

Each master-side staged-draw step uses:

- default retries: `8`

This means the master can transmit the same `START`, `CHUNK`, or `COMMIT` frame up to 8 times before giving up on that step.

### Absolute Deadline

The full staged draw transaction uses one absolute timeout:

- default transaction deadline: `2000 ms`

The master stops retrying and fails the draw request when:

- a step exceeds its 8-attempt retry budget, or
- the overall transaction runtime exceeds 2000 ms

Using both controls is intentional:

- retry count is easy to reason about during debugging
- absolute timeout guarantees the command unwinds even if repeated RF waits are individually short

### ACK Wait Window

Each RF request/response wait uses a short per-attempt timeout similar to the current ping loop. The exact value can stay near the current ping timing unless measurement on hardware shows it should change. The important behavior is:

- bounded per wait
- bounded retries
- bounded total command time

### Retry Telemetry

The master should count how many retries were needed for:

- `DRAW_START`
- each `DRAW_CHUNK`
- `DRAW_COMMIT`
- `rf_ping`

This data should be surfaced to the host so bench tests can distinguish:

- success with no retries
- success after retries
- total failure after exhausting retries or timing out

## Master Flow

When the master receives `PKT_TYPE_DRAW_TEXT` over USB:

1. decode the draw command locally
2. derive a transaction id from the USB packet id
3. send `DRAW_START` and wait for `DRAW_ACK`
4. split text into RF-sized chunks and, for each chunk:
   - send one `DRAW_CHUNK`
   - wait for the corresponding `DRAW_ACK`
   - retry the same chunk on timeout or mismatched response
5. send `DRAW_COMMIT`
6. wait for final `DRAW_ACK`
7. only then send USB ACK back to the host
8. if any step exhausts retries or exceeds the absolute timeout, send USB NACK

This changes USB draw-text semantics from "RF transmit accepted by master radio" to "slave confirmed the full draw transaction".

## Slave State Machine

The slave draw staging state should include:

- active flag
- source address
- transaction id
- fixed draw metadata (`x`, `y`, `font_id`, `flags`)
- total text length
- accumulated text bytes
- next expected chunk index
- last accepted chunk index
- staged text buffer sized to `APP_TEXT_MAX_LEN`

This is intentionally small and bounded.

## Error Handling

Slave-side `DRAW_ERROR` should cover at least:

- no active transaction
- transaction id mismatch
- chunk index mismatch other than duplicate replay
- staged length overflow
- commit before all bytes are received
- draw/render failure

Master-side failure mapping for USB should distinguish at least:

- RF send failure
- RF timeout / no ACK from slave
- explicit slave-side draw protocol error

The exact NACK reason bytes can be allocated during implementation, but they must remain stable and host-visible once added.

## `rf_ping` Adjustment

`rf_ping` should also use an absolute timeout in addition to retries. It already has retry behavior; the change here is to apply the same "bounded total command time" rule used for staged draws so the host never waits indefinitely when the link is especially bad.

This keeps ping behavior consistent with the new draw transport philosophy.

## Host Diagnostics

### Default CLI Reporting

The host script should print retry information for staged draws and RF ping. Successful commands should report enough information to show whether the link is barely working or comfortably working.

Recommended examples:

- `DRAW_TEXT ok: 4 chunks, 2 retries, 184 ms`
- `PONG from 0x22: 1 retry, 96 ms`
- `DRAW_TEXT failed: timeout after 8 retries on chunk 3`

### Bench Diagnostic Mode

Add a focused host-side diagnostic mode for repeated RF reliability testing. This mode should send a controlled stream of RF operations and summarize:

- total transactions attempted
- total transactions succeeded
- total transactions failed
- aggregate retries
- average retries per successful transaction
- failure rate

Recommended first cut:

- a repeated RF ping test because it isolates link reliability from EPD rendering time
- an optional repeated staged draw test using a fixed short text payload

Example summary:

- `100 attempts, 81 success, 19 failed, 47 total retries, failure rate 19.0%`

This does not need to become a full soak-test framework. A deterministic CLI helper that can be run on the bench is enough for now.

## Testing

Host-side tests should cover:

- `DRAW_START` resets stale staged state
- chunk retransmit with duplicate index is re-ACKed
- out-of-order chunk is rejected
- incomplete commit is rejected
- successful staged draw ACKs only after slave draw success
- master times out after bounded retries / absolute deadline
- `rf_ping` respects both retry count and total timeout
- host CLI reports retry counts and summarized failure statistics

Manual bench validation should check:

- repeated retries do not leave the slave stuck
- starting a new draw after interrupting a previous one works reliably
- shorter chunks improve effective success rate on the current hardware
- repeated diagnostic runs provide stable retry/failure metrics that can be compared before and after any later RF tuning or hardware changes

## Hardware Triage Notes

This software change is intended as the safest immediate mitigation, not proof that the radio hardware is healthy.

Important observations:

- the current supported draw text length is only 40 ASCII bytes, so failures on one-character messages indicate the link problem is not just application payload size
- a wrong crystal-load capacitor or other RF-front-end mismatch could still materially degrade carrier accuracy and sensitivity
- if the staged protocol still shows poor ACK rates even for very small chunks, the next triage step should be targeted CC1101 register and hardware validation rather than more transport complexity

The reported `24 pF` versus `27 pF` capacitor mismatch therefore remains a credible hardware suspect and should stay on the follow-up list after this software hardening pass.
