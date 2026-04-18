#!/usr/bin/env python3
"""
display_art.py — ASCII art scenes for the 5.83" RF e-ink display.

Usage:
    python3 scripts/display_art.py --port /dev/tty.usbmodem... --dst 0x20 --art rocket
    python3 scripts/display_art.py --port /dev/tty.usbmodem... --dst 0x20 --art wave
    python3 scripts/display_art.py --port /dev/tty.usbmodem... --dst 0x20 --art box
    python3 scripts/display_art.py --port /dev/tty.usbmodem... --dst 0x20 --art demo

Display: 648x480 px, Font16 => 58 chars/row x 30 rows
"""

import argparse
import sys
import os
import datetime

sys.path.insert(0, os.path.dirname(__file__))
from eink_canvas import EinkCanvas

# ── Art definitions ───────────────────────────────────────────────────────────

ROCKET = [
    "         /\\         ",
    "        /  \\        ",
    "       | () |       ",
    "       |    |       ",
    "      /|    |\\     ",
    "     / |    | \\    ",
    "    /  |    |  \\   ",
    "   /   |    |   \\  ",
    "  / *  |    |  * \\ ",
    " /*****|    |*****\\",
    "       |    |       ",
    "       |    |       ",
    "      /|    |\\     ",
    "     ( |    | )     ",
    "      \\|    |/     ",
    "       |    |       ",
    "    ~ ~  ~~  ~ ~    ",
    "   ~~ LAUNCH ~~     ",
    "  ~ ECE 395 RF ~    ",
]

WAVE = [
    "~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~",
    "  ~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~  ",
    "~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~",
    "",
    "  ~~~  openPeriph RF Display  ~~~  ",
    "",
    "~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~",
    "  ~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~  ",
    "~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~",
    "",
    "  CC1101  ->  STM32  ->  E-Ink  ",
    "",
    "~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~",
    "  ~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~  ",
    "~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~-~",
]

def make_box_art():
    width = 38
    border = "+" + "-" * (width - 2) + "+"
    blank = "|" + " " * (width - 2) + "|"

    def row(text):
        pad = width - 2 - len(text)
        left = pad // 2
        right = pad - left
        return "|" + " " * left + text + " " * right + "|"

    return [
        border,
        blank,
        row("openPeriph"),
        row("RF + E-Ink Demo"),
        blank,
        row("ECE 395  |  UIUC"),
        blank,
        border,
        "",
        row("CC1101 @ 315 MHz"),
        row("STM32 RF Bridge"),
        row("5.83 inch EPD"),
        "",
        border,
    ]

def make_demo_art():
    now = datetime.datetime.now()
    date_str = now.strftime("%Y-%m-%d  %H:%M")
    return [
        "================================================",
        "",
        "  ___  ____  ____  _  _  ____  ____  ____  _  _ ",
        " / _ \\|  _ \\| ___|| \\| ||  _ \\| ___||  _ \\| || |",
        "| (_) | |_) | _|  |  ` || |_) | _|  | |_) | __ |",
        " \\___/|  __/|____||_|\\_||  __/|____||  __/|_||_|",
        "      |_|               |_|         |_|         ",
        "",
        "  yeehow",
        "  By Peter and Barry",
        "",
        "================================================",
        "",
        f"  {date_str}",
        "",
        "  ECE 395 SP26 Project",
        "  ",
        "",
        "================================================",
    ]

ARTS = {
    'rocket': ROCKET,
    'wave': WAVE,
    'box': make_box_art,
    'demo': make_demo_art,
}

# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="ASCII art for e-ink display")
    parser.add_argument('--port', required=True, help='Serial port')
    parser.add_argument('--dst', required=True, type=lambda x: int(x, 0),
                        help='Destination RF address (e.g. 0x20)')
    parser.add_argument('--art', choices=list(ARTS.keys()), default='demo',
                        help='Which art to display')
    parser.add_argument('--font', choices=['12', '16'], default='16',
                        help='Font size')
    parser.add_argument('--x', type=int, default=5, help='X offset')
    parser.add_argument('--y', type=int, default=5, help='Y offset')
    parser.add_argument('--no-clear', action='store_true',
                        help='Skip clearing display first')
    parser.add_argument('--chunk-size', type=int, default=20,
                        help='Max chars per RF draw call (lower = more reliable)')
    args = parser.parse_args()

    art_src = ARTS[args.art]
    lines = art_src() if callable(art_src) else art_src

    with EinkCanvas(args.port, args.dst, chunk_size=args.chunk_size) as canvas:
        if not args.no_clear:
            print("Clearing display...")
            canvas.clear()

        print(f"Staging {len(lines)} lines of '{args.art}' art...")
        canvas.draw_multiline(lines, x_start=args.x, y_start=args.y,
                              font=int(args.font), clear_first_line=False)

        print("Flushing to display...")
        canvas.flush(full_refresh=True)

    print("Done.")

if __name__ == '__main__':
    main()
