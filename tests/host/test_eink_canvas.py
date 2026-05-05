#!/usr/bin/env python3
"""
Unit tests for the eink_canvas retry / pacing logic.

These tests mock out the serial port and send_data protocol functions so
they run entirely on the host — no hardware needed.
"""

import os
import sys
import time
import unittest
from unittest.mock import MagicMock, patch, call

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(REPO_ROOT, 'scripts'))

import send_data
import eink_canvas
from eink_canvas import EinkCanvas


def _make_canvas(chunk_size=24, send_begin_ok=True, send_text_ok=True,
                 send_commit_ok=True, send_flush_ok=True, send_tilemap_ok=True):
    """Build an EinkCanvas with a mocked serial port and controllable stubs."""
    with patch('eink_canvas.send_data') as mock_sd, \
         patch('serial.Serial'):
        mock_sd.RF_DRAW_TEXT_MAX_LEN = 24
        mock_sd.next_draw_session_id.return_value = 0x01
        mock_sd.read_response.return_value = {}
        mock_sd.send_draw_begin.return_value = send_begin_ok
        mock_sd.send_draw_text.return_value = send_text_ok
        mock_sd.send_draw_commit.return_value = send_commit_ok
        mock_sd.send_display_flush.return_value = send_flush_ok
        mock_sd.send_draw_tilemap.return_value = send_tilemap_ok
        mock_sd.encode_draw_begin_payload.return_value = b'\x00'
        mock_sd.encode_draw_text_payload.return_value = b'\x00'
        mock_sd.encode_draw_tilemap_payload.return_value = b'\x00'
        mock_sd.encode_draw_commit_payload.return_value = b'\x00'
        mock_sd.encode_flush_payload.return_value = b'\x00'
        mock_sd.RF_DRAW_TILEMAP_MAX_BYTES = 44

        canvas = EinkCanvas.__new__(EinkCanvas)
        canvas.dst = 0x20
        canvas.chunk_size = chunk_size
        canvas._ser = MagicMock()
        canvas._port = '/dev/null'
        canvas._baud = 115200
        canvas._session_id = None
        canvas._clear_first = False
        canvas._ops = []

    return canvas, mock_sd


class TestSendWithRetry(unittest.TestCase):
    """Test _send_with_retry: in-place retry with cooldown, no session restart."""

    def test_succeeds_on_first_attempt(self):
        canvas, _ = _make_canvas()
        fn = MagicMock(return_value=True)

        result = canvas._send_with_retry(fn)

        self.assertTrue(result)
        self.assertEqual(fn.call_count, 1)

    def test_retries_on_failure_then_succeeds(self):
        canvas, _ = _make_canvas()
        fn = MagicMock(side_effect=[False, False, True])

        # Use a short cooldown for test speed
        with patch.object(eink_canvas, 'RF_RETRY_COOLDOWN_S', 0.001):
            result = canvas._send_with_retry(fn)

        self.assertTrue(result)
        self.assertEqual(fn.call_count, 3)  # 1 initial + 2 retries

    def test_gives_up_after_max_retries(self):
        canvas, _ = _make_canvas()
        fn = MagicMock(return_value=False)

        with patch.object(eink_canvas, 'RF_RETRY_COOLDOWN_S', 0.001), \
             patch.object(eink_canvas, 'RF_OP_RETRIES', 2):
            result = canvas._send_with_retry(fn)

        self.assertFalse(result)
        self.assertEqual(fn.call_count, 3)  # 1 initial + 2 retries

    def test_retry_applies_cooldown_between_attempts(self):
        canvas, _ = _make_canvas()
        timestamps = []
        results = iter([False, True])

        def timed_fn():
            timestamps.append(time.monotonic())
            return next(results)

        cooldown = 0.05
        with patch.object(eink_canvas, 'RF_RETRY_COOLDOWN_S', cooldown):
            canvas._send_with_retry(timed_fn)

        self.assertEqual(len(timestamps), 2)
        elapsed = timestamps[1] - timestamps[0]
        self.assertGreaterEqual(elapsed, cooldown * 0.8)


