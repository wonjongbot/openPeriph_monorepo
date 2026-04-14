#!/usr/bin/env python3

import contextlib
import io
import os
import sys
import unittest
from types import SimpleNamespace

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(REPO_ROOT, 'scripts'))

import send_data


class FakeSerial:
    def __init__(self, response):
        self.response = bytearray(response)
        self.timeout = None
        self.written = b''
        self.closed = False

    def write(self, data):
        self.written += data

    def read(self, size):
        data = self.response[:size]
        del self.response[:size]
        return bytes(data)

    def close(self):
        self.closed = True


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

    def test_status_reports_cc1101_chip_info(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_STATUS,
            bytes([1, 0, 0x0D, 0x34, 0x12, 0x78, 0x56, 0, 0x12, 0x34]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_get_status(ser)

        self.assertIn('CC1101 PARTNUM: 0x12', out.getvalue())
        self.assertIn('CC1101 VERSION: 0x34', out.getvalue())

    def test_rf_ping_success_reports_pong(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_ACK,
            bytes([0x00]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_rf_ping(ser, 0x22)

        self.assertIn('Sent RF_PING to 0x22', out.getvalue())
        self.assertIn('PONG from 0x22', out.getvalue())

    def test_rf_ping_timeout_reports_no_pong(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_NACK,
            bytes([0x00, 0x06]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_rf_ping(ser, 0x22)

        self.assertIn('No RF pong from node 0x22', out.getvalue())

    def test_rf_ping_reports_invalid_destination_nack(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_NACK,
            bytes([0x00, 0x07]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_rf_ping(ser, 0x22)

        self.assertIn('Invalid RF ping destination: 0x22', out.getvalue())

    def test_rf_ping_rejects_out_of_range_destination(self):
        ser = FakeSerial(b'')

        with self.assertRaises(ValueError):
            send_data.send_rf_ping(ser, -1)

        with self.assertRaises(ValueError):
            send_data.send_rf_ping(ser, 256)

    def test_main_reports_invalid_rf_ping_request_cleanly(self):
        fake_serial_instance = FakeSerial(b'')

        class FakeSerialModule:
            class SerialException(Exception):
                pass

            def Serial(self, *args, **kwargs):
                return fake_serial_instance

        old_argv = sys.argv
        old_sys_modules = sys.modules.copy()
        sys.argv = [
            'send_data.py',
            '--port',
            'COM1',
            '--rf-ping',
            '256',
        ]
        sys.modules['serial'] = SimpleNamespace(
            Serial=FakeSerialModule().Serial,
            SerialException=FakeSerialModule.SerialException,
        )

        out = io.StringIO()

        try:
            with contextlib.redirect_stdout(out):
                with self.assertRaises(SystemExit) as cm:
                    send_data.main()
            self.assertEqual(cm.exception.code, 1)
            self.assertIn('Invalid RF ping request:', out.getvalue())
        finally:
            sys.argv = old_argv
            sys.modules.clear()
            sys.modules.update(old_sys_modules)


if __name__ == '__main__':
    unittest.main()
