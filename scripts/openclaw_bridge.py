#!/usr/bin/env python3
"""
openclaw_bridge.py — single CLI entry point that openClaw (or any other
shell-driven agent on Discord, Slack, etc.) can call to drive the
RF-connected e-ink display.

It wraps the existing host scripts (bus_tracker, lucky_button, weather_widget,
eink_clock, display_art) and adds a `text` subcommand for arbitrary strings
sent in over chat.

USB port handling
-----------------
The STM32F411 host enumerates as a USB CDC ACM device with:
    VID = 0x0483  (STMicroelectronics)
    PID = 0x5740  (STM32 Virtual COM Port)

That assignment is picked by Windows / macOS / Linux when the device is
plugged in, which is why the user-visible name (COM3, COM5, /dev/ttyACM0,
/dev/tty.usbmodem...) can change between plug-ins. Instead of hard-coding
the name, this bridge auto-discovers the port by VID/PID. You can still
override with --port if you want.

Subcommands
-----------
    bus        Show MTD bus departures (wraps bus_tracker.py)
    fortune    Show a random fortune sentence (uses lucky_button generator)
    image      Show a local image file using the tile-glyph codec
    text       Show an arbitrary chat-supplied string
    weather    Show a weather card (wraps weather_widget.py)
    clock      Show the current time (wraps eink_clock.py)
    art        Show an ASCII-art piece (wraps display_art.py)
    ports      List candidate STM32 serial ports (diagnostic)
    ping       Send a USB ping + RF ping to the slave (diagnostic)

Examples (the kind of thing openClaw should run)
------------------------------------------------
    python scripts/openclaw_bridge.py fortune
    python scripts/openclaw_bridge.py image path/to/photo.png
    python scripts/openclaw_bridge.py text "Meeting starts in 5 minutes"
    python scripts/openclaw_bridge.py bus --stop IU
    python scripts/openclaw_bridge.py weather --location "Urbana,IL"
    python scripts/openclaw_bridge.py art rocket

Environment variables (optional)
--------------------------------
    OPENPERIPH_PORT  Force a specific serial port (skips auto-detect)
    OPENPERIPH_DST   Force a specific RF destination (default 0x22)
    MTD_API_KEY      Used by the `bus` subcommand
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))

# Vendor / product IDs for the STM32 USB CDC enumeration in this firmware.
# Defined in USB_DEVICE/App/usbd_desc.c.
STM32_USB_VID = 0x0483
STM32_USB_PID = 0x5740

DEFAULT_DST = 0x20

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
PYTHON_EXE = sys.executable or "python"


# ── Port discovery ──────────────────────────────────────────────────────────

def find_stm32_port() -> str | None:
    """Return the device path of the first STM32 CDC port found, or None."""
    try:
        from serial.tools import list_ports
    except ModuleNotFoundError:
        sys.stderr.write(
            "pyserial is required: pip install pyserial\n"
        )
        return None

    candidates = []
    for info in list_ports.comports():
        if info.vid == STM32_USB_VID and info.pid == STM32_USB_PID:
            candidates.append(info.device)

    if candidates:
        return candidates[0]

    # Fallback: anything with "STM" in the description (some Windows drivers
    # don't expose a clean VID/PID through pyserial).
    for info in list_ports.comports():
        desc = (info.description or "") + " " + (info.manufacturer or "")
        if "STM" in desc.upper():
            return info.device
    return None


def list_ports_cmd() -> int:
    """Print every visible serial port so the user can sanity-check."""
    try:
        from serial.tools import list_ports
    except ModuleNotFoundError:
        print("pyserial is required: pip install pyserial")
        return 1

    found_any = False
    for info in list_ports.comports():
        found_any = True
        marker = ""
        if info.vid == STM32_USB_VID and info.pid == STM32_USB_PID:
            marker = "  <-- openPeriph host (STM32 CDC)"
        vid = f"{info.vid:04X}" if info.vid is not None else "----"
        pid = f"{info.pid:04X}" if info.pid is not None else "----"
        print(f"  {info.device:10s}  VID:{vid} PID:{pid}  {info.description}{marker}")
    if not found_any:
        print("  (no serial ports visible)")
    auto = find_stm32_port()
    if auto:
        print(f"\nAuto-detected openPeriph host: {auto}")
    else:
        print("\nNo openPeriph host detected. Plug it in via USB-C and retry.")
    return 0


def resolve_port(arg_port: str | None) -> str:
    """Resolve port from --port arg, env var, or auto-detect; exit cleanly otherwise."""
    if arg_port:
        return arg_port
    env_port = os.environ.get("OPENPERIPH_PORT")
    if env_port:
        return env_port
    auto = find_stm32_port()
    if auto:
        return auto
    sys.stderr.write(
            "ERROR: could not auto-detect the openPeriph host serial port.\n"
            "  - Make sure the host is plugged in via USB-C.\n"
            "  - Run `python openclaw_bridge.py ports` to see what's visible.\n"
            "  - Or pass --port COM5 (Windows) / /dev/ttyACM0 (Linux) explicitly.\n"
    )
    sys.exit(2)


def resolve_dst(arg_dst: int | None) -> int:
    if arg_dst is not None:
        return arg_dst
    env_dst = os.environ.get("OPENPERIPH_DST")
    if env_dst:
        try:
            return int(env_dst, 0)
        except ValueError:
            pass
    return DEFAULT_DST


# ── Helpers for invoking the existing scripts ───────────────────────────────

def _hex(addr: int) -> str:
    return f"0x{addr:02X}"


def _run_script(script_name: str, extra_args: list[str], port: str, dst: int) -> int:
    """Spawn one of the sibling scripts as a subprocess. Returns its exit code."""
    script_path = os.path.join(SCRIPTS_DIR, script_name)
    cmd = [PYTHON_EXE, script_path, "--port", port, "--dst", _hex(dst), *extra_args]
    print(f"$ {' '.join(cmd)}")
    try:
        return subprocess.call(cmd)
    except FileNotFoundError:
        sys.stderr.write(f"ERROR: missing script {script_path}\n")
        return 1


# ── Subcommand: bus ─────────────────────────────────────────────────────────

def cmd_bus(args) -> int:
    extra = ["--stop", args.stop]
    if args.api_key:
        extra += ["--api-key", args.api_key]
    if args.loop_minutes:
        extra += ["--loop-minutes", str(args.loop_minutes)]
    return _run_script("bus_tracker.py", extra, args.port, args.dst)


# ── Subcommand: weather ─────────────────────────────────────────────────────

def cmd_weather(args) -> int:
    extra = ["--location", args.location]
    return _run_script("weather_widget.py", extra, args.port, args.dst)


# ── Subcommand: clock ───────────────────────────────────────────────────────

def cmd_clock(args) -> int:
    extra = []
    if args.loop:
        extra += ["--loop", str(args.loop)]
    return _run_script("eink_clock.py", extra, args.port, args.dst)


# ── Subcommand: art ─────────────────────────────────────────────────────────

def cmd_art(args) -> int:
    extra = ["--art", args.art]
    return _run_script("display_art.py", extra, args.port, args.dst)


# ── Subcommand: text ────────────────────────────────────────────────────────

def cmd_text(args) -> int:
    """Push arbitrary chat text straight to the slave display."""
    from eink_canvas import EinkCanvas

    text = args.text.strip()
    if not text:
        sys.stderr.write("ERROR: --text/positional text is empty\n")
        return 1

    # Wrap into ~58-char-wide lines (font 16 on a 648 px panel).
    max_width = 58
    lines: list[str] = []
    for chunk in text.splitlines() or [text]:
        words = chunk.split()
        cur = ""
        for w in words:
            cand = w if not cur else f"{cur} {w}"
            if len(cand) <= max_width:
                cur = cand
            else:
                if cur:
                    lines.append(cur)
                cur = w[:max_width]
        if cur:
            lines.append(cur)
    if not lines:
        lines = [text[:max_width]]

    # Sanitize to printable ASCII (the firmware fonts don't cover Unicode).
    safe_lines = [
        "".join(ch if 32 <= ord(ch) <= 126 else "?" for ch in ln)
        for ln in lines[:24]
    ]

    print("Display lines:")
    for ln in safe_lines:
        print(f"  {ln}")

    with EinkCanvas(port=args.port, dst=args.dst) as canvas:
        if not canvas.clear():
            print("ERROR: clear() failed")
            return 1
        if not canvas.draw_multiline(safe_lines, x_start=5, y_start=5, font=16):
            print("ERROR: draw_multiline() failed")
            return 1
        if not canvas.flush(full_refresh=True):
            print("ERROR: flush() failed")
            return 1
    print("OK: text pushed to slave display.")
    return 0


# ── Subcommand: image ───────────────────────────────────────────────────────

def cmd_image(args) -> int:
    from eink_canvas import EinkCanvas

    if not os.path.exists(args.image_path):
        print(f"ERROR: file not found: {args.image_path}")
        return 1

    with EinkCanvas(port=args.port, dst=args.dst) as canvas:
        if not canvas.draw_image_file_as_tilemap(args.image_path, clear_first=True):
            print("ERROR: draw_image_file_as_tilemap() failed")
            return 1
        if not canvas.flush(full_refresh=True):
            print("ERROR: flush() failed")
            return 1
    print("OK: image pushed to slave display.")
    return 0


# ── Subcommand: fortune ─────────────────────────────────────────────────────

def cmd_fortune(args) -> int:
    """Generate a fortune line and render it. Reuses lucky_button's generator."""
    import datetime as _dt
    import random
    import lucky_button
    from eink_canvas import EinkCanvas

    mode = args.mode  # 'fortune', 'art', 'weather', 'news', 'repo', 'random'
    # lucky_button expects a synthetic agent event dict.
    event = {
        "slave_addr": args.dst,
        "event_id": random.randint(0, 255),
        "uptime_ms": int(_dt.datetime.now().timestamp() * 1000) & 0xFFFF,
    }
    selected, lines = lucky_button.resolve_mode(mode, event, agent="none")
    print(f"Mode: {selected}")
    for ln in lines:
        print(f"  {ln}")

    with EinkCanvas(port=args.port, dst=args.dst) as canvas:
        if not lucky_button.draw_lines(canvas, lines):
            print("ERROR: draw failed")
            return 1
    print(f"OK: pushed '{selected}' card to slave display.")
    return 0