class TestDrawTextInPlaceRetry(unittest.TestCase):
    """draw_text retries the exact failed chunk, not the whole session."""

    @patch('eink_canvas.send_data')
    @patch('serial.Serial')
    def test_draw_text_retries_failed_chunk_in_place(self, mock_serial, mock_sd):
        """When a chunk fails, retry that chunk — don't restart from op 0."""
        mock_sd.RF_DRAW_TEXT_MAX_LEN = 24
        mock_sd.next_draw_session_id.return_value = 0x01
        mock_sd.read_response.return_value = {}
        mock_sd.send_draw_begin.return_value = True
        mock_sd.encode_draw_begin_payload.return_value = b'\x00'
        mock_sd.encode_draw_text_payload.return_value = b'\x00'

        canvas = EinkCanvas.__new__(EinkCanvas)
        canvas.dst = 0x20
        canvas.chunk_size = 10
        canvas._ser = MagicMock()
        canvas._port = '/dev/null'
        canvas._baud = 115200
        canvas._session_id = None
        canvas._clear_first = False
        canvas._ops = []

        # Chunk 0 succeeds, chunk 1 fails once then succeeds, chunk 2 succeeds
        mock_sd.send_draw_text.side_effect = [True, False, True, True]

        with patch.object(eink_canvas, 'RF_INTER_OP_DELAY_S', 0), \
             patch.object(eink_canvas, 'RF_RETRY_COOLDOWN_S', 0.001):
            result = canvas.draw_text("ABCDEFGHIJ1234567890XY", x=0, y=0, font=12)

        self.assertTrue(result)
        # 4 send_draw_text calls: op0 ok, op1 fail, op1 retry ok, op2 ok
        self.assertEqual(mock_sd.send_draw_text.call_count, 4)
        # Session should NOT have been restarted (only 1 begin call)
        self.assertEqual(mock_sd.send_draw_begin.call_count, 1)

    @patch('eink_canvas.send_data')
    @patch('serial.Serial')
    def test_draw_text_gives_up_on_persistent_failure(self, mock_serial, mock_sd):
        """If retries exhausted on a chunk, return False without restarting."""
        mock_sd.RF_DRAW_TEXT_MAX_LEN = 24
        mock_sd.next_draw_session_id.return_value = 0x01
        mock_sd.read_response.return_value = {}
        mock_sd.send_draw_begin.return_value = True
        mock_sd.encode_draw_begin_payload.return_value = b'\x00'
        mock_sd.encode_draw_text_payload.return_value = b'\x00'

        canvas = EinkCanvas.__new__(EinkCanvas)
        canvas.dst = 0x20
        canvas.chunk_size = 10
        canvas._ser = MagicMock()
        canvas._port = '/dev/null'
        canvas._baud = 115200
        canvas._session_id = None
        canvas._clear_first = False
        canvas._ops = []

        # Chunk 0 succeeds, chunk 1 always fails
        mock_sd.send_draw_text.side_effect = [True] + [False] * 10

        with patch.object(eink_canvas, 'RF_INTER_OP_DELAY_S', 0), \
             patch.object(eink_canvas, 'RF_RETRY_COOLDOWN_S', 0.001), \
             patch.object(eink_canvas, 'RF_OP_RETRIES', 2):
            result = canvas.draw_text("ABCDEFGHIJ1234567890", x=0, y=0, font=12)

        self.assertFalse(result)
        # 1 success + 1 initial fail + 2 retries = 4 total
        self.assertEqual(mock_sd.send_draw_text.call_count, 4)
        # Session never restarted
        self.assertEqual(mock_sd.send_draw_begin.call_count, 1)


class TestFlushInPlaceRetry(unittest.TestCase):
    """flush() retries commit/flush in-place instead of replaying."""

    @patch('eink_canvas.send_data')
    @patch('serial.Serial')
    def test_flush_retries_commit_in_place(self, mock_serial, mock_sd):
        mock_sd.RF_DRAW_TEXT_MAX_LEN = 24
        mock_sd.next_draw_session_id.return_value = 0x01
        mock_sd.read_response.return_value = {}
        mock_sd.send_draw_begin.return_value = True
        mock_sd.encode_draw_begin_payload.return_value = b'\x00'
        mock_sd.encode_draw_commit_payload.return_value = b'\x00'
        mock_sd.encode_flush_payload.return_value = b'\x00'
        # Commit fails once, then succeeds; flush succeeds first try
        mock_sd.send_draw_commit.side_effect = [False, True]
        mock_sd.send_display_flush.return_value = True

        canvas = EinkCanvas.__new__(EinkCanvas)
        canvas.dst = 0x20
        canvas.chunk_size = 24
        canvas._ser = MagicMock()
        canvas._port = '/dev/null'
        canvas._baud = 115200
        canvas._session_id = 0x01
        canvas._clear_first = False
        canvas._ops = [('x', 0, 12, 'test')]

        with patch.object(eink_canvas, 'RF_INTER_OP_DELAY_S', 0), \
             patch.object(eink_canvas, 'RF_RETRY_COOLDOWN_S', 0.001):
            result = canvas.flush(full_refresh=True)

        self.assertTrue(result)
        self.assertEqual(mock_sd.send_draw_commit.call_count, 2)
        # No session replay — begin never called again
        self.assertEqual(mock_sd.send_draw_begin.call_count, 0)


