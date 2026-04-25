#!/usr/bin/env python3
"""
bus_tracker.py — Graphic-style MTD departures on the RF e-ink display.

Uses the CUMTD developer API conservatively:
- GetDeparturesByStop once per refresh cycle
- GetVehicle only for the top few live departures

The display is still text-rendered, but arranged as transit-style cards with
route badges, ETA blocks, and a vertical live progress rail.
"""

import argparse
import datetime
import json
import os
import sys
import time
import urllib.parse
import urllib.request
from urllib.error import HTTPError

sys.path.insert(0, os.path.dirname(__file__))
from eink_canvas import EinkCanvas

API_VERSION = "v2.2"
API_FORMAT = "json"
MTD_API_BASE = f"https://developer.mtd.org/api/{API_VERSION}/{API_FORMAT}"

FONT = 12
LINE_SPACING = 12
X_START = 4
Y_START = 4
CARD_WIDTH = 28
CARD_HEIGHT = 12
CARD_GAP = 2
MAX_FEATURED = 3
VEHICLE_CACHE_TTL_SECONDS = 60


def normalize_api_key(raw: str | None) -> str | None:
    if raw is None:
        return None
    value = str(raw).strip()
    return value or None


def describe_http_error(err: HTTPError) -> str:
    body = ""
    try:
        if err.fp is not None:
            body = err.fp.read().decode(errors="replace")
    except Exception:
        body = ""

    if body:
        try:
            data = json.loads(body)
            payload = get_rsp_payload(data)
            stat = payload.get("stat", {})
            msg = stat.get("msg")
            if msg:
                return f"HTTP {err.code}: {msg}"
        except Exception:
            pass

    return f"HTTP {err.code}: {err.reason}"


def fetch_json(method: str, params: dict) -> dict:
    query = urllib.parse.urlencode(params)
    url = f"{MTD_API_BASE}/{method}?{query}"
    try:
        with urllib.request.urlopen(url, timeout=8) as resp:
            return json.loads(resp.read().decode())
    except HTTPError as exc:
        raise RuntimeError(describe_http_error(exc)) from exc


def get_rsp_payload(data: dict) -> dict:
    if isinstance(data, dict) and isinstance(data.get("rsp"), dict):
        return data["rsp"]
    return data if isinstance(data, dict) else {}


def ensure_ok_response(data: dict):
    payload = get_rsp_payload(data)
    stat = payload.get("stat", {})
    code = stat.get("code", 200)
    try:
        code = int(code)
    except (TypeError, ValueError):
        code = 200
    if code >= 300:
        raise RuntimeError(stat.get("msg", f"API error {code}"))


def fetch_departures(api_key: str, stop_id: str, preview_minutes: int = 30, count: int = 8) -> dict:
    data = fetch_json(
        "getdeparturesbystop",
        {
            "key": api_key,
            "stop_id": stop_id,
            "pt": max(0, min(preview_minutes, 60)),
            "count": max(1, count),
        },
    )
    ensure_ok_response(data)
    return data


def fetch_vehicle(api_key: str, vehicle_id: str) -> dict:
    data = fetch_json(
        "getvehicle",
        {
            "key": api_key,
            "vehicle_id": vehicle_id,
        },
    )
    ensure_ok_response(data)
    return data


def extract_stop_and_departures(data: dict, fallback_stop_id: str):
    payload = get_rsp_payload(data)
    stops = payload.get("stops") or payload.get("stop") or []
    stop_name = fallback_stop_id
    if isinstance(stops, list) and stops:
        stop_name = stops[0].get("stop_name", fallback_stop_id)
    elif isinstance(stops, dict):
        stop_name = stops.get("stop_name", fallback_stop_id)

    departures = payload.get("departures") or []
    if not isinstance(departures, list):
        departures = []
    return stop_name, departures


def extract_vehicle(data: dict):
    payload = get_rsp_payload(data)
    vehicle = payload.get("vehicle")
    if isinstance(vehicle, dict):
        return vehicle
    vehicles = payload.get("vehicles")
    if isinstance(vehicles, list) and vehicles:
        return vehicles[0]
    if isinstance(payload, dict) and "vehicle_id" in payload:
        return payload
    return None


def departure_minutes(departure: dict) -> int:
    try:
        return int(departure.get("expected_mins", 999))
    except (TypeError, ValueError):
        return 999


def select_featured_departures(departures: list, limit: int = MAX_FEATURED) -> list:
    def rank(item):
        monitored = bool(item.get("is_monitored"))
        scheduled = bool(item.get("is_scheduled", True))
        return (
            0 if monitored else 1,
            0 if scheduled else 1,
            departure_minutes(item),
            str(item.get("route", {}).get("route_short_name", "ZZZ")),
        )

    return sorted(departures, key=rank)[:limit]


