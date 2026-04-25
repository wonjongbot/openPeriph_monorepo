#!/usr/bin/env python3
"""
send_data.py — PC-side tool to send data to the STM32 USB-RF bridge.

Usage:
    python send_data.py --port COM3 --image photo.jpg
    python send_data.py --port /dev/ttyACM0 --email "subject|body|recipient"
    python send_data.py --port /dev/ttyACM0 --text "Hello over RF!"
    python send_data.py --port /dev/ttyACM0 --ping
    python send_data.py --port /dev/ttyACM0 --status

Install:
    pip install pyserial

Protocol matches the STM32 firmware packet format:
    [0xAA 0x55] [TYPE] [ID] [LEN_LO LEN_HI] [PAYLOAD...] [CRC_LO CRC_HI] [0x0D]
"""

import argparse
import struct
import sys
import os
import time

# ── Packet types ──────────────────────────────────────────────────
PKT_TYPE_IMAGE_DATA     = 0x01
PKT_TYPE_EMAIL_DATA     = 0x02
PKT_TYPE_TEXT_DATA      = 0x03
PKT_TYPE_FILE_START     = 0x04
PKT_TYPE_FILE_END       = 0x05
PKT_TYPE_COMMAND        = 0x10
PKT_TYPE_DRAW_TEXT      = 0x11
PKT_TYPE_DRAW_BEGIN     = 0x12
PKT_TYPE_DRAW_COMMIT    = 0x13
PKT_TYPE_DISPLAY_FLUSH  = 0x14

PKT_TYPE_ACK         = 0x80
PKT_TYPE_NACK        = 0x81
PKT_TYPE_STATUS      = 0x82

CMD_PING             = 0x00
CMD_GET_STATUS       = 0x04
CMD_LOCAL_HELLO      = 0x06
CMD_RF_PING          = 0x07

APP_FONT_12 = 0x01
APP_FONT_16 = 0x02

APP_DRAW_FLAG_CLEAR_FIRST = 0x01

APP_TEXT_MAX_LEN = 40
RF_DRAW_TEXT_MAX_LEN = 24

PKT_MAX_PAYLOAD      = 1024
_last_rf_ping_result = None
_draw_session_counter = 0

# ── CRC-16/CCITT ─────────────────────────────────────────────────
def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc

# ── Packet builder ────────────────────────────────────────────────
_seq_counter = 0

def build_packet(pkt_type: int, payload: bytes) -> bytes:
    global _seq_counter

    pkt_id = _seq_counter & 0xFF
    _seq_counter += 1

    length = len(payload)
    # CRC covers: TYPE + ID + LEN(2) + PAYLOAD
    crc_data = struct.pack('<BBH', pkt_type, pkt_id, length) + payload
    crc = crc16_ccitt(crc_data)

    frame = bytes([0xAA, 0x55])                         # SYNC
    frame += struct.pack('<BBH', pkt_type, pkt_id, length)  # header
    frame += payload                                     # payload
    frame += struct.pack('<H', crc)                      # CRC16
    frame += bytes([0x0D])                               # END

    return frame

# ── Packet parser (for responses) ────────────────────────────────
def read_response(ser, timeout: float = 2.0) -> dict:
    """Read one response packet from the MCU."""
    ser.timeout = timeout
    result = {'valid': False, 'type': 0, 'id': 0, 'payload': b''}

    # Wait for SYNC
    while True:
        b = ser.read(1)
        if not b:
            return result  # timeout
        if b[0] == 0xAA:
            b2 = ser.read(1)
            if b2 and b2[0] == 0x55:
                break

    # Read header: TYPE(1) + ID(1) + LEN(2) = 4 bytes
    hdr = ser.read(4)
    if len(hdr) < 4:
        return result

    pkt_type, pkt_id, length = struct.unpack('<BBH', hdr)

    # Read payload
    payload = ser.read(length) if length > 0 else b''
    if len(payload) < length:
        return result

    # Read CRC(2) + END(1)
    trailer = ser.read(3)
    if len(trailer) < 3:
        return result

    crc_received = struct.unpack('<H', trailer[:2])[0]
    end_marker = trailer[2]

    # Verify
    crc_data = struct.pack('<BBH', pkt_type, pkt_id, length) + payload
    crc_calc = crc16_ccitt(crc_data)

    result['type'] = pkt_type
    result['id'] = pkt_id
    result['payload'] = payload
    result['valid'] = (crc_calc == crc_received and end_marker == 0x0D)

    return result