class TestTilemapDrawing(unittest.TestCase):
    @patch('eink_canvas.send_data')
    @patch('serial.Serial')
    def test_draw_tilemap_chunks_packed_payload(self, mock_serial, mock_sd):
        mock_sd.RF_DRAW_TEXT_MAX_LEN = 24
        mock_sd.RF_DRAW_TILEMAP_MAX_BYTES = 4
        mock_sd.next_draw_session_id.return_value = 0x01
        mock_sd.read_response.return_value = {}
        mock_sd.send_draw_begin.return_value = True
        mock_sd.send_draw_tilemap.return_value = True
        mock_sd.encode_draw_begin_payload.return_value = b'\x00'
        mock_sd.encode_draw_tilemap_payload.return_value = b'\x00'

        canvas = EinkCanvas.__new__(EinkCanvas)
        canvas.dst = 0x20
        canvas.chunk_size = 24
        canvas._ser = MagicMock()
        canvas._port = '/dev/null'
        canvas._baud = 115200
        canvas._session_id = None
        canvas._clear_first = False
        canvas._ops = []
        canvas._next_text_op_index = 0

        with patch.object(eink_canvas, 'RF_INTER_OP_DELAY_S', 0):
            result = canvas.draw_tilemap(b'\x01\x23\x45\x67\x89\xAB', clear_first=True)

        self.assertTrue(result)
        self.assertEqual(mock_sd.send_draw_begin.call_count, 1)
        self.assertEqual(mock_sd.encode_draw_tilemap_payload.call_count, 2)
        self.assertEqual(mock_sd.send_draw_tilemap.call_count, 2)

    @patch('eink_canvas.send_data')
    @patch('serial.Serial')
    def test_flush_retries_display_flush_in_place(self, mock_serial, mock_sd):
        mock_sd.RF_DRAW_TEXT_MAX_LEN = 24
        mock_sd.next_draw_session_id.return_value = 0x01
        mock_sd.read_response.return_value = {}
        mock_sd.send_draw_begin.return_value = True
        mock_sd.encode_draw_begin_payload.return_value = b'\x00'
        mock_sd.encode_draw_commit_payload.return_value = b'\x00'
        mock_sd.encode_flush_payload.return_value = b'\x00'
        mock_sd.send_draw_commit.return_value = True
        # display_flush fails once, then succeeds
        mock_sd.send_display_flush.side_effect = [False, True]

        canvas = EinkCanvas.__new__(EinkCanvas)
        canvas.dst = 0x20
        canvas.chunk_size = 24
        canvas._ser = MagicMock()
        canvas._port = '/dev/null'
        canvas._baud = 115200
        canvas._session_id = 0x01
        canvas._clear_first = False
        canvas._ops = [('x', 0, 12, 'test')]

        with patch.object(eink_canvas, 'RF_INTER_OP_DELAY_S', 0), \
             patch.object(eink_canvas, 'RF_RETRY_COOLDOWN_S', 0.001):
            result = canvas.flush(full_refresh=True)

        self.assertTrue(result)
        self.assertEqual(mock_sd.send_display_flush.call_count, 2)
        self.assertEqual(mock_sd.send_draw_begin.call_count, 0)


class TestNoSessionRestart(unittest.TestCase):
    """Verify the old replay-from-scratch behavior is gone."""

    def test_no_replay_session_method(self):
        """_replay_session should no longer exist on EinkCanvas."""
        self.assertFalse(hasattr(EinkCanvas, '_replay_session'))


if __name__ == '__main__':
    unittest.main()
