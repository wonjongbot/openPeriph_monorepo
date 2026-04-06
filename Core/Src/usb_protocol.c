/**
 * @file    usb_protocol.c
 */

#include "usb_protocol.h"
#include <string.h>

/* ================================================================
 *  CRC-16/CCITT (polynomial 0x1021, init 0xFFFF)
 * ================================================================ */
uint16_t Protocol_CRC16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ================================================================
 *  Parser
 * ================================================================ */
void Protocol_Init(ProtocolParser_t *ctx)
{
    memset(ctx, 0, sizeof(ProtocolParser_t));
    ctx->state = PARSE_WAIT_SYNC0;
    ctx->seq_counter = 0;
}

bool Protocol_ParseByte(ProtocolParser_t *ctx, uint8_t byte)
{
    switch (ctx->state) {

    case PARSE_WAIT_SYNC0:
        if (byte == PKT_SYNC_0)
            ctx->state = PARSE_WAIT_SYNC1;
        break;

    case PARSE_WAIT_SYNC1:
        if (byte == PKT_SYNC_1) {
            ctx->state = PARSE_WAIT_TYPE;
            ctx->payload_idx = 0;
            memset(&ctx->pkt, 0, sizeof(Packet_t));
        } else if (byte == PKT_SYNC_0) {
            /* Stay in SYNC0 — could be repeated 0xAA */
        } else {
            ctx->state = PARSE_WAIT_SYNC0;
        }
        break;

    case PARSE_WAIT_TYPE:
        ctx->pkt.type = (PacketType_t)byte;
        ctx->state = PARSE_WAIT_ID;
        break;

    case PARSE_WAIT_ID:
        ctx->pkt.id = byte;
        ctx->state = PARSE_WAIT_LEN_LO;
        break;

    case PARSE_WAIT_LEN_LO:
        ctx->pkt.payload_len = byte;          /* low byte */
        ctx->state = PARSE_WAIT_LEN_HI;
        break;

    case PARSE_WAIT_LEN_HI:
        ctx->pkt.payload_len |= (uint16_t)byte << 8;  /* high byte */
        if (ctx->pkt.payload_len > PKT_MAX_PAYLOAD) {
            /* Invalid length — discard and re-sync */
            ctx->state = PARSE_WAIT_SYNC0;
        } else if (ctx->pkt.payload_len == 0) {
            ctx->state = PARSE_WAIT_CRC_LO;
        } else {
            ctx->payload_idx = 0;
            ctx->state = PARSE_WAIT_PAYLOAD;
        }
        break;

    case PARSE_WAIT_PAYLOAD:
        ctx->pkt.payload[ctx->payload_idx++] = byte;
        if (ctx->payload_idx >= ctx->pkt.payload_len)
            ctx->state = PARSE_WAIT_CRC_LO;
        break;

    case PARSE_WAIT_CRC_LO:
        ctx->pkt.crc = byte;
        ctx->state = PARSE_WAIT_CRC_HI;
        break;

    case PARSE_WAIT_CRC_HI:
        ctx->pkt.crc |= (uint16_t)byte << 8;
        ctx->state = PARSE_WAIT_END;
        break;

    case PARSE_WAIT_END:
        ctx->state = PARSE_WAIT_SYNC0;     /* reset for next packet */
        if (byte == PKT_END_MARKER) {
            /* Verify CRC: computed over TYPE + ID + LEN(2) + PAYLOAD */
            uint8_t crc_buf[4 + PKT_MAX_PAYLOAD];
            crc_buf[0] = (uint8_t)ctx->pkt.type;
            crc_buf[1] = ctx->pkt.id;
            crc_buf[2] = (uint8_t)(ctx->pkt.payload_len & 0xFF);
            crc_buf[3] = (uint8_t)(ctx->pkt.payload_len >> 8);
            if (ctx->pkt.payload_len > 0)
                memcpy(&crc_buf[4], ctx->pkt.payload, ctx->pkt.payload_len);

            uint16_t calc_crc = Protocol_CRC16(crc_buf,
                                               4 + ctx->pkt.payload_len);

            ctx->pkt.valid = (calc_crc == ctx->pkt.crc);
            return true;   /* packet ready (check .valid for CRC result) */
        }
        /* else: bad end marker — packet discarded */
        break;
    }

    return false;
}

/* ================================================================
 *  Packet Builder
 * ================================================================ */
uint16_t Protocol_BuildPacket(ProtocolParser_t *ctx,
                              PacketType_t type,
                              const uint8_t *payload,
                              uint16_t payload_len,
                              uint8_t *out_buf)
{
    if (payload_len > PKT_MAX_PAYLOAD) return 0;

    uint16_t idx = 0;

    /* SYNC */
    out_buf[idx++] = PKT_SYNC_0;
    out_buf[idx++] = PKT_SYNC_1;

    /* TYPE + ID + LEN */
    out_buf[idx++] = (uint8_t)type;
    out_buf[idx++] = ctx->seq_counter++;
    out_buf[idx++] = (uint8_t)(payload_len & 0xFF);
    out_buf[idx++] = (uint8_t)(payload_len >> 8);

    /* PAYLOAD */
    if (payload_len > 0 && payload != NULL) {
        memcpy(&out_buf[idx], payload, payload_len);
        idx += payload_len;
    }

    /* CRC16 over TYPE + ID + LEN + PAYLOAD (bytes 2..idx-1 of out_buf) */
    uint16_t crc = Protocol_CRC16(&out_buf[2], 4 + payload_len);
    out_buf[idx++] = (uint8_t)(crc & 0xFF);
    out_buf[idx++] = (uint8_t)(crc >> 8);

    /* END */
    out_buf[idx++] = PKT_END_MARKER;

    return idx;
}

uint16_t Protocol_BuildACK(ProtocolParser_t *ctx, uint8_t ack_id,
                           uint8_t *out_buf)
{
    uint8_t payload[1] = { ack_id };
    return Protocol_BuildPacket(ctx, PKT_TYPE_ACK, payload, 1, out_buf);
}

uint16_t Protocol_BuildNACK(ProtocolParser_t *ctx, uint8_t nack_id,
                            uint8_t reason, uint8_t *out_buf)
{
    uint8_t payload[2] = { nack_id, reason };
    return Protocol_BuildPacket(ctx, PKT_TYPE_NACK, payload, 2, out_buf);
}