def parse_ack_telemetry(payload: bytes):
    if len(payload) >= 4:
        return {
            'retries': payload[1],
            'elapsed_ms': payload[2] | (payload[3] << 8),
        }
    return None

def get_last_rf_ping_result():
    if _last_rf_ping_result is None:
        return None
    return dict(_last_rf_ping_result)

# ── High-level senders ────────────────────────────────────────────
def send_and_wait_ack(ser, frame, label="packet", timeout: float = 2.0):
    ser.write(frame)
    print(f"  Sent {label} ({len(frame)} bytes)")

    resp = read_response(ser, timeout=timeout)
    if resp['valid'] and resp['type'] == PKT_TYPE_ACK:
        telemetry = parse_ack_telemetry(resp['payload'])
        if label == "DRAW_TEXT" and telemetry is not None:
            print(f"DRAW_TEXT ok: {telemetry['retries']} retries, {telemetry['elapsed_ms']} ms")
        else:
            print(f"  ACK received (id={resp['id']})")
        return True
    elif resp['valid'] and resp['type'] == PKT_TYPE_NACK:
        print(f"  NACK received (reason={resp['payload'].hex()})")
        return False
    else:
        print(f"  No valid response (timeout or corrupt)")
        return False

def send_file_chunked(ser, pkt_type, data, filename="data"):
    """Send large data in chunks with FILE_START / FILE_END framing."""
    total = len(data)
    print(f"Sending {filename} ({total} bytes) as type 0x{pkt_type:02X}")

    # FILE_START: payload = filename (null-term) + total_size (4 bytes LE)
    start_payload = filename.encode('ascii') + b'\x00' + struct.pack('<I', total)
    frame = build_packet(PKT_TYPE_FILE_START, start_payload)
    if not send_and_wait_ack(ser, frame, "FILE_START"):
        return False

    # Send data in chunks
    offset = 0
    chunk_num = 0
    while offset < total:
        chunk = data[offset:offset + PKT_MAX_PAYLOAD]
        frame = build_packet(pkt_type, chunk)
        ok = send_and_wait_ack(ser, frame, f"chunk {chunk_num} ({len(chunk)}B)")
        if not ok:
            print(f"  Failed at chunk {chunk_num}, aborting")
            return False
        offset += len(chunk)
        chunk_num += 1

    # FILE_END
    frame = build_packet(PKT_TYPE_FILE_END, b'')
    send_and_wait_ack(ser, frame, "FILE_END")

    print(f"Transfer complete: {chunk_num} chunks sent")
    return True

def send_ping(ser):
    payload = bytes([CMD_PING])
    frame = build_packet(PKT_TYPE_COMMAND, payload)
    ser.write(frame)
    print("Sent PING")
    resp = read_response(ser)
    if resp['valid'] and resp['type'] == PKT_TYPE_ACK:
        print("PONG! MCU is alive.")
    else:
        print("No response to PING.")

def send_get_status(ser):
    payload = bytes([CMD_GET_STATUS])
    frame = build_packet(PKT_TYPE_COMMAND, payload)
    ser.write(frame)
    print("Sent GET_STATUS")
    resp = read_response(ser)
    if resp['valid'] and resp['type'] == PKT_TYPE_STATUS:
        p = resp['payload']
        if len(p) >= 8:
            print(f"  Firmware: v{p[0]}.{p[1]}")
            print(f"  CC1101 MARCSTATE: 0x{p[2]:02X}")
            print(f"  RX buffer used: {p[3] | (p[4] << 8)} bytes")
            print(f"  Error count: {p[5] | (p[6] << 8)}")
            if len(p) >= 10:
                print(f"  CC1101 PARTNUM: 0x{p[8]:02X}")
                print(f"  CC1101 VERSION: 0x{p[9]:02X}")
            if p[7] == 1:
                print("  Radio recovery: recovered to RX")
            elif p[7] == 2:
                print("  Radio recovery: failed")
        else:
            print(f"  Status payload: {p.hex()}")
    elif resp['valid'] and resp['type'] == PKT_TYPE_NACK:
        p = resp['payload']
        if len(p) >= 2:
            print(f"  NACK received (id={p[0]}, reason=0x{p[1]:02X})")
        else:
            print(f"  NACK received (payload={p.hex()})")
    else:
        print("No valid status response.")

