/**
 * @file mesh_protocol.c
 * @brief Shared message protocol implementation.
 */
#include "mesh_protocol.h"

uint8_t mesh_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

int mesh_msg_serialize(uint8_t *out, uint8_t msg_type, uint8_t seq,
                        const uint8_t *payload, uint8_t payload_len)
{
    if (!out || payload_len > ESPNOW_MAX_PAYLOAD) {
        return 0;
    }

    uint8_t idx = 0;
    out[idx++] = msg_type;
    out[idx++] = seq;
    out[idx++] = payload_len;

    if (payload_len > 0 && payload) {
        memcpy(&out[idx], payload, payload_len);
        idx += payload_len;
    }

    /* CRC8 covers msg_type + seq + payload_len + payload */
    out[idx++] = mesh_crc8(out, 3 + payload_len);

    return idx;
}

bool mesh_msg_parse(const uint8_t *buf, int buf_len, mesh_msg_t *out_msg)
{
    if (!buf || !out_msg || buf_len < 4) {
        return false;  /* Minimum: type(1) + seq(1) + len(1) + crc(1) = 4 */
    }

    out_msg->msg_type   = buf[0];
    out_msg->seq        = buf[1];
    out_msg->payload_len = buf[2];

    /* Validate payload length against buffer */
    int expected_len = 3 + out_msg->payload_len + 1;  /* header(3) + payload + crc(1) */
    if (expected_len > buf_len) {
        return false;
    }

    if (out_msg->payload_len > ESPNOW_MAX_PAYLOAD) {
        return false;
    }

    if (out_msg->payload_len > 0) {
        memcpy(out_msg->payload, &buf[3], out_msg->payload_len);
    }

    /* Verify CRC8 */
    uint8_t expected_crc = mesh_crc8(buf, 3 + out_msg->payload_len);
    if (expected_crc != buf[3 + out_msg->payload_len]) {
        return false;
    }

    return true;
}
