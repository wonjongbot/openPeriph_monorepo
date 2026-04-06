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
PKT_TYPE_IMAGE_DATA  = 0x01
PKT_TYPE_EMAIL_DATA  = 0x02
PKT_TYPE_TEXT_DATA   = 0x03
PKT_TYPE_FILE_START  = 0x04
PKT_TYPE_FILE_END    = 0x05
PKT_TYPE_COMMAND     = 0x10
PKT_TYPE_DRAW_TEXT   = 0x11

PKT_TYPE_ACK         = 0x80
PKT_TYPE_NACK        = 0x81
PKT_TYPE_STATUS      = 0x82

CMD_PING             = 0x00
CMD_GET_STATUS       = 0x04
CMD_LOCAL_HELLO      = 0x06

APP_FONT_12 = 0x01
APP_FONT_16 = 0x02

APP_DRAW_FLAG_CLEAR_FIRST = 0x01
APP_DRAW_FLAG_FULL_REFRESH = 0x02

APP_TEXT_MAX_LEN = 40

PKT_MAX_PAYLOAD      = 1024

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

# ── High-level senders ────────────────────────────────────────────
def send_and_wait_ack(ser, frame, label="packet"):
    ser.write(frame)
    print(f"  Sent {label} ({len(frame)} bytes)")

    resp = read_response(ser)
    if resp['valid'] and resp['type'] == PKT_TYPE_ACK:
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
        else:
            print(f"  Status payload: {p.hex()}")
    else:
        print("No valid status response.")

def send_local_hello(ser):
    payload = bytes([CMD_LOCAL_HELLO])
    frame = build_packet(PKT_TYPE_COMMAND, payload)
    send_and_wait_ack(ser, frame, "LOCAL_HELLO")

def encode_draw_text_payload(dst: int,
                             x: int,
                             y: int,
                             font: str,
                             clear_first: bool,
                             full_refresh: bool,
                             text: str) -> bytes:
    if not 0 <= dst <= 0xFF:
        raise ValueError("destination address must fit in 0..255")
    if not 0 <= x <= 0xFFFF:
        raise ValueError("x must fit in 0..65535")
    if not 0 <= y <= 0xFFFF:
        raise ValueError("y must fit in 0..65535")

    try:
        text_bytes = text.encode('ascii')
    except UnicodeEncodeError as exc:
        raise ValueError("draw text currently supports ASCII only") from exc

    if len(text_bytes) > APP_TEXT_MAX_LEN:
        raise ValueError(f"draw text payload exceeds {APP_TEXT_MAX_LEN} ASCII bytes")

    font_id = APP_FONT_12 if font == '12' else APP_FONT_16
    flags = 0
    if clear_first:
        flags |= APP_DRAW_FLAG_CLEAR_FIRST
    if full_refresh:
        flags |= APP_DRAW_FLAG_FULL_REFRESH

    return (
        bytes([dst])
        + struct.pack('<HHBB', x, y, font_id, flags)
        + bytes([len(text_bytes)])
        + text_bytes
    )

# ── CLI ──────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Send data to STM32 USB-RF bridge")
    parser.add_argument('--port', required=True,
                        help='Serial port (COM3 or /dev/ttyACM0)')
    parser.add_argument('--baud', type=int, default=115200,
                        help='Baud rate (default: 115200, ignored for CDC)')
    parser.add_argument('--image', help='Path to image file to send')
    parser.add_argument('--email',
                        help='Email data as "subject|body|recipient"')
    parser.add_argument('--text', help='Plain text message to send')
    parser.add_argument('--draw-text', help='Text to draw on slave EPD')
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
    parser.add_argument('--ping', action='store_true',
                        help='Send a ping command')
    parser.add_argument('--status', action='store_true',
                        help='Request MCU status')
    parser.add_argument('--local-hello', action='store_true',
                        help='Render local Hello World on the connected board EPD')

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
                payload = encode_draw_text_payload(
                    args.dst,
                    args.x,
                    args.y,
                    args.font,
                    args.clear_first,
                    args.full_refresh,
                    args.draw_text,
                )
            except ValueError as exc:
                print(f"Invalid draw-text request: {exc}")
                sys.exit(1)
            frame = build_packet(PKT_TYPE_DRAW_TEXT, payload)
            send_and_wait_ack(ser, frame, "DRAW_TEXT")

        else:
            print("No action specified. Use --ping, --image, --email, --text, or --draw-text")

    finally:
        ser.close()

if __name__ == '__main__':
    main()