def short_route_name(departure: dict) -> str:
    route = departure.get("route") or {}
    name = route.get("route_short_name") or route.get("route_id") or "?"
    return str(name)[:4]


def abbreviate_stop_id(stop_id: str, width: int = 6) -> str:
    if not stop_id:
        return "--"
    stop_id = str(stop_id)
    label = stop_id.split(":")[0]
    return label[:width]


def eta_label(minutes: int) -> str:
    if minutes < 0:
        return "NOW"
    if minutes == 0:
        return "<1m"
    if minutes > 99:
        return "99+"
    return f"{minutes}m"


def wrap_headsign(text: str, width: int, lines: int = 2) -> list:
    words = str(text or "No headsign").split()
    out = []
    current = ""

    for word in words:
        candidate = word if not current else f"{current} {word}"
        if len(candidate) <= width:
            current = candidate
            continue
        if current:
            out.append(current[:width])
        current = word[:width]
        if len(out) >= lines:
            break

    if len(out) < lines and current:
        out.append(current[:width])
    while len(out) < lines:
        out.append("")
    return out[:lines]


def format_updated_label(last_updated: str) -> str:
    if not last_updated:
        return "upd --:--"
    try:
        dt = datetime.datetime.fromisoformat(str(last_updated).replace("Z", "+00:00"))
    except ValueError:
        return "upd --:--"
    return f"upd {dt.strftime('%H:%M')}"


def progress_row_index(minutes: int, steps: int) -> int:
    capped = max(0, min(minutes, 20))
    if steps <= 1:
        return 0
    return int(round((20 - capped) / 20.0 * (steps - 1)))


def fit_card_line(text: str, width: int) -> str:
    return f"|{text[:width - 2]:<{width - 2}}|"


def build_departure_card(departure: dict, vehicle: dict | None, stop_id: str, width: int = CARD_WIDTH, height: int = CARD_HEIGHT) -> list:
    inner = width - 2
    route = short_route_name(departure)
    eta = eta_label(departure_minutes(departure))
    live = bool(departure.get("is_monitored"))
    scheduled = bool(departure.get("is_scheduled", True))
    vehicle_id = str(departure.get("vehicle_id") or "--")
    headsign_lines = wrap_headsign(departure.get("headsign", ""), inner, lines=2)

    rail_steps = 5
    progress_idx = progress_row_index(departure_minutes(departure), rail_steps)
    prev_stop = abbreviate_stop_id((vehicle or {}).get("previous_stop_id"), width=6) if vehicle else "ORIG"
    next_stop = abbreviate_stop_id((vehicle or {}).get("next_stop_id"), width=6) if vehicle else abbreviate_stop_id(stop_id, width=6)
    status = "LIVE" if live else "SCHD"
    updated = format_updated_label((vehicle or {}).get("last_updated"))

    lines = ["+" + "-" * inner + "+"]
    lines.append(fit_card_line(f"[{route}] {eta:>{inner - len(route) - 4}}", width))
    lines.append(fit_card_line(headsign_lines[0], width))
    lines.append(fit_card_line(headsign_lines[1], width))
    lines.append(fit_card_line(f"{status:<4} veh {vehicle_id:<10}", width))

    for step in range(rail_steps):
        marker = "|"
        if step == 0:
            marker = "^"
        if step == rail_steps - 1:
            marker = "@"
        if step == progress_idx:
            marker = "o" if live else "."

        if step == 0:
            left = f"{prev_stop:<6}"
            right = "origin"
        elif step == rail_steps - 1:
            left = f"{abbreviate_stop_id(stop_id, 6):<6}"
            right = "board"
        elif step == progress_idx:
            left = " " * 6
            right = f"next {next_stop}"
        else:
            left = " " * 6
            right = ""

        content = f"{left} {marker} {right}"
        lines.append(fit_card_line(content, width))

    lines.append(fit_card_line(updated, width))
    lines.append("+" + "-" * inner + "+")
    return lines[:height]


def combine_cards(cards: list[list[str]], gap: int = CARD_GAP) -> list[str]:
    if not cards:
        return []

    height = len(cards[0])
    spacer = " " * gap
    lines = []
    for row in range(height):
        parts = []
        for card in cards:
            parts.append(card[row])
        lines.append(spacer.join(parts))
    return lines