def send_local_hello(ser):
    payload = bytes([CMD_LOCAL_HELLO])
    frame = build_packet(PKT_TYPE_COMMAND, payload)
    send_and_wait_ack(ser, frame, "LOCAL_HELLO")

def send_rf_ping_with_details(ser, dst_addr):
    global _last_rf_ping_result

    _last_rf_ping_result = None

    if not 0 <= dst_addr <= 0xFF:
        raise ValueError("destination address must fit in 0..255")

    payload = bytes([CMD_RF_PING, dst_addr])
    frame = build_packet(PKT_TYPE_COMMAND, payload)
    ser.write(frame)
    print(f"Sent RF_PING to 0x{dst_addr:02X}")

    start = time.monotonic()
    resp = read_response(ser)
    rtt_ms = int((time.monotonic() - start) * 1000)

    if resp['valid'] and resp['type'] == PKT_TYPE_ACK:
        telemetry = parse_ack_telemetry(resp['payload'])
        if telemetry is not None:
            elapsed_ms = telemetry['elapsed_ms']
            retries = telemetry['retries']
            retry_label = "retry" if retries == 1 else "retries"
            print(f"PONG from 0x{dst_addr:02X}: {retries} {retry_label}, {elapsed_ms} ms")
            _last_rf_ping_result = {
                'ok': True,
                'retries': retries,
                'elapsed_ms': elapsed_ms,
            }
            return _last_rf_ping_result

        print(f"PONG from 0x{dst_addr:02X} (host RTT {rtt_ms} ms)")
        _last_rf_ping_result = {
            'ok': True,
            'retries': 0,
            'elapsed_ms': rtt_ms,
        }
        return _last_rf_ping_result

    if resp['valid'] and resp['type'] == PKT_TYPE_NACK:
        telemetry = parse_ack_telemetry(resp['payload'][1:]) if len(resp['payload']) >= 5 else None
        if len(resp['payload']) >= 2:
            reason = resp['payload'][1]
            if reason == 0x06:
                print(f"No RF pong from node 0x{dst_addr:02X}")
            elif reason == 0x07:
                print(f"Invalid RF ping destination: 0x{dst_addr:02X}")
            else:
                print(f"RF ping failed (reason=0x{reason:02X})")
        else:
            print("No valid RF ping response.")
        _last_rf_ping_result = {
            'ok': False,
            'retries': telemetry['retries'] if telemetry is not None else 0,
            'elapsed_ms': telemetry['elapsed_ms'] if telemetry is not None else rtt_ms,
        }
        return _last_rf_ping_result

    print("No valid RF ping response.")
    _last_rf_ping_result = {
        'ok': False,
        'retries': 0,
        'elapsed_ms': rtt_ms,
    }
    return _last_rf_ping_result

def send_rf_ping(ser, dst_addr):
    return send_rf_ping_with_details(ser, dst_addr)['ok']

def send_draw_begin(ser, payload: bytes):
    frame = build_packet(PKT_TYPE_DRAW_BEGIN, payload)
    return send_and_wait_ack(ser, frame, "DRAW_BEGIN")

def send_draw_text(ser, payload: bytes):
    frame = build_packet(PKT_TYPE_DRAW_TEXT, payload)
    return send_and_wait_ack(ser, frame, "DRAW_TEXT")

def send_draw_commit(ser, payload: bytes):
    frame = build_packet(PKT_TYPE_DRAW_COMMIT, payload)
    return send_and_wait_ack(ser, frame, "DRAW_COMMIT")

def encode_draw_begin_payload(dst: int, session_id: int, clear_first: bool) -> bytes:
    if not 0 <= dst <= 0xFF:
        raise ValueError("destination address must fit in 0..255")
    if not 0 <= session_id <= 0xFF:
        raise ValueError("session id must fit in 0..255")
    flags = APP_DRAW_FLAG_CLEAR_FIRST if clear_first else 0
    return bytes([dst, session_id, flags])

def encode_draw_commit_payload(dst: int, session_id: int) -> bytes:
    if not 0 <= dst <= 0xFF:
        raise ValueError("destination address must fit in 0..255")
    if not 0 <= session_id <= 0xFF:
        raise ValueError("session id must fit in 0..255")
    return bytes([dst, session_id])

def encode_flush_payload(dst: int, session_id: int, full_refresh: bool) -> bytes:
    if not 0 <= dst <= 0xFF:
        raise ValueError("destination address must fit in 0..255")
    if not 0 <= session_id <= 0xFF:
        raise ValueError("session id must fit in 0..255")
    return bytes([dst, session_id, 1 if full_refresh else 0])

