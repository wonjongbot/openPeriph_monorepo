#!/usr/bin/env python3

import datetime
import os
import sys
import unittest
from urllib.error import HTTPError

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(REPO_ROOT, 'scripts'))

import bus_tracker


class FakeCanvas:
    def __init__(self, chars_per_row: int, rows_available: int, draw_ok: bool = True, flush_ok: bool = True):
        self._chars_per_row = chars_per_row
        self._rows_available = rows_available
        self._draw_ok = draw_ok
        self._flush_ok = flush_ok
        self.clear_calls = 0
        self.draw_calls = 0
        self.flush_calls = 0

    def chars_per_row(self, font: int = 16) -> int:
        return self._chars_per_row

    def rows_available(self, font: int = 16) -> int:
        return self._rows_available

    def clear(self) -> bool:
        self.clear_calls += 1
        return True

    def draw_multiline(self, lines, x_start=0, y_start=0, font=16, line_spacing=None) -> bool:
        self.draw_calls += 1
        return self._draw_ok

    def flush(self, full_refresh: bool = True) -> bool:
        self.flush_calls += 1
        return self._flush_ok


class BusTrackerTests(unittest.TestCase):
    def test_extract_departures_supports_rsp_wrapper(self):
        data = {
            "rsp": {
                "stops": [{"stop_id": "IU", "stop_name": "Illinois Terminal"}],
                "departures": [
                    {"expected_mins": 7, "headsign": "Airport", "route": {"route_short_name": "100"}},
                ],
            }
        }

        stop_name, departures = bus_tracker.extract_stop_and_departures(data, "IU")

        self.assertEqual(stop_name, "Illinois Terminal")
        self.assertEqual(len(departures), 1)
        self.assertEqual(departures[0]["expected_mins"], 7)

    def test_select_featured_departures_prefers_live_then_eta(self):
        departures = [
            {"expected_mins": 12, "is_monitored": True, "headsign": "A", "route": {"route_short_name": "1"}},
            {"expected_mins": 3, "is_monitored": False, "headsign": "B", "route": {"route_short_name": "2"}},
            {"expected_mins": 5, "is_monitored": True, "headsign": "C", "route": {"route_short_name": "3"}},
            {"expected_mins": 1, "is_monitored": True, "headsign": "D", "route": {"route_short_name": "4"}},
        ]

        featured = bus_tracker.select_featured_departures(departures, limit=3)

        self.assertEqual([item["route"]["route_short_name"] for item in featured], ["4", "3", "1"])

    def test_build_departure_card_renders_route_eta_and_progress_marker(self):
        departure = {
            "expected_mins": 4,
            "headsign": "Downtown via Green",
            "is_monitored": True,
            "is_scheduled": True,
            "vehicle_id": "1952",
            "route": {"route_short_name": "22"},
        }
        vehicle = {
            "previous_stop_id": "WRIGHT:3",
            "next_stop_id": "IU",
            "last_updated": "2026-04-17T18:58:00-05:00",
        }

        card = bus_tracker.build_departure_card(departure, vehicle, "IU", width=28, height=12)

        self.assertEqual(len(card), 12)
        self.assertTrue(all(len(line) == 28 for line in card))
        joined = "\n".join(card)
        self.assertIn("22", joined)
        self.assertIn("4m", joined)
        self.assertIn("o", joined)
        self.assertIn("IU", joined)

    def test_build_departure_card_degrades_when_no_vehicle_context(self):
        departure = {
            "expected_mins": 11,
            "headsign": "Campus Loop",
            "is_monitored": False,
            "is_scheduled": True,
            "vehicle_id": "",
            "route": {"route_short_name": "10"},
        }

        card = bus_tracker.build_departure_card(departure, None, "ISR", width=28, height=12)

        joined = "\n".join(card)
        self.assertIn("SCHD", joined)
        self.assertIn("10", joined)
        self.assertIn("ISR", joined)

    def test_build_display_lines_combines_three_cards(self):
        now = datetime.datetime(2026, 4, 17, 19, 5)
        departures = [
            {"expected_mins": 1, "headsign": "A", "is_monitored": True, "is_scheduled": True, "vehicle_id": "1001", "route": {"route_short_name": "1"}},
            {"expected_mins": 4, "headsign": "B", "is_monitored": True, "is_scheduled": True, "vehicle_id": "1002", "route": {"route_short_name": "2"}},
            {"expected_mins": 9, "headsign": "C", "is_monitored": False, "is_scheduled": True, "vehicle_id": "", "route": {"route_short_name": "3"}},
        ]
        vehicles = {
            "1001": {"previous_stop_id": "A:1", "next_stop_id": "IU", "last_updated": "2026-04-17T19:04:00-05:00"},
            "1002": {"previous_stop_id": "B:1", "next_stop_id": "IU", "last_updated": "2026-04-17T19:03:00-05:00"},
        }

        lines = bus_tracker.build_display_lines("Illinois Terminal", "IU", departures, vehicles, now=now)

        self.assertGreater(len(lines), 10)
        self.assertIn("ILLINOIS TERMINAL", lines[0])
        self.assertTrue(any("1" in line and "2" in line and "3" in line for line in lines))

    def test_ensure_layout_fits_rejects_font_16_for_card_layout(self):
        now = datetime.datetime(2026, 4, 17, 19, 5)
        departures = [
            {"expected_mins": 1, "headsign": "A", "is_monitored": True, "is_scheduled": True, "vehicle_id": "1001", "route": {"route_short_name": "1"}},
            {"expected_mins": 4, "headsign": "B", "is_monitored": True, "is_scheduled": True, "vehicle_id": "1002", "route": {"route_short_name": "2"}},
            {"expected_mins": 9, "headsign": "C", "is_monitored": False, "is_scheduled": True, "vehicle_id": "", "route": {"route_short_name": "3"}},
        ]
        vehicles = {
            "1001": {"previous_stop_id": "A:1", "next_stop_id": "IU", "last_updated": "2026-04-17T19:04:00-05:00"},
            "1002": {"previous_stop_id": "B:1", "next_stop_id": "IU", "last_updated": "2026-04-17T19:03:00-05:00"},
        }
        lines = bus_tracker.build_display_lines("Illinois Terminal", "IU", departures, vehicles, now=now)
        canvas = FakeCanvas(chars_per_row=58, rows_available=30)

        with self.assertRaises(RuntimeError):
            bus_tracker.ensure_layout_fits(canvas, lines, font=16, line_spacing=16)

    def test_render_lines_to_canvas_does_not_flush_after_draw_failure(self):
        lines = ["hello", "world"]
        canvas = FakeCanvas(chars_per_row=92, rows_available=40, draw_ok=False)

        with self.assertRaises(RuntimeError):
            bus_tracker.render_lines_to_canvas(canvas, lines, font=12, line_spacing=12)

        self.assertEqual(canvas.clear_calls, 1)
        self.assertEqual(canvas.draw_calls, 1)
        self.assertEqual(canvas.flush_calls, 0)

    def test_normalize_api_key_strips_whitespace(self):
        self.assertEqual(bus_tracker.normalize_api_key("  abc123 \n"), "abc123")
        self.assertIsNone(bus_tracker.normalize_api_key("   "))
        self.assertIsNone(bus_tracker.normalize_api_key(None))

    def test_describe_http_error_prefers_api_message(self):
        class FakeResponse:
            def read(self):
                return b'{"rsp":{"stat":{"code":401,"msg":"Invalid key provided"}}}'

            def close(self):
                return None

        err = HTTPError("https://developer.mtd.org/", 401, "Unauthorized", hdrs=None, fp=FakeResponse())

        message = bus_tracker.describe_http_error(err)

        self.assertIn("HTTP 401", message)
        self.assertIn("Invalid key provided", message)


if __name__ == '__main__':
    unittest.main()