def build_display_lines(stop_name: str, stop_id: str, departures: list, vehicles: dict, now: datetime.datetime | None = None) -> list[str]:
    now = now or datetime.datetime.now()
    featured = select_featured_departures(departures, limit=MAX_FEATURED)
    header = f"MTD LIVE  {stop_name.upper()[:58]:<58}{now.strftime('%H:%M')}"
    separator = "=" * 92

    cards = []
    for departure in featured:
        vehicle_id = str(departure.get("vehicle_id") or "")
        vehicle = vehicles.get(vehicle_id) if vehicle_id else None
        cards.append(build_departure_card(departure, vehicle, stop_id))

    while len(cards) < MAX_FEATURED:
        empty = build_departure_card(
            {
                "expected_mins": 999,
                "headsign": "No live departure",
                "is_monitored": False,
                "is_scheduled": False,
                "vehicle_id": "",
                "route": {"route_short_name": "--"},
            },
            None,
            stop_id,
        )
        cards.append(empty)

    lines = [header[:92], separator]
    lines.extend(combine_cards(cards))
    lines.append("-" * 92)
    lines.append("Data: CUMTD GetDeparturesByStop + GetVehicle  |  Poll no more than once per minute")
    return lines


def fetch_top_vehicle_context(api_key: str, departures: list, cache: dict | None = None) -> dict:
    cache = cache if cache is not None else {}
    now_ts = time.time()
    vehicles = {}

    for departure in select_featured_departures(departures, limit=MAX_FEATURED):
        if not departure.get("is_monitored"):
            continue
        vehicle_id = str(departure.get("vehicle_id") or "")
        if not vehicle_id:
            continue

        cached = cache.get(vehicle_id)
        if cached and (now_ts - cached[0] < VEHICLE_CACHE_TTL_SECONDS):
            vehicles[vehicle_id] = cached[1]
            continue

        try:
            vehicle = extract_vehicle(fetch_vehicle(api_key, vehicle_id))
        except Exception:
            vehicle = None

        if vehicle is not None:
            cache[vehicle_id] = (now_ts, vehicle)
            vehicles[vehicle_id] = vehicle

    return vehicles


def ensure_layout_fits(canvas: EinkCanvas, lines: list[str], font: int, line_spacing: int) -> None:
    max_cols = canvas.chars_per_row(font)
    max_rows = canvas.rows_available(font)
    widest = max((len(line) for line in lines), default=0)

    if widest > max_cols:
        raise RuntimeError(
            f"Layout too wide for font {font}: needs {widest} cols, panel supports {max_cols}"
        )
    if len(lines) > max_rows:
        raise RuntimeError(
            f"Layout too tall for font {font}: needs {len(lines)} rows, panel supports {max_rows}"
        )
    if line_spacing <= 0:
        raise RuntimeError("line_spacing must be positive")


def render_lines_to_canvas(canvas: EinkCanvas, lines: list[str], font: int, line_spacing: int) -> None:
    ensure_layout_fits(canvas, lines, font=font, line_spacing=line_spacing)

    if not canvas.clear():
        raise RuntimeError("Failed to clear display buffer")
    if not canvas.draw_multiline(lines, x_start=X_START, y_start=Y_START, font=font, line_spacing=line_spacing):
        raise RuntimeError("Failed while staging text to display buffer")
    if not canvas.flush(full_refresh=True):
        raise RuntimeError("Failed to flush display buffer")


def render_once(canvas: EinkCanvas, api_key: str, stop_id: str, vehicle_cache: dict):
    raw = fetch_departures(api_key, stop_id)
    stop_name, departures = extract_stop_and_departures(raw, stop_id)
    vehicles = fetch_top_vehicle_context(api_key, departures, cache=vehicle_cache)
    lines = build_display_lines(stop_name, stop_id, departures, vehicles)

    for line in lines:
        print(f"  {line}")

    render_lines_to_canvas(canvas, lines, font=FONT, line_spacing=LINE_SPACING)


def build_test_lines(cycle: int = 0) -> list[str]:
    """Generate a static bus-tracker layout for stress testing (no API)."""
    now = datetime.datetime.now()
    fake_departures = [
        {"expected_mins": 2, "headsign": "Green West", "is_monitored": True,
         "is_scheduled": True, "vehicle_id": "1724",
         "route": {"route_short_name": "50"}},
        {"expected_mins": 5, "headsign": "Teal Orchard Downs", "is_monitored": True,
         "is_scheduled": True, "vehicle_id": "1721",
         "route": {"route_short_name": "120"}},
        {"expected_mins": 8, "headsign": "Illini Limited", "is_monitored": True,
         "is_scheduled": True, "vehicle_id": "2022",
         "route": {"route_short_name": "220"}},
    ]
    fake_vehicles = {
        "1724": {"previous_stop_id": "RACEMU", "next_stop_id": "GWNMN",
                 "last_updated": now.isoformat()},
        "1721": {"previous_stop_id": "PAMD", "next_stop_id": "GWNNV",
                 "last_updated": now.isoformat()},
        "2022": {"previous_stop_id": "GRGIKE", "next_stop_id": "GWNMN",
                 "last_updated": now.isoformat()},
    }
    return build_display_lines(f"IU (test #{cycle})", "IU", fake_departures, fake_vehicles, now)