def send_display_flush(ser, payload: bytes):
    frame = build_packet(PKT_TYPE_DISPLAY_FLUSH, payload)
    # If the slave's 3× ACK burst is lost, the master must wait for the
    # EPD refresh (3-5 s) to finish before the slave can re-ACK.
    return send_and_wait_ack(ser, frame, "DISPLAY_FLUSH", timeout=10.0)

def print_rf_ping_bench_summary(results):
    total = len(results)
    success = sum(1 for item in results if item['ok'])
    failed = total - success
    retries = sum(item['retries'] for item in results)
    failure_rate = (failed / total * 100.0) if total else 0.0
    print(f"{total} attempts, {success} success, {failed} failed, {retries} total retries, failure rate {failure_rate:.1f}%")

def run_rf_ping_bench(ser, dst_addr, count):
    if count <= 0:
        raise ValueError("count must be positive")

    results = []
    for attempt in range(count):
        print(f"RF ping bench attempt {attempt + 1}/{count}")
        results.append(send_rf_ping_with_details(ser, dst_addr))

    print_rf_ping_bench_summary(results)
    return results

def encode_draw_text_payload(dst: int,
                             session_id: int,
                             op_index: int,
                             x: int,
                             y: int,
                             font: str,
                             text: str) -> bytes:
    if not 0 <= dst <= 0xFF:
        raise ValueError("destination address must fit in 0..255")
    if not 0 <= session_id <= 0xFF:
        raise ValueError("session id must fit in 0..255")
    if not 0 <= op_index <= 0xFF:
        raise ValueError("op index must fit in 0..255")
    if not 0 <= x <= 0xFFFF:
        raise ValueError("x must fit in 0..65535")
    if not 0 <= y <= 0xFFFF:
        raise ValueError("y must fit in 0..65535")

    try:
        text_bytes = text.encode('ascii')
    except UnicodeEncodeError as exc:
        raise ValueError("draw text currently supports ASCII only") from exc

    if len(text_bytes) > RF_DRAW_TEXT_MAX_LEN:
        raise ValueError(f"draw text payload exceeds {RF_DRAW_TEXT_MAX_LEN} ASCII bytes")

    font_id = APP_FONT_12 if font == '12' else APP_FONT_16

    return (
        bytes([dst])
        + bytes([session_id, op_index])
        + struct.pack('<HHB', x, y, font_id)
        + bytes([len(text_bytes)])
        + text_bytes
    )

def next_draw_session_id() -> int:
    global _draw_session_counter

    _draw_session_counter = (_draw_session_counter + 1) & 0xFF
    if _draw_session_counter == 0:
        _draw_session_counter = 1
    return _draw_session_counter

def send_single_draw_text(ser,
                          dst: int,
                          x: int,
                          y: int,
                          font: str,
                          text: str,
                          clear_first: bool = False,
                          full_refresh: bool = True) -> bool:
    session_id = next_draw_session_id()
    begin_payload = encode_draw_begin_payload(dst, session_id, clear_first)
    text_payload = encode_draw_text_payload(dst, session_id, 0, x, y, font, text)
    commit_payload = encode_draw_commit_payload(dst, session_id)
    flush_payload = encode_flush_payload(dst, session_id, full_refresh)

    return (
        send_draw_begin(ser, begin_payload)
        and send_draw_text(ser, text_payload)
        and send_draw_commit(ser, commit_payload)
        and send_display_flush(ser, flush_payload)
    )

