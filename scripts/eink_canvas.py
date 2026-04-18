#!/usr/bin/env python3
"""
eink_canvas.py — High-level staged drawing API for the RF-connected e-ink display.

Wraps send_data.py protocol functions. All draw_text calls stage into the
framebuffer (defer mode). Call flush() once at the end to push everything
to the display in a single EPD refresh.

Usage:
    from eink_canvas import EinkCanvas

    canvas = EinkCanvas(port='/dev/tty.usbmodem...', dst=0x20)
    canvas.clear()
    canvas.draw_text("Hello!", x=10, y=10, font=16)
    canvas.draw_text("World", x=10, y=30, font=16)
    canvas.flush()

Display specs (5.83" EPD):
    Resolution : 648 x 480 pixels
    Font16     : 11 px wide, 16 px tall -> 58 chars/row, 30 rows
    Font12     :  7 px wide, 12 px tall -> 92 chars/row, 40 rows
"""

import sys
import time
import os

sys.path.insert(0, os.path.dirname(__file__))

import send_data

DISPLAY_WIDTH_PX  = 648
DISPLAY_HEIGHT_PX = 480

FONT_WIDTHS  = {12: 7, 16: 11}
FONT_HEIGHTS = {12: 12, 16: 16}


class EinkCanvas:
    def __init__(self, port: str, dst: int, baud: int = 115200, chunk_size: int = 20):
        """
        port       : serial port (e.g. /dev/tty.usbmodem...)
        dst        : destination RF address (e.g. 0x20)
        chunk_size : max ASCII chars per draw_text call (1-40).
                     Lower values increase reliability on noisy RF links.
        """
        if not (1 <= chunk_size <= send_data.RF_DRAW_TEXT_MAX_LEN):
            raise ValueError(f"chunk_size must be 1..{send_data.RF_DRAW_TEXT_MAX_LEN}")
        self.dst = dst
        self.chunk_size = chunk_size
        self._ser = None
        self._port = port
        self._baud = baud
        self._session_id = None
        self._clear_first = False
        self._ops = []
        self._open()

    def _open(self):
        try:
            import serial
        except ModuleNotFoundError:
            print("pyserial is required: pip install pyserial")
            sys.exit(1)
        self._ser = serial.Serial(self._port, self._baud, timeout=3)
        time.sleep(0.5)
        send_data.read_response(self._ser, timeout=1.0)

    def close(self):
        if self._ser and self._ser.is_open:
            self._ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def _reset_session(self):
        self._session_id = None
        self._clear_first = False
        self._ops = []

    def _begin_session(self, clear_first: bool) -> bool:
        self._session_id = send_data.next_draw_session_id()
        self._clear_first = clear_first
        payload = send_data.encode_draw_begin_payload(self.dst, self._session_id, clear_first)
        if not send_data.send_draw_begin(self._ser, payload):
            self._reset_session()
            return False
        return True

    def _ensure_session(self, clear_first: bool = False) -> bool:
        if self._session_id is None:
            return self._begin_session(clear_first)
        if clear_first and not self._clear_first:
            return self.clear()
        return True

    def _replay_session(self, full_refresh: bool = None) -> bool:
        ops = list(self._ops)
        clear_first = self._clear_first
        if not self._begin_session(clear_first):
            return False

        for op_index, (x, y, font, chunk) in enumerate(ops):
            payload = send_data.encode_draw_text_payload(
                self.dst,
                self._session_id,
                op_index,
                x,
                y,
                str(font),
                chunk,
            )
            if not send_data.send_draw_text(self._ser, payload):
                return False

        if full_refresh is None:
            return True

        commit_payload = send_data.encode_draw_commit_payload(self.dst, self._session_id)
        if not send_data.send_draw_commit(self._ser, commit_payload):
            return False

        flush_payload = send_data.encode_flush_payload(self.dst, self._session_id, full_refresh)
        if not send_data.send_display_flush(self._ser, flush_payload):
            return False

        self._reset_session()
        return True

    def draw_text(self, text: str, x: int, y: int, font: int = 16,
                  clear_first: bool = False) -> bool:
        """Stage text at (x, y) inside the current draw session."""
        if not text:
            return True
        if not self._ensure_session(clear_first=clear_first):
            return False
        chunks = [text[i:i + self.chunk_size]
                  for i in range(0, len(text), self.chunk_size)]
        ok = True
        for chunk in chunks:
            op_x = x
            op_index = len(self._ops)
            self._ops.append((op_x, y, font, chunk))
            payload = send_data.encode_draw_text_payload(
                self.dst, self._session_id, op_index, op_x, y, str(font), chunk
            )
            if not send_data.send_draw_text(self._ser, payload):
                if not self._replay_session(full_refresh=None):
                    ok = False
                    break
            x += len(chunk) * FONT_WIDTHS.get(font, 11)
        return ok

    def draw_multiline(self, lines: list, x_start: int = 5, y_start: int = 5,
                       font: int = 16, line_spacing: int = None,
                       clear_first_line: bool = False) -> bool:
        """
        Stage a list of strings as separate rows.
        line_spacing defaults to font height.
        """
        spacing = line_spacing if line_spacing is not None else FONT_HEIGHTS.get(font, 16)
        y = y_start
        ok = True
        for i, line in enumerate(lines):
            cf = clear_first_line and (i == 0)
            if not self.draw_text(line, x_start, y, font, clear_first=cf):
                ok = False
                break
            y += spacing
        return ok

    def flush(self, full_refresh: bool = True) -> bool:
        """Commit the current draw session and push it to the EPD."""
        if self._session_id is None:
            return True

        commit_payload = send_data.encode_draw_commit_payload(self.dst, self._session_id)
        if not send_data.send_draw_commit(self._ser, commit_payload):
            return self._replay_session(full_refresh=full_refresh)

        flush_payload = send_data.encode_flush_payload(self.dst, self._session_id, full_refresh)
        if not send_data.send_display_flush(self._ser, flush_payload):
            return self._replay_session(full_refresh=full_refresh)

        self._reset_session()
        return True

    def clear(self, full_refresh: bool = False) -> bool:
        """Start a fresh session with a cleared backing buffer."""
        self._reset_session()
        if not self._begin_session(clear_first=True):
            return False
        if full_refresh:
            return self.flush(full_refresh=True)
        return True

    def chars_per_row(self, font: int = 16) -> int:
        return DISPLAY_WIDTH_PX // FONT_WIDTHS.get(font, 11)

    def rows_available(self, font: int = 16) -> int:
        return DISPLAY_HEIGHT_PX // FONT_HEIGHTS.get(font, 16)
