#ifndef TRACKER_PROTOCOL_H
#define TRACKER_PROTOCOL_H

#include <stdint.h>

#define UART_FRAME_HEADER   0xAA
#define UART_FRAME_TAIL     0x55

typedef enum {
    CMD_ANGLE_DATA   = 0x01,
    CMD_TRACK_STATE  = 0x02,
    CMD_FACE_INFO    = 0x03,
} tracker_cmd_t;

#pragma pack(push, 1)
typedef struct {
    uint8_t  header;
    uint8_t  cmd;
    uint8_t  len;
    uint8_t  data[16];
    uint8_t  crc8;
    uint8_t  tail;
} tracker_frame_t;
#pragma pack(pop)

uint8_t tracker_crc8(const uint8_t *data, uint8_t len);

#endif /* TRACKER_PROTOCOL_H */
