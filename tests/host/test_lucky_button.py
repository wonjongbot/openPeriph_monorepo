#!/usr/bin/env python3

import os
import sys
import unittest
from unittest.mock import MagicMock, patch

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(REPO_ROOT, 'scripts'))

import lucky_button
import send_data


class LuckyButtonTests(unittest.TestCase):
    def test_sanitize_lines_bounds_ascii_width_and_count(self):
        raw = ["hello", "snowman \u2603", "x" * 80]

        lines = lucky_button.sanitize_lines(raw, max_lines=3, max_width=10)

        self.assertEqual(lines, ["hello", "snowman ?", "xxxxxxxxxx"])

    def test_sanitize_lines_empty_fallback_respects_max_lines(self):
        self.assertEqual(
            lucky_button.sanitize_lines([], max_lines=1, max_width=10),
            ["Lucky button"],
        )

    def test_generate_mode_lines_supports_approved_modes_without_agent(self):
        event = {
            'slave_addr': 0x22,
            'event_id': 7,
            'press_type': 1,
            'uptime_ms': 1234,
        }

        for mode in lucky_button.APPROVED_MODES:
            with self.subTest(mode=mode):
                lines = lucky_button.generate_mode_lines(mode, event, agent='none')
                self.assertGreaterEqual(len(lines), 2)
                self.assertTrue(all(isinstance(line, str) for line in lines))
                self.assertTrue(all(len(line) <= lucky_button.MAX_LINE_WIDTH for line in lines))

    def test_random_mode_chooses_approved_mode(self):
        event = {
            'slave_addr': 0x22,
            'event_id': 1,
            'press_type': 1,
            'uptime_ms': 1,
        }

        with patch('lucky_button.random.choice', return_value='fortune'):
            selected, lines = lucky_button.resolve_mode('random', event, agent='none')

        self.assertEqual(selected, 'fortune')
        self.assertGreaterEqual(len(lines), 2)

    def test_handle_packet_draws_agent_event(self):
        payload = bytes([0x22, 0x09, 0x01, 0x34, 0x12])
        packet = {
            'valid': True,
            'type': send_data.PKT_TYPE_AGENT_EVENT,
            'payload': payload,
        }
        canvas = MagicMock()

        handled = lucky_button.handle_packet(packet, canvas, mode='fortune', agent='none')

        self.assertTrue(handled)
        canvas.clear.assert_called_once()
        self.assertGreaterEqual(canvas.draw_multiline.call_count, 1)
        canvas.flush.assert_called_once_with(full_refresh=True)

    def test_handle_packet_ignores_non_agent_event(self):
        packet = {
            'valid': True,
            'type': send_data.PKT_TYPE_ACK,
            'payload': b'',
        }
        canvas = MagicMock()

        handled = lucky_button.handle_packet(packet, canvas, mode='fortune', agent='none')

        self.assertFalse(handled)
        canvas.clear.assert_not_called()


if __name__ == '__main__':
    unittest.main()
