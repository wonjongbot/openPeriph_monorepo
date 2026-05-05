# openClaw → openPeriph display bridge

This file is meant to be pasted into **openClaw's system / instruction prompt**
(or otherwise made visible to the agent) so that, when you talk to openClaw on
Discord, it knows which shell command to run to put something on the
RF-connected e-ink display.

The bridge lives at:
`scripts/openclaw_bridge.py`

Everything below is openClaw-facing.

---

## What this is

There is a small device on the user's desk with an e-ink screen. A second
device, plugged into the user's computer via USB-C, talks to it over a 433 MHz
RF link. The user's computer sends commands to the USB-C device; the USB-C
device forwards them over RF; the desk device renders them on its screen.

You (openClaw) drive this entire chain by running a single Python script:
`python scripts/openclaw_bridge.py <subcommand> [...]`

You do **not** need to specify a serial port. The bridge auto-detects the
STM32 CDC port by USB VID/PID (0x0483 / 0x5740). Slave RF address defaults to
`0x22`. If something looks wrong, run `ports` first.

## Subcommands

| Subcommand | What shows up on the display | Example |
|---|---|---|
| `image path/to/file.png` | A stylized monochrome image via the tile-glyph codec | `python scripts/openclaw_bridge.py image ./photo.png` |
| `text "<msg>"` | Arbitrary text from the user | `python scripts/openclaw_bridge.py text "Meeting starts in 5 minutes"` |
| `fortune` | A short fortune sentence | `python scripts/openclaw_bridge.py fortune` |
| `lucky` | One of weather/art/news/repo/fortune chosen at random | `python scripts/openclaw_bridge.py lucky` |
| `bus --stop IU` | Live MTD bus departures for a stop | `python scripts/openclaw_bridge.py bus --stop IU` |
| `weather --location "Urbana,IL"` | Weather card | `python scripts/openclaw_bridge.py weather --location "Urbana,IL"` |
| `clock` | Current time | `python scripts/openclaw_bridge.py clock` |
| `art rocket` | ASCII-art piece (`rocket`, `wave`, `box`, `demo`) | `python scripts/openclaw_bridge.py art wave` |
| `ping` | Diagnostic: USB-ping host, RF-ping slave | `python scripts/openclaw_bridge.py ping` |
| `ports` | Diagnostic: list visible serial ports | `python scripts/openclaw_bridge.py ports` |

The `bus` subcommand needs an MTD API key. The user can either set the
`MTD_API_KEY` environment variable on the machine where openClaw runs, or
pass `--api-key <key>` explicitly.

## Picking the right subcommand from a Discord message

These are the kinds of messages the user is likely to send, and what to do
with each:

- "Show me a fortune" / "give me a lucky sentence" / "say something nice on
  the screen" → `fortune`
- "Surprise me" / "pick something random" / "lucky button" → `lucky`
- "What's the bus situation" / "show bus stop IU" / "MTD departures" → `bus`
  (use `--stop` if the user names a specific stop, e.g. `--stop PAR`)
- "What's the weather" / "show weather for <city>" → `weather --location <city>`
- "Show the clock" / "what time is it on the display" → `clock`
- "Show the rocket" / "draw the wave" → `art <name>`
- "Show this photo" / "put this image on the eink" → `image <path>`
- Anything else where the user clearly wants their own words on the screen
  ("display 'Lunch at 12:30'", "put 'Hi mom' on the eink") → `text "<the words>"`

If a message is ambiguous (e.g. just "do something"), prefer `lucky` — it
delegates the choice and is the closest analog to the physical lucky button.

## Reporting back to Discord

The bridge prints status lines to stdout. After running it:

- Exit code `0` → it worked. Tell the user something short like "Sent a
  fortune to your display" or "Showing bus stop IU now."
- Non-zero exit code → something failed. Read the last few lines of stdout /
  stderr and pass that back to the user verbatim. Common cases:
  - `could not auto-detect the openPeriph host serial port` → ask the user
    to plug the host in via USB-C, then run `ports` to confirm.
  - `RF ping slave 0x22: FAIL` → the slave is out of range or off; suggest
    moving it closer or power-cycling it.
  - `provide --api-key or set MTD_API_KEY` (bus only) → the user needs to set
    their CUMTD API key.

## Things to NOT do

- Don't try to re-implement the protocol yourself (no raw serial writes).
  Always go through `openclaw_bridge.py`.
- Don't pass `--port` unless the user explicitly tells you which port to
  use; the auto-detection by VID/PID is the right default and survives
  re-plugging.
- Don't run long-running modes (`bus --loop-minutes`, `clock --loop`) from
  Discord without confirming — those occupy the serial port indefinitely.

## One-time setup the user needs (do not run this themselves on every
   command)

```
pip install pyserial
```

If the user reports `pyserial is required`, tell them to run that.
