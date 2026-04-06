# OpenPeriph Experiment Quickstart

This is the shortest path to run the phase 1 demo:

`PC -> USB CDC -> master -> RF -> slave -> 2.13" EPD text render`

This guide assumes:

- both boards use this same repo/project
- both boards have the common PCB population
- you are using the `2.13_V4` e-paper panel
- you program boards with STM32CubeIDE and ST-Link

## 1. Hardware Setup

You need:

- `Board A` as `MASTER`
- `Board B` as `SLAVE`
- 2.13" e-paper connected to the slave board
- RF module connected on both boards
- USB cable for the master board
- ST-Link access for programming both boards

Recommended addresses for the first test:

- master address: `0x01`
- slave address: `0x22`

## 2. Config File

All role and panel settings live in [openperiph_config.h](/Users/wonjongbot/sp26/ece395/openPeriph_monorepo/Core/Inc/openperiph_config.h).

For the 2.13" display, keep:

```c
#define OPENPERIPH_EPD_PANEL OPENPERIPH_EPD_PANEL_2IN13_V4
```

## 3. Build And Flash The Master

Edit [openperiph_config.h](/Users/wonjongbot/sp26/ece395/openPeriph_monorepo/Core/Inc/openperiph_config.h) to:

```c
#define OPENPERIPH_ROLE OPENPERIPH_ROLE_MASTER
#define OPENPERIPH_NODE_ADDR 0x01U
```

Then in STM32CubeIDE:

1. Open the project.
2. Save `openperiph_config.h`.
3. Build the `Debug` configuration.
4. Program `Board A` with the normal STM32CubeIDE run/debug flow using ST-Link.

If you want to build from terminal first:

```bash
GNU_TOOLS="/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.macos64_1.0.100.202509120712/tools"
PATH="$GNU_TOOLS/bin:$PATH" make -C Debug clean all
```

## 4. Build And Flash The Slave

Edit [openperiph_config.h](/Users/wonjongbot/sp26/ece395/openPeriph_monorepo/Core/Inc/openperiph_config.h) to:

```c
#define OPENPERIPH_ROLE OPENPERIPH_ROLE_SLAVE
#define OPENPERIPH_NODE_ADDR 0x22U
```

Keep the panel as:

```c
#define OPENPERIPH_EPD_PANEL OPENPERIPH_EPD_PANEL_2IN13_V4
```

Then:

1. Save `openperiph_config.h`.
2. Build again.
3. Program `Board B` with STM32CubeIDE using ST-Link.

After flashing the slave, you can change the config file back to master defaults so the repo stays in the normal state:

```c
#define OPENPERIPH_ROLE OPENPERIPH_ROLE_MASTER
#define OPENPERIPH_NODE_ADDR 0x01U
```

You do not need to reflash the master just for changing the file back locally.

## 5. Power-Up Expectations

On boot, firmware blinks `PC14` three times.

If RF init succeeds, the firmware also emits a USB status packet with a string like:

```text
openPeriph USB Bridge v1.0 ready (122x250)
```

That startup string is only visible on the board connected over USB CDC, which for this experiment should be the master.

## 6. Connect To The Master USB CDC Port

Plug the master board into your computer over USB.

On macOS, the port is typically something like:

- `/dev/tty.usbmodem*`
- sometimes `/dev/cu.usbmodem*`

Find it with:

```bash
ls /dev/tty.usbmodem* /dev/cu.usbmodem* 2>/dev/null
```

## 7. Install Python Dependency

The host script needs `pyserial`:

```bash
python3 -m pip install pyserial
```

## 8. Send A Draw-Text Command

From the repo root:

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

Replace `/dev/tty.usbmodemXXXX` with your actual master USB CDC port.

Expected host-side result:

- the script connects to the master
- the master replies with `ACK`

Expected device-side result:

- the slave receives the RF frame addressed to `0x22`
- the slave updates the 2.13" EPD
- `Hello EPD` appears near `(x=10, y=20)`

## 9. Useful Test Commands

Ping the master:

```bash
python3 scripts/send_data.py --port /dev/tty.usbmodemXXXX --ping
```

Ask the master for status:

```bash
python3 scripts/send_data.py --port /dev/tty.usbmodemXXXX --status
```

This now prints the CC1101 `MARCSTATE` byte as reported by firmware.

Render a local EPD test on whichever board is connected over USB:

```bash
python3 scripts/send_data.py --port /dev/tty.usbmodemXXXX --local-hello
```

Expected result:

- the host gets an `ACK`
- the connected board renders `Hello World` on its own EPD

This is useful before debugging RF, because it confirms the local USB command path and local EPD stack are alive.

## 10. First Debug Checks If It Fails

If the host script cannot open the port:

- confirm you plugged in the master, not only the slave
- re-run `ls /dev/tty.usbmodem* /dev/cu.usbmodem* 2>/dev/null`

If the script connects but no `ACK` arrives:

- make sure the master was flashed with `OPENPERIPH_ROLE_MASTER`
- make sure the slave was flashed with `OPENPERIPH_ROLE_SLAVE`
- make sure the destination matches the slave address: `--dst 0x22`
- confirm both boards have RF modules attached and powered

If the slave receives RF but the screen stays blank:

- confirm the slave build still uses `OPENPERIPH_EPD_PANEL_2IN13_V4`
- confirm the 2.13" panel is connected to the slave board, not the master
- try `--local-hello` first on that same board
- try the same command again with `--clear-first --full-refresh`

If you want a very small first test:

```bash
python3 scripts/send_data.py \
  --port /dev/tty.usbmodemXXXX \
  --draw-text "Hi" \
  --dst 0x22 \
  --x 0 \
  --y 0 \
  --font 12 \
  --clear-first \
  --full-refresh
```
