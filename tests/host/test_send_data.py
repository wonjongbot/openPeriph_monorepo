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
    def setUp(self):
        send_data._last_rf_ping_result = None

    def test_get_last_rf_ping_result_is_none_before_any_ping(self):
        self.assertIsNone(send_data.get_last_rf_ping_result())

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

    def test_rf_ping_returns_boolean_success(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_ACK,
            bytes([0x00, 0x01, 0x2A, 0x00]),
        )
        ser = FakeSerial(response)

        result = send_data.send_rf_ping(ser, 0x22)

        self.assertIs(result, True)

    def test_rf_ping_success_reports_retry_count_and_elapsed(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_ACK,
            bytes([0x00, 0x01, 0x2A, 0x00]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            send_data.send_rf_ping(ser, 0x22)

        self.assertIn('1 retry', out.getvalue())
        self.assertIn('42 ms', out.getvalue())
        self.assertEqual(send_data.get_last_rf_ping_result()['retries'], 1)
        self.assertEqual(send_data.get_last_rf_ping_result()['elapsed_ms'], 42)

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
        self.assertIs(send_data.send_rf_ping(FakeSerial(response), 0x22), False)

    def test_rf_ping_timeout_preserves_retry_stats_from_nack_payload(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_NACK,
            bytes([0x00, 0x06, 0x08, 0x58, 0x02]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            result = send_data.send_rf_ping(ser, 0x22)

        self.assertIn('No RF pong from node 0x22', out.getvalue())
        self.assertIs(result, False)
        self.assertEqual(send_data.get_last_rf_ping_result()['retries'], 8)
        self.assertEqual(send_data.get_last_rf_ping_result()['elapsed_ms'], 600)

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

    def test_draw_text_ack_reports_retry_telemetry(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_ACK,
            bytes([0x00, 0x02, 0xB8, 0x00]),
        )
        ser = FakeSerial(response)
        out = io.StringIO()
        payload = send_data.encode_draw_text_payload(0x22, 0, 0, '16', False, False, 'HELLO')

        with contextlib.redirect_stdout(out):
            send_data.send_draw_text(ser, payload)

        self.assertIn('DRAW_TEXT ok', out.getvalue())
        self.assertIn('2 retries', out.getvalue())
        self.assertIn('184 ms', out.getvalue())

    def test_rf_ping_rejects_out_of_range_destination(self):
        ser = FakeSerial(b'')

        with self.assertRaises(ValueError):
            send_data.send_rf_ping(ser, -1)

        with self.assertRaises(ValueError):
            send_data.send_rf_ping(ser, 256)

    def test_invalid_rf_ping_request_clears_last_result(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_ACK,
            bytes([0x00, 0x01, 0x2A, 0x00]),
        )
        send_data.send_rf_ping(FakeSerial(response), 0x22)

        with self.assertRaises(ValueError):
            send_data.send_rf_ping(FakeSerial(b''), 256)

        self.assertIsNone(send_data.get_last_rf_ping_result())

    def test_get_last_rf_ping_result_returns_copy(self):
        response = send_data.build_packet(
            send_data.PKT_TYPE_ACK,
            bytes([0x00, 0x01, 0x2A, 0x00]),
        )
        send_data.send_rf_ping(FakeSerial(response), 0x22)

        result = send_data.get_last_rf_ping_result()
        result['retries'] = 99

        self.assertEqual(send_data.get_last_rf_ping_result()['retries'], 1)

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

    def test_main_reports_invalid_rf_ping_bench_count_cleanly(self):
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
            '--rf-ping-bench',
            '0x22',
            '--count',
            '0',
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

    def test_main_dispatches_rf_ping_bench(self):
        fake_serial_instance = FakeSerial(b'')

        class FakeSerialModule:
            class SerialException(Exception):
                pass

            def Serial(self, *args, **kwargs):
                return fake_serial_instance

        old_argv = sys.argv
        old_sys_modules = sys.modules.copy()
        old_runner = send_data.run_rf_ping_bench
        calls = []
        sys.argv = [
            'send_data.py',
            '--port',
            'COM1',
            '--rf-ping-bench',
            '0x22',
            '--count',
            '3',
        ]
        sys.modules['serial'] = SimpleNamespace(
            Serial=FakeSerialModule().Serial,
            SerialException=FakeSerialModule.SerialException,
        )

        def fake_run_rf_ping_bench(ser, dst_addr, count):
            calls.append((ser, dst_addr, count))
            return []

        send_data.run_rf_ping_bench = fake_run_rf_ping_bench

        try:
            with contextlib.redirect_stdout(io.StringIO()):
                send_data.main()
            self.assertEqual(calls, [(fake_serial_instance, 0x22, 3)])
        finally:
            send_data.run_rf_ping_bench = old_runner
            sys.argv = old_argv
            sys.modules.clear()
            sys.modules.update(old_sys_modules)

    def test_main_rejects_conflicting_action_flags(self):
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
            '0x22',
            '--status',
        ]
        sys.modules['serial'] = SimpleNamespace(
            Serial=FakeSerialModule().Serial,
            SerialException=FakeSerialModule.SerialException,
        )

        stderr = io.StringIO()

        try:
            with contextlib.redirect_stderr(stderr):
                with self.assertRaises(SystemExit) as cm:
                    send_data.main()
            self.assertEqual(cm.exception.code, 2)
            self.assertIn('not allowed with argument', stderr.getvalue())
        finally:
            sys.argv = old_argv
            sys.modules.clear()
            sys.modules.update(old_sys_modules)

    def test_rf_ping_bench_reports_summary(self):
        out = io.StringIO()
        stats = [
            {'ok': True, 'retries': 0, 'elapsed_ms': 12},
            {'ok': False, 'retries': 8, 'elapsed_ms': 600},
        ]

        with contextlib.redirect_stdout(out):
            send_data.print_rf_ping_bench_summary(stats)

        self.assertIn('2 attempts', out.getvalue())
        self.assertIn('1 success', out.getvalue())
        self.assertIn('1 failed', out.getvalue())
        self.assertIn('8 total retries', out.getvalue())
        self.assertIn('failure rate 50.0%', out.getvalue())

    def test_run_rf_ping_bench_aggregates_results(self):
        responses = (
            send_data.build_packet(send_data.PKT_TYPE_ACK, bytes([0x00, 0x01, 0x0C, 0x00]))
            + send_data.build_packet(send_data.PKT_TYPE_NACK, bytes([0x00, 0x06, 0x08, 0x58, 0x02]))
        )
        ser = FakeSerial(responses)
        out = io.StringIO()

        with contextlib.redirect_stdout(out):
            results = send_data.run_rf_ping_bench(ser, 0x22, 2)

        self.assertEqual(
            results,
            [
                {'ok': True, 'retries': 1, 'elapsed_ms': 12},
                {'ok': False, 'retries': 8, 'elapsed_ms': 600},
            ],
        )
        self.assertIn('2 attempts, 1 success, 1 failed, 9 total retries, failure rate 50.0%', out.getvalue())

    def test_run_rf_ping_bench_rejects_non_positive_count(self):
        ser = FakeSerial(b'')

        with self.assertRaises(ValueError):
            send_data.run_rf_ping_bench(ser, 0x22, 0)

        with self.assertRaises(ValueError):
            send_data.run_rf_ping_bench(ser, 0x22, -1)


if __name__ == '__main__':
    unittest.main()