# ── Subcommand: ping ────────────────────────────────────────────────────────

def cmd_ping(args) -> int:
    """USB ping the host, then RF ping the slave; print both results.

    Note: send_data.send_ping() returns None (it just prints), so we issue
    the CMD_PING ourselves and read the response so we can return a real
    boolean. send_rf_ping() does return a bool, so we use that directly.
    """
    import serial  # pyserial check
    import send_data

    try:
        ser = serial.Serial(args.port, 115200, timeout=2)
    except Exception as exc:
        print(f"ERROR: cannot open {args.port}: {exc}")
        return 1
    try:
        # USB ping: build CMD_PING ourselves, check response type.
        ping_frame = send_data.build_packet(
            send_data.PKT_TYPE_COMMAND, bytes([send_data.CMD_PING])
        )
        ser.write(ping_frame)
        resp = send_data.read_response(ser, timeout=2.0)
        usb_ok = bool(resp.get("valid")) and resp.get("type") == send_data.PKT_TYPE_ACK
        print(f"USB ping host (COM):  {'OK' if usb_ok else 'FAIL'}"
              + ("" if usb_ok else f"  (resp={resp})"))

        # RF ping: send_rf_ping returns a bool already and prints details.
        rf_ok = send_data.send_rf_ping(ser, args.dst)
        print(f"RF ping slave {_hex(args.dst)}: {'OK' if rf_ok else 'FAIL'}")

        if usb_ok and not rf_ok:
            print()
            print("USB link is healthy — the master STM32 is talking to your PC.")
            print("RF link is the problem. Check, in this order:")
            print("  1. Is the slave board powered on and running its firmware?")
            print("     (LEDs / startup output on the slave's UART, if you have one wired up)")
            print("  2. Is the slave within ~few meters of the master, no thick walls?")
            print("  3. Is the slave actually built with OPENPERIPH_ROLE_SLAVE and")
            print("     OPENPERIPH_NODE_ADDR == 0x22? (see Core/Inc/openperiph_config.h)")
            print("  4. Are both boards on the same RF channel? Default is 0x00 on both.")
            print("  5. Try power-cycling the slave; CC1101 sometimes needs a fresh init.")
        return 0 if (usb_ok and rf_ok) else 1
    finally:
        ser.close()


