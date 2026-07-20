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

/* ── Router SSID (REQUIRED for ALL nodes, including CHILD) ──
 * ESP-WIFI-MESH uses router SSID to identify the mesh network.
 * CHILD nodes need this to find the correct mesh, even though
 * they don't connect to the router directly.
 * ⚠️  This MUST match the SSID configured on Board A (ROOT).
 *     Change this to match your actual router SSID before building.
 */
#define MESH_ROUTER_SSID         "PathFinder_2.4G"

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
} mesh_msg_type_t;

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
