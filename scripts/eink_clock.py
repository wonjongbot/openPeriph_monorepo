#!/usr/bin/env python3
"""
eink_clock.py — Large ASCII block-digit clock for the RF e-ink display.

Renders the current time using 5-row tall ASCII block digits, plus
date and a rotating fun fact below.

Usage:
    python3 scripts/eink_clock.py --port /dev/tty.usbmodem... --dst 0x20
    python3 scripts/eink_clock.py --port /dev/tty.usbmodem... --dst 0x20 --loop 5

Display: 648x480 @ Font16 -> 58 chars/row, 30 rows
The clock occupies rows 0-4 (digits) + separator + date/info below.
"""

import argparse
import sys
import os
import datetime
import time

sys.path.insert(0, os.path.dirname(__file__))
from eink_canvas import EinkCanvas

# 5-row tall ASCII block digit glyphs, each 5 chars wide
DIGITS = {
    '0': [
        " ### ",
        "#   #",
        "#   #",
        "#   #",
        " ### ",
    ],
    '1': [
        "  #  ",
        " ##  ",
        "  #  ",
        "  #  ",
        " ### ",
    ],
    '2': [
        "#### ",
        "    #",
        " ### ",
        "#    ",
        "#####",
    ],
    '3': [
        "#####",
        "    #",
        " ####",
        "    #",
        "#####",
    ],
    '4': [
        "#   #",
        "#   #",
        "#####",
        "    #",
        "    #",
    ],
    '5': [
        "#####",
        "#    ",
        "#### ",
        "    #",
        "#### ",
    ],
    '6': [
        " ####",
        "#    ",
        "#####",
        "#   #",
        " ### ",
    ],
    '7': [
        "#####",
        "    #",
        "   # ",
        "  #  ",
        "  #  ",
    ],
    '8': [
        " ### ",
        "#   #",
        " ### ",
        "#   #",
        " ### ",
    ],
    '9': [
        " ### ",
        "#   #",
        " ####",
        "    #",
        " ### ",
    ],
    ':': [
        "  ",
        " *",
        "  ",
        " *",
        "  ",
    ],
    ' ': [
        "   ",
        "   ",
        "   ",
        "   ",
        "   ",
    ],
}

FUN_FACTS = [
    "RF at 315 MHz. CC1101. Retries: up to 8.",
    "E-ink: bistable. Keeps image with no power!",
    "STM32 + CC1101 = magic over the air.",
    "Built with love @ ECE 395 UIUC 2026.",
    "Staged draw: N commands, 1 EPD refresh.",
    "648 x 480 pixels of pure e-ink glory.",
]


def build_clock_rows(time_str: str) -> list:
    """Convert HH:MM string into 5 rows of ASCII block art."""
    rows = [""] * 5
    for ch in time_str:
        glyph = DIGITS.get(ch, DIGITS[' '])
        for i in range(5):
            rows[i] += glyph[i] + " "
    return rows


def build_display(now: datetime.datetime) -> list:
    time_str = now.strftime("%H:%M")
    date_str = now.strftime("%A, %B %d %Y")
    fact = FUN_FACTS[now.minute % len(FUN_FACTS)]

    clock_rows = build_clock_rows(time_str)

    lines = []
    lines.append("")
    lines.extend(clock_rows)
    lines.append("")
    lines.append("-" * 38)
    lines.append(f"  {date_str}")
    lines.append("")
    lines.append(f"  {fact}")

    return lines


def render(canvas: EinkCanvas, clear: bool = True):
    now = datetime.datetime.now()
    lines = build_display(now)

    if clear:
        canvas.clear()

    print(f"Rendering clock: {now.strftime('%H:%M:%S')}")
    canvas.draw_multiline(lines, x_start=4, y_start=4, font=16, line_spacing=18)
    canvas.flush(full_refresh=True)


def main():
    parser = argparse.ArgumentParser(description="ASCII block clock for e-ink display")
    parser.add_argument('--port', required=True, help='Serial port')
    parser.add_argument('--dst', required=True, type=lambda x: int(x, 0),
                        help='Destination RF address (e.g. 0x20)')
    parser.add_argument('--loop', type=int, default=0,
                        help='Refresh interval in minutes (0 = display once)')
    parser.add_argument('--chunk-size', type=int, default=20,
                        help='Max chars per RF draw call')
    args = parser.parse_args()

    with EinkCanvas(args.port, args.dst, chunk_size=args.chunk_size) as canvas:
        render(canvas, clear=True)

        if args.loop > 0:
            print(f"Looping every {args.loop} minute(s). Ctrl+C to stop.")
            try:
                while True:
                    time.sleep(args.loop * 60)
                    render(canvas, clear=True)
            except KeyboardInterrupt:
                print("\nStopped.")

    print("Done.")


if __name__ == '__main__':
    main()
