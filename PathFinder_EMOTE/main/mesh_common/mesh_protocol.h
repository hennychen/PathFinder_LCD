/**
 * @file mesh_protocol.h
 * @brief Shared message protocol for ESP-NOW + ESP-WIFI-MESH communication.
 *
 * Frame layout (compact, <250 bytes for ESP-NOW):
 *   [MSG_TYPE(1)][SEQ(1)][PAYLOAD_LEN(1)][PAYLOAD(0-248)][CRC8(1)]
 *
 * Total max size = 1 + 1 + 1 + 248 + 1 = 252 bytes (within ESP-NOW 250-byte
 * limit when payload <= 246; ESP-NOW max user payload is 250 bytes).
 *
 * Frame layout (extended, for Mesh P2P with optional larger payloads):
 *   Same format but payload can be up to 1500 bytes (MESH_MTU).
 */
#ifndef MESH_PROTOCOL_H
#define MESH_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── Mesh network shared configuration ── */
#define MESH_ID_BYTES           {0x18, 0xFE, 0x34, 0x00, 0x00, 0x01}
#define MESH_AP_PASSWD           "pathfinder_mesh_2026"
#define MESH_AP_CONNECTIONS      6
#define MESH_MAX_LAYER           2
#define MESH_CHANNEL             0   /* 0 = auto-follow router */

/* ── ESP-NOW constraints ── */
#define ESPNOW_MAX_PAYLOAD       246   /* 250 - 4 header/tail bytes */

/* ── Message types (extends tracker_cmd_t) ── */
typedef enum {
    /* Tracking data (from B -> A) */
    MSG_ANGLE_DATA     = 0x01,   /* Sound source angle: u16 fixed-point + valid flag */
    MSG_TRACK_STATE    = 0x02,   /* Tracker state machine state */
    MSG_FACE_INFO      = 0x03,   /* Face detection: cx, cy, w, h, found */
    /* Control commands (from A -> B) */
    MSG_SERVO_CTRL     = 0x04,   /* Pan/tilt servo angle override */
    MSG_MODE_SWITCH    = 0x05,   /* Force tracker mode */
    /* Heartbeat / management */
    MSG_HEARTBEAT      = 0x10,   /* Periodic heartbeat (B -> A, every 500ms) */
    MSG_MESH_READY     = 0x11,   /* Mesh link established notification */
    MSG_MESH_BYE       = 0x12,   /* Mesh link disconnecting */
    /* Sensor data (A -> B, 2Hz) — all A-board sensors in one compact packet */
    MSG_SENSOR_DATA    = 0x20,
    /* Conversation sync (B -> A) — dialog state, LLM emotion, subtitle text */
    MSG_DIALOG_STATE   = 0x21,  /* 1 byte: 0=idle 1=listening 2=speaking 3=connecting */
    MSG_EMOTION        = 0x22,  /* N bytes ASCII: emotion name (e.g. "happy", "neutral") */
    MSG_CHAT_TEXT      = 0x23,  /* N bytes UTF-8: subtitle text (≤246B per frame) */
} mesh_msg_type_t;

/* ── Sensor data packet (A-board → B-board, ~72 bytes) ── */
#pragma pack(push, 1)
typedef struct {
    /* AHT20 — temperature & humidity */
    float temperature;       /**< °C */
    float humidity;          /**< %RH */
    /* BMP280 — pressure & altitude */
    float pressure;          /**< Pa */
    float altitude;          /**< m */
    /* UV index */
    float uv_index;          /**< 0-11+ */
    /* MPU9250 — accel & gyro */
    float accel[3];          /**< g (x, y, z) */
    float gyro[3];           /**< °/s (x, y, z) */
    float imu_temp;          /**< °C */
    /* HMC5883L/QMC5883L — compass */
    float heading;           /**< 0-360° */
    float mag[3];            /**< µT (x, y, z) */
    /* validity flags */
    uint8_t flags;           /**< bit0:AHT20 bit1:BMP280 bit2:UV bit3:IMU bit4:Compass */
    uint32_t timestamp_ms;   /**< sender uptime (ms) */
} sensor_packet_t;
#pragma pack(pop)

/* ── Compact frame structure (matches ESP-NOW payload) ── */
#pragma pack(push, 1)
typedef struct {
    uint8_t  msg_type;
    uint8_t  seq;
    uint8_t  payload_len;
    uint8_t  payload[ESPNOW_MAX_PAYLOAD];
    /* CRC8 is appended after payload_len + payload bytes, not stored in struct */
} mesh_msg_t;
#pragma pack(pop)

/* ── CRC8 — polynomial 0x07, init 0x00 (CRC-8/SMBUS variant) ── */
uint8_t mesh_crc8(const uint8_t *data, uint8_t len);

/**
 * @brief Serialize a message into a flat buffer.
 * @param out      Output buffer (must be >= 4 + payload_len bytes)
 * @param msg_type Message type
 * @param seq      Sequence number (auto-incremented by sender)
 * @param payload  Payload data (NULL if payload_len == 0)
 * @param payload_len  Payload length (0..ESPNOW_MAX_PAYLOAD)
 * @return Total bytes written, or 0 on error.
 */
int mesh_msg_serialize(uint8_t *out, uint8_t msg_type, uint8_t seq,
                        const uint8_t *payload, uint8_t payload_len);

/**
 * @brief Parse a received buffer into a message structure.
 * @param buf      Received buffer
 * @param buf_len  Buffer length
 * @param out_msg  Parsed message (payload pointer points into buf)
 * @return true on success (CRC valid), false otherwise.
 */
bool mesh_msg_parse(const uint8_t *buf, int buf_len, mesh_msg_t *out_msg);

#endif /* MESH_PROTOCOL_H */
