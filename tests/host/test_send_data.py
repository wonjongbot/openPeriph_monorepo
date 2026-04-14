#!/usr/bin/env python3

import contextlib
import io
import os
import sys
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(REPO_ROOT, 'scripts'))

import send_data


class FakeSerial:
    def __init__(self, response):
        self.response = bytearray(response)
        self.timeout = None
        self.written = b''

    def write(self, data):
        self.written += data

    def read(self, size):
        data = self.response[:size]
        del self.response[:size]
        return bytes(data)


class SendDataTests(unittest.TestCase):
    def test_status_reports_nack_reason(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_NACK,
            bytes([0x00, 0x04]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_get_status(ser)

        self.assertIn('NACK received', out.getvalue())
        self.assertIn('0x04', out.getvalue())

    def test_status_reports_radio_recovery(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_STATUS,
            bytes([1, 0, 0x0D, 0, 0, 0, 0, 1]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_get_status(ser)

        self.assertIn('CC1101 MARCSTATE: 0x0D', out.getvalue())
        self.assertIn('Radio recovery: recovered to RX', out.getvalue())


if __name__ == '__main__':
    unittest.main()
