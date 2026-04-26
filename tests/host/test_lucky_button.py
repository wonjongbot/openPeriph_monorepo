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

    def test_handle_packet_returns_false_when_canvas_clear_fails(self):
        payload = bytes([0x22, 0x09, 0x01, 0x34, 0x12])
        packet = {
            'valid': True,
            'type': send_data.PKT_TYPE_AGENT_EVENT,
            'payload': payload,
        }
        canvas = MagicMock()
        canvas.clear.return_value = False

        handled = lucky_button.handle_packet(packet, canvas, mode='fortune', agent='none')

        self.assertFalse(handled)
        canvas.clear.assert_called_once()
        canvas.draw_multiline.assert_not_called()
        canvas.flush.assert_not_called()

    def test_handle_packet_returns_false_when_canvas_draw_fails(self):
        payload = bytes([0x22, 0x09, 0x01, 0x34, 0x12])
        packet = {
            'valid': True,
            'type': send_data.PKT_TYPE_AGENT_EVENT,
            'payload': payload,
        }
        canvas = MagicMock()
        canvas.clear.return_value = True
        canvas.draw_multiline.return_value = False

        handled = lucky_button.handle_packet(packet, canvas, mode='fortune', agent='none')

        self.assertFalse(handled)
        canvas.clear.assert_called_once()
        canvas.draw_multiline.assert_called_once()
        canvas.flush.assert_not_called()

    def test_handle_packet_returns_false_when_canvas_flush_fails(self):
        payload = bytes([0x22, 0x09, 0x01, 0x34, 0x12])
        packet = {
            'valid': True,
            'type': send_data.PKT_TYPE_AGENT_EVENT,
            'payload': payload,
        }
        canvas = MagicMock()
        canvas.clear.return_value = True
        canvas.draw_multiline.return_value = True
        canvas.flush.return_value = False

        handled = lucky_button.handle_packet(packet, canvas, mode='fortune', agent='none')

        self.assertFalse(handled)
        canvas.clear.assert_called_once()
        canvas.draw_multiline.assert_called_once()
        canvas.flush.assert_called_once_with(full_refresh=True)

    def test_run_once_reads_from_canvas_serial(self):
        packet = {
            'valid': True,
            'type': send_data.PKT_TYPE_AGENT_EVENT,
            'payload': bytes([0x22, 0x01, 0x01, 0x00, 0x00]),
        }
        canvas = MagicMock()
        canvas._ser = object()

        with patch('lucky_button.EinkCanvas') as canvas_cls:
            canvas_cls.return_value.__enter__.return_value = canvas
            with patch('lucky_button.send_data.read_response', return_value=packet) as read_response:
                lucky_button.run_once('/dev/fake', 0x22, mode='fortune', agent='none')

        canvas_cls.assert_called_once_with(port='/dev/fake', dst=0x22, baud=115200)
        read_response.assert_called_once_with(canvas._ser, timeout=1.0)
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

    def test_repo_summary_reports_git_status_failure(self):
        status = MagicMock(returncode=1, stdout='')

        with patch('lucky_button.subprocess.run', return_value=status):
            lines = lucky_button._repo_summary()

        self.assertEqual(lines, ["Repo pulse", "git status failed"])


if __name__ == '__main__':
    unittest.main()
