/**
 * @file    usb_protocol.h
 * @brief   Packet-based protocol layer on top of USB CDC
 *
 * PACKET FORMAT (all multi-byte fields are little-endian):
 * ┌──────────┬──────────┬────────┬──────────┬─────────────┬──────────┬──────────┐
 * │ SYNC (2) │ TYPE (1) │ ID (1) │ LEN (2)  │ PAYLOAD (N) │ CRC16(2) │  END (1) │
 * │ 0xAA 0x55│          │        │          │             │          │   0x0D   │
 * └──────────┴──────────┴────────┴──────────┴─────────────┴──────────┴──────────┘
 *
 *  SYNC    : 0xAA 0x55 — marks start of frame
 *  TYPE    : Packet type (image=0x01, email=0x02, text=0x03. command=0x10, ACK=0x80...)
 *  ID      : Sequence number (0-255, wraps)
 *  LEN     : Payload length (0 .. PKT_MAX_PAYLOAD)
 *  PAYLOAD : The actual data
 *  CRC16   : CRC-16/CCITT over TYPE+ID+LEN+PAYLOAD
 *  END     : 0x0D — marks end of frame
 *
 *  Max total frame size = 2 + 1 + 1 + 2 + PKT_MAX_PAYLOAD + 2 + 1
 */

#ifndef USB_PROTOCOL_H
#define USB_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "ring_buffer.h"

/* ---------- Protocol constants ---------- */
#define PKT_SYNC_0          0xAAU
#define PKT_SYNC_1          0x55U
#define PKT_END_MARKER      0x0DU

#define PKT_HEADER_SIZE     6U      /* SYNC(2) + TYPE(1) + ID(1) + LEN(2) */
#define PKT_TRAILER_SIZE    3U      /* CRC16(2) + END(1) */
#define PKT_OVERHEAD        (PKT_HEADER_SIZE + PKT_TRAILER_SIZE)

/* Max payload per packet.
 * CC1101 max FIFO is 64 bytes, but we allow larger payloads that get
 * chunked before sending over RF. 1024 is a good balance for image chunks. */
#define PKT_MAX_PAYLOAD     1024U
#define PKT_MAX_FRAME       (PKT_OVERHEAD + PKT_MAX_PAYLOAD)

/* ---------- Packet types ---------- */
typedef enum {
    /* PC -> MCU */
    PKT_TYPE_IMAGE_DATA     = 0x01,  /* Image chunk */
    PKT_TYPE_EMAIL_DATA     = 0x02,  /* Email content */
    PKT_TYPE_TEXT_DATA      = 0x03,  /* Plain text message */
    PKT_TYPE_FILE_START     = 0x04,  /* Start of a multi-packet file transfer */
    PKT_TYPE_FILE_END       = 0x05,  /* End of multi-packet file transfer */
    PKT_TYPE_COMMAND        = 0x10,  /* Generic command to MCU */
    PKT_TYPE_DRAW_TEXT      = 0x11,  /* Draw-text operation payload */
    PKT_TYPE_DRAW_BEGIN     = 0x12,  /* Begin staged draw session */
    PKT_TYPE_DRAW_COMMIT    = 0x13,  /* Commit staged draw session */
    PKT_TYPE_DISPLAY_FLUSH  = 0x14,  /* Flush committed staged framebuffer to EPD */

    /* MCU -> PC */
    PKT_TYPE_ACK            = 0x80,  /* Acknowledge receipt */
    PKT_TYPE_NACK           = 0x81,  /* Negative acknowledge (CRC error, etc.) */
    PKT_TYPE_STATUS         = 0x82,  /* MCU status report */
    PKT_TYPE_RF_TX_DONE     = 0x83,  /* CC1101 finished transmitting */
    PKT_TYPE_RF_RX_DATA     = 0x84,  /* Data received over RF */
    PKT_TYPE_ERROR          = 0xFE,  /* Error report */
    PKT_TYPE_DEBUG          = 0xFF,  /* Debug / log message */
} PacketType_t;

/* ---------- Command sub-types (payload byte 0 when TYPE == PKT_TYPE_COMMAND) ---------- */
typedef enum {
    CMD_PING            = 0x00,  /* Ping -> responds with ACK */
    CMD_RESET           = 0x01,  /* Soft-reset MCU */
    CMD_SET_RF_CHANNEL  = 0x02,  /* Set CC1101 channel (payload[1]) */
    CMD_SET_RF_POWER    = 0x03,  /* Set CC1101 TX power (payload[1]) */
    CMD_GET_STATUS      = 0x04,  /* Request status report */
    CMD_SET_RF_ADDR     = 0x05,  /* Set CC1101 device address */
    CMD_LOCAL_HELLO     = 0x06,  /* Render a local display hello-world test */
    CMD_RF_PING         = 0x07,  /* Send RF ping and wait for pong */
} CommandID_t;

/* ---------- Parsed packet structure ---------- */
typedef struct {
    PacketType_t type;
    uint8_t      id;
    uint16_t     payload_len;
    uint8_t      payload[PKT_MAX_PAYLOAD];
    uint16_t     crc;
    bool         valid;          /* true if CRC matched */
} Packet_t;

/* ---------- Parser state machine ---------- */
typedef enum {
    PARSE_WAIT_SYNC0,
    PARSE_WAIT_SYNC1,
    PARSE_WAIT_TYPE,
    PARSE_WAIT_ID,
    PARSE_WAIT_LEN_LO,
    PARSE_WAIT_LEN_HI,
    PARSE_WAIT_PAYLOAD,
    PARSE_WAIT_CRC_LO,
    PARSE_WAIT_CRC_HI,
    PARSE_WAIT_END,
} ParserState_t;

typedef struct {
    ParserState_t state;
    Packet_t      pkt;
    uint16_t      payload_idx;
    uint8_t       seq_counter;   /* auto-incrementing TX sequence */
} ProtocolParser_t;

/* ---------- API ---------- */

/** Initialise the protocol parser */
void Protocol_Init(ProtocolParser_t *ctx);

/**
 * Feed raw bytes from USB CDC into the parser.
 * Returns true when a complete, valid packet is available in ctx->pkt.
 * Call repeatedly for each byte or block received.
 */
bool Protocol_ParseByte(ProtocolParser_t *ctx, uint8_t byte);

/**
 * Build a packet frame into `out_buf`.
 * Returns total frame length written to out_buf.
 * out_buf must be at least PKT_MAX_FRAME bytes.
 */
uint16_t Protocol_BuildPacket(ProtocolParser_t *ctx,
                              PacketType_t type,
                              const uint8_t *payload,
                              uint16_t payload_len,
                              uint8_t *out_buf);

/** Build and return an ACK packet for a given packet ID */
uint16_t Protocol_BuildACK(ProtocolParser_t *ctx, uint8_t ack_id,
                           uint8_t *out_buf);

/** Build and return a NACK packet for a given packet ID */
uint16_t Protocol_BuildNACK(ProtocolParser_t *ctx, uint8_t nack_id,
                            uint8_t reason, uint8_t *out_buf);

/** Compute CRC-16/CCITT over a buffer */
uint16_t Protocol_CRC16(const uint8_t *data, uint16_t len);

#endif /* USB_PROTOCOL_H */
