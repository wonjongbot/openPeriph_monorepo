# openPeriph Monorepo

STM32 + CC1101 + e-paper experiments for a USB-to-RF master board and one or
more RF slave boards.

## Current Demo: Lucky Button

The current end-to-end demo is:

```text
slave PC11 button press
  -> slave sends RF_MSG_AGENT_TRIGGER
  -> master forwards PKT_TYPE_AGENT_EVENT over USB CDC
  -> scripts/lucky_button.py chooses an approved mode
  -> host sends RF draw commands back through the master
  -> slave e-paper display updates
```

Approved demo modes are:

- `weather`
- `art`
- `news`
- `repo`
- `fortune`
- `random`, which chooses one of the approved modes

The `--agent codex` and `--agent claude` flags are accepted by the script, but
the current implementation still uses local deterministic demo generators. Real
Codex/Claude callback integration is a future step.

## Hardware Roles

Use two boards:

- `MASTER`: connected to the computer over USB CDC and connected to a CC1101
- `SLAVE`: connected to a CC1101, e-paper display, and the PC11 button

Recommended addresses:

- master node address: `0x01`
- slave node address: `0x22`
- slave `OPENPERIPH_MASTER_ADDR`: `0x01`

The important rule is:

```text
master OPENPERIPH_NODE_ADDR == slave OPENPERIPH_MASTER_ADDR
host --dst == slave OPENPERIPH_NODE_ADDR
```

If the slave LED flickers but the master sees no event, check this address rule
first.

## Slave Setup

In [Core/Inc/openperiph_config.h](Core/Inc/openperiph_config.h), build the slave
firmware with:

```c
#define OPENPERIPH_ROLE OPENPERIPH_ROLE_SLAVE
#define OPENPERIPH_NODE_ADDR 0x22U
#define OPENPERIPH_MASTER_ADDR 0x01U
```

Wire the button from `PC11` to `GND`.

Behavior:

- `PC11` is configured as input with internal pull-up.
- The button is active-low.
- A stable low press after debounce sends one RF trigger.
- `PC14` pulses for about 100 ms on accepted button press, so you can see that
  the button path fired.

Flash this build onto the slave board with STM32CubeIDE / ST-Link.

## Master Setup

In [Core/Inc/openperiph_config.h](Core/Inc/openperiph_config.h), build the
master firmware with:

```c
#define OPENPERIPH_ROLE OPENPERIPH_ROLE_MASTER
#define OPENPERIPH_NODE_ADDR 0x01U
```

Flash this build onto the master board. Keep the master plugged into your
computer over USB CDC.

## Build From Terminal

The STM32 toolchain is under the STM32CubeIDE app bundle:

```bash
GNU_TOOLS="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.100.202509120712/tools"
PATH="$GNU_TOOLS/bin:$PATH" make -C Debug all -j4
```

Use STM32CubeIDE to flash after building, or just build and flash inside
STM32CubeIDE.

## Host Setup

Install the Python serial dependency:

```bash
python3 -m pip install pyserial
```

Find the master USB CDC port on macOS:

```bash
ls /dev/tty.usbmodem* /dev/cu.usbmodem* 2>/dev/null
```

Use the master port in the commands below.

## Run The Lucky Button Demo

Start the watcher from the repo root:

```bash
python3 scripts/lucky_button.py \
  --port /dev/tty.usbmodemXXXX \
  --dst 0x22 \
  --mode random
```

Then press the slave button.

Expected result:

- slave `PC14` flickers once
- the host prints a line like `event 3 from 0x22: fortune`
- the slave e-paper refreshes with the generated content

Force a specific mode:

```bash
python3 scripts/lucky_button.py --port /dev/tty.usbmodemXXXX --dst 0x22 --mode fortune
python3 scripts/lucky_button.py --port /dev/tty.usbmodemXXXX --dst 0x22 --mode repo
python3 scripts/lucky_button.py --port /dev/tty.usbmodemXXXX --dst 0x22 --mode art
```

## Useful Sanity Checks

Ping the master over USB:

```bash
python3 scripts/send_data.py --port /dev/tty.usbmodemXXXX --ping
```

Ask the master for status:

```bash
python3 scripts/send_data.py --port /dev/tty.usbmodemXXXX --status
```

Draw directly to the slave without using the button:

```bash
python3 scripts/send_data.py \
  --port /dev/tty.usbmodemXXXX \
  --draw-text "Hello EPD" \
  --dst 0x22 \
  --x 10 \
  --y 20 \
  --font 16 \
  --clear-first \
  --full-refresh
```

## Debug Checklist

If the slave LED flickers but the master sees nothing:

- confirm the master was flashed as `OPENPERIPH_ROLE_MASTER`
- confirm the master node address is `0x01`
- confirm the slave `OPENPERIPH_MASTER_ADDR` is also `0x01`
- confirm both boards are on the same RF channel
- confirm both CC1101 modules are attached and powered

If the host prints the event but the display does not update:

- confirm `--dst` matches the slave node address
- try a direct `scripts/send_data.py --draw-text ...` command
- check that the slave firmware was built for the connected e-paper panel
- keep the boards close together for the first test

## More Detail

The longer experiment guide is in
[docs/usage/openperiph-experiment-quickstart.md](docs/usage/openperiph-experiment-quickstart.md).
