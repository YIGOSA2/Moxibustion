#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include "main.h"

#define BLE_FRAME_HEAD0        0xAAU
#define BLE_FRAME_HEAD1        0x55U
#define BLE_FRAME_MAX_PAYLOAD  64U
#define BLE_FRAME_MAX_SIZE     (BLE_FRAME_MAX_PAYLOAD + 5U)

typedef enum
{
  BLE_CMD_SET_TEMP = 0x01,
  BLE_CMD_SET_MODE = 0x02,
  BLE_CMD_SET_TIME = 0x03,
  BLE_CMD_START = 0x04,
  BLE_CMD_PAUSE = 0x05,
  BLE_CMD_STOP = 0x06,
  BLE_CMD_GET_STATUS = 0x07,
  BLE_CMD_CLEAR_FAULT = 0x08,
  BLE_CMD_ENROLL_NFC_USER = 0x09,
  BLE_CMD_SET_CLOCK = 0x0A,
  BLE_CMD_GET_SESSION_COUNT = 0x0B,
  BLE_CMD_GET_SESSION = 0x0C,
  BLE_CMD_CLEAR_SESSIONS = 0x0D,
  BLE_RSP_ACK = 0x81,
  BLE_RSP_STATUS = 0x90,
  BLE_RSP_FAULT = 0x91,
  BLE_RSP_SESSION_COUNT = 0x92,
  BLE_RSP_SESSION = 0x93
} BLE_Command;

typedef enum
{
  BLE_ACK_OK = 0,
  BLE_ACK_INVALID_PARAM = 1,
  BLE_ACK_INVALID_STATE = 2,
  BLE_ACK_BUSY = 3,
  BLE_ACK_NOT_ALLOWED = 4,
  BLE_ACK_INTERNAL_ERROR = 5
} BLE_AckResult;

typedef struct
{
  uint8_t cmd;
  uint8_t payload_len;
  uint8_t payload[BLE_FRAME_MAX_PAYLOAD];
} BLE_Frame;

uint8_t BLE_Protocol_BuildFrame(uint8_t cmd, const uint8_t *payload, uint8_t payload_len,
                                uint8_t *out_buf, uint8_t out_size);
HAL_StatusTypeDef BLE_Protocol_TryParse(const uint8_t *buf, uint16_t len, BLE_Frame *frame);

#endif
