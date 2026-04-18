#!/usr/bin/env python3
"""
weather_widget.py — Current weather on the RF e-ink display.

No API key required — uses wttr.in's free JSON endpoint.
Great for a desk display showing conditions in Urbana-Champaign.

Usage:
    python3 scripts/weather_widget.py --port /dev/tty.usbmodem... --dst 0x20
    python3 scripts/weather_widget.py --port /dev/tty.usbmodem... --dst 0x20 \\
        --location "Chicago,IL"

Display layout (Font12, 92 chars/row):
    Line 0: header bar
    Line 1: location + update time
    Line 2: separator
    Line 3: condition description
    Line 4: temperature + feels like
    Line 5: humidity + wind
    Line 6: separator
    Line 7: 3-day forecast summary
"""

import argparse
import sys
import os
import json
import datetime

sys.path.insert(0, os.path.dirname(__file__))
from eink_canvas import EinkCanvas

FONT = 12
LINE_SPACING = 14
X_START = 4
Y_START = 4

WIND_DIRS = ['N','NNE','NE','ENE','E','ESE','SE','SSE',
             'S','SSW','SW','WSW','W','WNW','NW','NNW']

CONDITION_MAP = {
    113: "Sunny",           116: "Partly cloudy",
    119: "Cloudy",          122: "Overcast",
    143: "Mist",            176: "Patchy rain",
    179: "Patchy snow",     182: "Patchy sleet",
    185: "Patchy freezing", 200: "Thundery outbreaks",
    227: "Blowing snow",    230: "Blizzard",
    248: "Fog",             260: "Freezing fog",
    263: "Light drizzle",   266: "Light drizzle",
    281: "Freezing drizzle",284: "Heavy freezing",
    293: "Light rain",      296: "Light rain",
    299: "Moderate rain",   302: "Moderate rain",
    305: "Heavy rain",      308: "Heavy rain",
    311: "Light sleet",     314: "Moderate sleet",
    317: "Light snow",      320: "Moderate snow",
    323: "Patchy snow",     326: "Patchy snow",
    329: "Patchy heavy snow",332: "Moderate snow",
    335: "Patchy heavy snow",338: "Heavy snow",
    350: "Ice pellets",     353: "Light shower",
    356: "Moderate shower", 359: "Torrential rain",
    362: "Light sleet shower",365: "Moderate sleet",
    368: "Light snow shower",371: "Moderate snow",
    374: "Light ice pellets",377: "Moderate ice",
    386: "Patchy light rain with thunder",
    389: "Moderate rain with thunder",
    392: "Patchy light snow with thunder",
    395: "Moderate snow with thunder",
}


def c_to_f(c: float) -> float:
    return c * 9.0 / 5.0 + 32.0


def wind_dir(degrees: int) -> str:
    idx = int((degrees + 11.25) / 22.5) % 16
    return WIND_DIRS[idx]


def fetch_weather(location: str) -> dict:
    import urllib.request
    import urllib.parse
    url = f"https://wttr.in/{urllib.parse.quote(location)}?format=j1"
    with urllib.request.urlopen(url, timeout=8) as resp:
        return json.loads(resp.read().decode())


def parse_weather(data: dict, location: str) -> list:
    now_str = datetime.datetime.now().strftime("%H:%M")

    try:
        current = data["current_condition"][0]
        temp_c = float(current["temp_C"])
        feels_c = float(current["FeelsLikeC"])
        humidity = int(current["humidity"])
        wind_speed = int(current["windspeedKmph"])
        wind_deg = int(current["winddirDegree"])
        code = int(current["weatherCode"])
        condition = CONDITION_MAP.get(code, current.get("weatherDesc", [{}])[0].get("value", "Unknown"))

        temp_f = c_to_f(temp_c)
        feels_f = c_to_f(feels_c)
        wdir = wind_dir(wind_deg)

        lines = [
            "== WEATHER ===================",
            f"  {location[:20]}  {now_str}",
            "------------------------------",
            f"  {condition}",
            f"  {temp_f:.0f}F  (feels {feels_f:.0f}F)",
            f"  Humidity: {humidity}%  Wind: {wind_speed}kph {wdir}",
            "------------------------------",
        ]

        weather_days = data.get("weather", [])[:3]
        for day in weather_days:
            date = day.get("date", "")
            max_f = c_to_f(float(day.get("maxtempC", 0)))
            min_f = c_to_f(float(day.get("mintempC", 0)))
            hourly = day.get("hourly", [{}])
            day_code = int(hourly[len(hourly)//2].get("weatherCode", 113))
            day_cond = CONDITION_MAP.get(day_code, "")[:12]
            lines.append(f"  {date}  {min_f:.0f}-{max_f:.0f}F  {day_cond}")

    except (KeyError, IndexError, ValueError) as exc:
        lines = [
            "== WEATHER ===================",
            f"  {location}",
            "------------------------------",
            "  Weather unavailable",
            f"  Error: {str(exc)[:28]}",
            "------------------------------",
        ]

    return lines


def main():
    parser = argparse.ArgumentParser(description="Weather widget for e-ink display")
    parser.add_argument('--port', required=True, help='Serial port')
    parser.add_argument('--dst', required=True, type=lambda x: int(x, 0),
                        help='Destination RF address (e.g. 0x20)')
    parser.add_argument('--location', default='Urbana,IL',
                        help='Location for weather (default: Urbana,IL)')
    parser.add_argument('--chunk-size', type=int, default=20,
                        help='Max chars per RF draw call')
    args = parser.parse_args()

    print(f"Fetching weather for '{args.location}'...")
    try:
        data = fetch_weather(args.location)
    except Exception as exc:
        print(f"Fetch failed: {exc}")
        data = {}

    lines = parse_weather(data, args.location)
    for line in lines:
        print(f"  {line}")

    with EinkCanvas(args.port, args.dst, chunk_size=args.chunk_size) as canvas:
        print("Clearing display...")
        canvas.clear()
        print("Staging weather widget...")
        canvas.draw_multiline(lines, x_start=X_START, y_start=Y_START,
                              font=FONT, line_spacing=LINE_SPACING)
        print("Flushing to display...")
        canvas.flush(full_refresh=True)

    print("Done.")


if __name__ == '__main__':
    main()