# ── CLI ──────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Send data to STM32 USB-RF bridge")
    parser.add_argument('--port', required=True,
                        help='Serial port (COM3 or /dev/ttyACM0)')
    parser.add_argument('--baud', type=int, default=115200,
                        help='Baud rate (default: 115200, ignored for CDC)')
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument('--image', help='Path to image file to send')
    actions.add_argument('--email',
                         help='Email data as "subject|body|recipient"')
    actions.add_argument('--text', help='Plain text message to send')
    actions.add_argument('--draw-text', help='Text to draw on slave EPD')
    actions.add_argument('--flush', action='store_true',
                         help='Flush staged framebuffer to EPD (requires --dst)')
    parser.add_argument('--session', type=lambda x: int(x, 0),
                        help='Draw session id for low-level staged commands')
    parser.add_argument('--dst', type=lambda x: int(x, 0),
                        help='Destination slave address')
    parser.add_argument('--x', type=int, default=0,
                        help='Draw X coordinate')
    parser.add_argument('--y', type=int, default=0,
                        help='Draw Y coordinate')
    parser.add_argument('--font', choices=['12', '16'], default='16',
                        help='Font size')
    parser.add_argument('--clear-first', action='store_true',
                        help='Clear display before drawing')
    parser.add_argument('--full-refresh', action='store_true',
                        help='Request full display refresh')
    actions.add_argument('--ping', action='store_true',
                         help='Send a ping command')
    actions.add_argument('--status', action='store_true',
                         help='Request MCU status')
    actions.add_argument('--rf-ping', type=lambda x: int(x, 0),
                         help='Send an RF ping to the given destination address')
    actions.add_argument('--rf-ping-bench', type=lambda x: int(x, 0),
                         help='Run repeated RF ping diagnostics against the given destination')
    actions.add_argument('--local-hello', action='store_true',
                         help='Render local Hello World on the connected board EPD')
    parser.add_argument('--count', type=int, default=20,
                        help='Number of repeated RF diagnostic attempts')

    args = parser.parse_args()

    try:
        import serial
    except ModuleNotFoundError:
        print("pyserial is required. Install with: pip install pyserial")
        sys.exit(1)

    # Open serial port
    try:
        ser = serial.Serial(args.port, args.baud, timeout=2)
        time.sleep(0.5)  # let USB CDC settle
        print(f"Connected to {args.port}")
    except serial.SerialException as e:
        print(f"Error opening {args.port}: {e}")
        sys.exit(1)

    try:
        # Check for startup message
        resp = read_response(ser, timeout=1.0)
        if resp['valid'] and resp['type'] == PKT_TYPE_STATUS:
            print(f"MCU says: {resp['payload'].decode('ascii', errors='replace')}")

        if args.ping:
            send_ping(ser)

        elif args.rf_ping is not None:
            try:
                send_rf_ping(ser, args.rf_ping)
            except ValueError as exc:
                print(f"Invalid RF ping request: {exc}")
                sys.exit(1)

        elif args.rf_ping_bench is not None:
            try:
                run_rf_ping_bench(ser, args.rf_ping_bench, args.count)
            except ValueError as exc:
                print(f"Invalid RF ping request: {exc}")
                sys.exit(1)

        elif args.status:
            send_get_status(ser)

        elif args.local_hello:
            send_local_hello(ser)

        elif args.image:
            if not os.path.exists(args.image):
                print(f"File not found: {args.image}")
                sys.exit(1)
            with open(args.image, 'rb') as f:
                data = f.read()
            send_file_chunked(ser, PKT_TYPE_IMAGE_DATA, data,
                              os.path.basename(args.image))

        elif args.email:
            data = args.email.encode('utf-8')
            if len(data) <= PKT_MAX_PAYLOAD:
                frame = build_packet(PKT_TYPE_EMAIL_DATA, data)
                send_and_wait_ack(ser, frame, "email")
            else:
                send_file_chunked(ser, PKT_TYPE_EMAIL_DATA, data, "email")

        elif args.text:
            data = args.text.encode('utf-8')
            frame = build_packet(PKT_TYPE_TEXT_DATA, data)
            send_and_wait_ack(ser, frame, "text")

        elif args.draw_text is not None:
            if args.dst is None:
                print("--dst is required with --draw-text")
                sys.exit(1)

            try:
                ok = send_single_draw_text(
                    ser,
                    args.dst,
                    args.x,
                    args.y,
                    args.font,
                    args.draw_text,
                    clear_first=args.clear_first,
                    full_refresh=args.full_refresh,
                )
            except ValueError as exc:
                print(f"Invalid draw-text request: {exc}")
                sys.exit(1)
            if not ok:
                sys.exit(1)

        elif args.flush:
            if args.dst is None or args.session is None:
                print("--dst and --session are required with --flush")
                sys.exit(1)
            try:
                payload = encode_flush_payload(args.dst, args.session, args.full_refresh)
            except ValueError as exc:
                print(f"Invalid flush request: {exc}")
                sys.exit(1)
            send_display_flush(ser, payload)

        else:
            print("No action specified. Use --ping, --rf-ping, --rf-ping-bench, --image, --email, --text, --draw-text, or --flush")

    finally:
        ser.close()

if __name__ == '__main__':
    main()
