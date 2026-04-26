#!/usr/bin/env python3
"""Host-side lucky button event watcher."""

import argparse
import datetime as _dt
import os
import random
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))

import send_data
from eink_canvas import EinkCanvas


APPROVED_MODES = ('weather', 'art', 'news', 'repo', 'fortune')
MAX_LINE_WIDTH = 58
MAX_LINES = 12


def sanitize_lines(lines, max_lines=MAX_LINES, max_width=MAX_LINE_WIDTH):
    """Return printable ASCII display lines split to the requested width."""
    if max_lines <= 0:
        return []

    cleaned = []

    for line in lines:
        text = ''.join(ch if 32 <= ord(ch) <= 126 else '?' for ch in str(line))
        while len(text) > max_width:
            cleaned.append(text[:max_width])
            if len(cleaned) >= max_lines:
                return cleaned
            text = text[max_width:]
        cleaned.append(text)
        if len(cleaned) >= max_lines:
            return cleaned

    fallback = ["Lucky button", "No content generated"]
    return cleaned or fallback[:max_lines]


def _repo_summary():
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    try:
        status = subprocess.run(
            ['git', 'status', '--short'],
            cwd=repo_root,
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ["Repo pulse", "git status unavailable"]

    if status.returncode != 0:
        return ["Repo pulse", "git status failed"]

    changed = [line for line in status.stdout.splitlines() if line.strip()]
    if not changed:
        return ["Repo pulse", "Working tree is clean"]
    return ["Repo pulse", f"{len(changed)} changed paths"] + changed[:8]


def generate_mode_lines(mode, event, agent='none'):
    now = _dt.datetime.now().strftime('%Y-%m-%d %H:%M')

    if mode == 'weather':
        return sanitize_lines([
            "Weather card",
            now,
            "Network-free demo mode",
            "Look outside, then press again",
            f"event #{event['event_id']} from 0x{event['slave_addr']:02X}",
        ])

    if mode == 'art':
        return sanitize_lines([
            "Button sketch",
            "   +------+",
            "  / luck /|",
            " +------+ |",
            " |  AI  | +",
            " | draws|/",
            " +------+",
        ])

    if mode == 'news':
        return sanitize_lines([
            "Tiny headline desk",
            now,
            "No network fetch in --agent none",
            "Today's scoop: hardware talks back",
            f"uptime low16: {event['uptime_ms']} ms",
        ])

    if mode == 'repo':
        return sanitize_lines(_repo_summary())

    if mode == 'fortune':
        fortunes = [
            "A small input becomes a large adventure.",
            "The next packet carries unreasonable optimism.",
            "A clean debounce is a clean conscience.",
            "Today favors patient radios.",
        ]
        pick = fortunes[(event['event_id'] + event['uptime_ms']) % len(fortunes)]
        return sanitize_lines([
            "Fortune",
            now,
            pick,
            f"event #{event['event_id']}",
        ])

    raise ValueError(f"unsupported mode: {mode}")


def resolve_mode(mode, event, agent='none'):
    selected = random.choice(APPROVED_MODES) if mode == 'random' else mode
    if selected not in APPROVED_MODES:
        raise ValueError(f"unsupported mode: {selected}")
    return selected, generate_mode_lines(selected, event, agent=agent)


def draw_lines(canvas, lines):
    if not canvas.clear():
        return False
    if not canvas.draw_multiline(
        lines,
        x_start=5,
        y_start=5,
        font=16,
        clear_first_line=False,
    ):
        return False
    return canvas.flush(full_refresh=True)


def handle_packet(packet, canvas, mode='random', agent='none'):
    if not packet.get('valid') or packet.get('type') != send_data.PKT_TYPE_AGENT_EVENT:
        return False

    event = send_data.parse_agent_event_payload(packet.get('payload', b''))
    selected, lines = resolve_mode(mode, event, agent=agent)
    print(f"event {event['event_id']} from 0x{event['slave_addr']:02X}: {selected}")
    return draw_lines(canvas, lines)


def run(port, dst, baud=115200, mode='random', agent='none'):
    try:
        import serial
    except ModuleNotFoundError:
        print("pyserial is required. Install with: pip install pyserial")
        return 1

    with serial.Serial(port, baud, timeout=1) as ser:
        time.sleep(0.5)
        send_data.read_response(ser, timeout=0.2)
        with EinkCanvas(port=port, dst=dst, baud=baud) as canvas:
            while True:
                packet = send_data.read_response(ser, timeout=1.0)
                try:
                    handle_packet(packet, canvas, mode=mode, agent=agent)
                except ValueError as exc:
                    print(f"ignored malformed agent event: {exc}")


def main():
    parser = argparse.ArgumentParser(description="Watch master USB for lucky-button events")
    parser.add_argument('--port', required=True)
    parser.add_argument('--dst', type=lambda value: int(value, 0), default=0x22)
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--mode', choices=('random',) + APPROVED_MODES, default='random')
    parser.add_argument('--agent', choices=('none', 'codex', 'claude'), default='none')
    args = parser.parse_args()

    return run(args.port, args.dst, baud=args.baud, mode=args.mode, agent=args.agent)


if __name__ == '__main__':
    raise SystemExit(main())