# ── Argparse wiring ─────────────────────────────────────────────────────────

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="openclaw_bridge",
        description="Bridge between an agent (e.g. openClaw on Discord) and the openPeriph "
                    "RF e-ink display.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Port resolution order: --port flag, then $OPENPERIPH_PORT, then auto-detect by "
            f"USB VID 0x{STM32_USB_VID:04X} / PID 0x{STM32_USB_PID:04X}."
        ),
    )
    p.add_argument("--port", default=None,
                   help="Serial port (default: auto-detect STM32 CDC, then $OPENPERIPH_PORT).")
    p.add_argument("--dst", default=None, type=lambda x: int(x, 0),
                   help=f"RF destination address (default: {_hex(DEFAULT_DST)} or $OPENPERIPH_DST). "
                        "Override per-call with --dst 0x22 etc.")

    sub = p.add_subparsers(dest="cmd", required=True)

    # bus
    pb = sub.add_parser("bus", help="Show MTD bus departures on the display")
    pb.add_argument("--stop", default="IU", help="MTD stop ID (default: IU)")
    pb.add_argument("--api-key", default=None, help="MTD API key (or set MTD_API_KEY env var)")
    pb.add_argument("--loop-minutes", type=int, default=0, help="Refresh cadence (0 = once)")
    pb.set_defaults(func=cmd_bus)

    # weather
    pw = sub.add_parser("weather", help="Show a weather card")
    pw.add_argument("--location", default="Urbana,IL", help="Location string")
    pw.set_defaults(func=cmd_weather)

    # clock
    pc = sub.add_parser("clock", help="Show the current time")
    pc.add_argument("--loop", type=int, default=0, help="Loop refresh seconds (0 = once)")
    pc.set_defaults(func=cmd_clock)

    # art
    pa = sub.add_parser("art", help="Show an ASCII-art piece")
    pa.add_argument("art", choices=["rocket", "wave", "box", "demo"], default="demo", nargs="?")
    pa.set_defaults(func=cmd_art)

    # text
    pt = sub.add_parser("text", help="Push arbitrary text from chat")
    pt.add_argument("text", help="The string to display (use quotes for multi-word)")
    pt.set_defaults(func=cmd_text)

    # image
    pi = sub.add_parser("image", help="Push a local image file via the tile-glyph codec")
    pi.add_argument("image_path", help="Path to the image file to render")
    pi.set_defaults(func=cmd_image)

    # fortune (a.k.a. "lucky")
    pf = sub.add_parser("fortune", help="Show a random fortune sentence (lucky-button modes)")
    pf.add_argument("--mode", choices=("random", "fortune", "weather", "art", "news", "repo"),
                    default="fortune",
                    help="Lucky-button mode to render (default: fortune)")
    pf.set_defaults(func=cmd_fortune)
    # Alias: `lucky` == `fortune` with mode=random
    pl = sub.add_parser("lucky", help="Alias of `fortune --mode random`")
    pl.add_argument("--mode", choices=("random", "fortune", "weather", "art", "news", "repo"),
                    default="random")
    pl.set_defaults(func=cmd_fortune)

    # ping
    pp = sub.add_parser("ping", help="USB-ping host and RF-ping slave")
    pp.set_defaults(func=cmd_ping)

    # ports
    pl2 = sub.add_parser("ports", help="List candidate serial ports (diagnostic)")
    pl2.set_defaults(func=lambda args: list_ports_cmd())

    return p


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    # The `ports` subcommand is the only one that doesn't need a port resolved.
    if args.cmd != "ports":
        args.port = resolve_port(args.port)
        args.dst = resolve_dst(args.dst)
        print(f"# port={args.port} dst={_hex(args.dst)}")

    return args.func(args) or 0


if __name__ == "__main__":
    raise SystemExit(main())
