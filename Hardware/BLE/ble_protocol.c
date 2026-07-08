#include "ble_protocol.h"

#include <string.h>

static uint8_t ble_protocol_xor(const uint8_t *buf, uint16_t len)
{
  uint8_t chk = 0U;
  uint16_t i;

  for (i = 0U; i < len; ++i)
  {
    chk ^= buf[i];
  }

  return chk;
}

uint8_t BLE_Protocol_BuildFrame(uint8_t cmd, const uint8_t *payload, uint8_t payload_len,
                                uint8_t *out_buf, uint8_t out_size)
{
  uint8_t frame_len;

  if ((out_buf == NULL) || (payload_len > BLE_FRAME_MAX_PAYLOAD))
  {
    return 0U;
  }

  frame_len = (uint8_t)(payload_len + 5U);
  if (out_size < frame_len)
  {
    return 0U;
  }

  out_buf[0] = BLE_FRAME_HEAD0;
  out_buf[1] = BLE_FRAME_HEAD1;
  out_buf[2] = (uint8_t)(payload_len + 2U);
  out_buf[3] = cmd;

  if ((payload != NULL) && (payload_len > 0U))
  {
    memcpy(&out_buf[4], payload, payload_len);
  }

  out_buf[4U + payload_len] = ble_protocol_xor(out_buf, (uint16_t)(4U + payload_len));

  return frame_len;
}

HAL_StatusTypeDef BLE_Protocol_TryParse(const uint8_t *buf, uint16_t len, BLE_Frame *frame)
{
  uint8_t payload_len;
  uint8_t expected_chk;

  if ((buf == NULL) || (frame == NULL) || (len < 5U))
  {
    return HAL_ERROR;
  }

  if ((buf[0] != BLE_FRAME_HEAD0) || (buf[1] != BLE_FRAME_HEAD1))
  {
    return HAL_ERROR;
  }

  if (buf[2] < 2U)
  {
    return HAL_ERROR;
  }

  if (len != (uint16_t)(buf[2] + 3U))
  {
    return HAL_ERROR;
  }

  payload_len = (uint8_t)(buf[2] - 2U);
  if (payload_len > BLE_FRAME_MAX_PAYLOAD)
  {
    return HAL_ERROR;
  }

  expected_chk = ble_protocol_xor(buf, (uint16_t)(len - 1U));
  if (expected_chk != buf[len - 1U])
  {
    return HAL_ERROR;
  }

  frame->cmd = buf[3];
  frame->payload_len = payload_len;

  if (payload_len > 0U)
  {
    memcpy(frame->payload, &buf[4], payload_len);
  }

  return HAL_OK;
}