def main():
    parser = argparse.ArgumentParser(description="Graphic MTD bus tracker for e-ink display")
    parser.add_argument('--port', required=True, help='Serial port')
    parser.add_argument('--dst', required=True, type=lambda x: int(x, 0),
                        help='Destination RF address (e.g. 0x20)')
    parser.add_argument('--stop', default='IU',
                        help='MTD stop ID (default: IU = Illinois Terminal)')
    parser.add_argument('--api-key', default=None,
                        help='CUMTD API key (or set MTD_API_KEY env var)')
    parser.add_argument('--chunk-size', type=int, default=24,
                        help='Max chars per RF draw call (default: 24, max for RF)')
    parser.add_argument('--loop-minutes', type=int, default=0,
                        help='Refresh cadence in minutes (0 = render once, minimum 1)')
    parser.add_argument('--test', type=int, default=0, metavar='N',
                        help='Stress test: render static data N times (no API needed)')
    parser.add_argument('--test-delay', type=int, default=10,
                        help='Seconds between stress test cycles (default: 10)')
    args = parser.parse_args()

    # ── Stress-test mode ──────────────────────────────────────────
    if args.test > 0:
        with EinkCanvas(args.port, args.dst, chunk_size=args.chunk_size) as canvas:
            passed = 0
            failed = 0
            for cycle in range(1, args.test + 1):
                print(f"\n=== Stress test cycle {cycle}/{args.test} ===")
                lines = build_test_lines(cycle)
                for line in lines:
                    print(f"  {line}")
                try:
                    render_lines_to_canvas(canvas, lines, font=FONT, line_spacing=LINE_SPACING)
                    passed += 1
                    print(f"  PASS (cycle {cycle})")
                except Exception as exc:
                    failed += 1
                    print(f"  FAIL (cycle {cycle}): {exc}")
                if cycle < args.test:
                    print(f"  Waiting {args.test_delay}s...")
                    time.sleep(args.test_delay)
            print(f"\n=== Results: {passed} passed, {failed} failed out of {args.test} ===")
        return

    # ── Normal API mode ───────────────────────────────────────────
    api_key = normalize_api_key(args.api_key) or normalize_api_key(os.environ.get('MTD_API_KEY'))
    if not api_key:
        print("Error: provide --api-key or set MTD_API_KEY environment variable")
        print("Get a free key at https://developer.cumtd.com/")
        sys.exit(1)

    if args.loop_minutes < 0:
        print("--loop-minutes must be 0 or greater")
        sys.exit(1)
    if args.loop_minutes == 0:
        sleep_seconds = 0
    else:
        sleep_seconds = max(60, args.loop_minutes * 60)

    vehicle_cache = {}

    with EinkCanvas(args.port, args.dst, chunk_size=args.chunk_size) as canvas:
        try:
            print(f"Fetching departures for stop '{args.stop}'...")
            render_once(canvas, api_key, args.stop, vehicle_cache)

            if sleep_seconds > 0:
                print(f"Refreshing every {sleep_seconds // 60} minute(s). Ctrl+C to stop.")
                while True:
                    time.sleep(sleep_seconds)
                    print(f"Refreshing departures for stop '{args.stop}'...")
                    render_once(canvas, api_key, args.stop, vehicle_cache)
        except KeyboardInterrupt:
            print("\nStopped.")
        except Exception as exc:
            print(f"API/render error: {exc}")
            fallback = [
                "MTD LIVE",
                "------------------------------",
                f"Stop: {args.stop}",
                "",
                "Transit data unavailable",
                str(exc)[:36],
                "",
                "Check API key / rate limit / stop id",
            ]
            try:
                render_lines_to_canvas(canvas, fallback, font=FONT, line_spacing=LINE_SPACING)
            except Exception as fallback_exc:
                print(f"Fallback render error: {fallback_exc}")
            sys.exit(1)

    print("Done.")


if __name__ == '__main__':
    main()
