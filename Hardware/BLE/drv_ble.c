#include "drv_ble.h"

#include <string.h>

#define BLE_RX_BUF_SIZE 64U
#define BLE_RX_RING_SIZE 128U
#define BLE_UART_ERR_PE 0x01U
#define BLE_UART_ERR_FE 0x02U
#define BLE_UART_ERR_NE 0x04U
#define BLE_UART_ERR_ORE 0x08U
#define BLE_UART_ERR_RING_OVF 0x10U

static UART_HandleTypeDef *s_ble_uart;
static uint8_t s_rx_buf[BLE_RX_BUF_SIZE];
static uint8_t s_rx_len;
static BLE_Frame s_pending_frame;
static uint8_t s_has_pending_frame;
static uint8_t s_rx_debug_count;
static uint8_t s_rx_debug_last_byte;
static uint8_t s_rx_debug_frame_count;
static uint8_t s_rx_debug_last_cmd;
static uint8_t s_rx_debug_uart_error_flags;
static uint8_t s_rx_it_byte;
static volatile uint8_t s_rx_ring[BLE_RX_RING_SIZE];
static volatile uint16_t s_rx_ring_head;
static volatile uint16_t s_rx_ring_tail;

static HAL_StatusTypeDef ble_send_frame(uint8_t cmd, const uint8_t *payload, uint8_t payload_len);
static void ble_start_receive_it(void);
static void ble_capture_uart_errors(void);
static uint8_t ble_ring_push(uint8_t byte);
static uint8_t ble_ring_pop(uint8_t *out_byte);

void BLE_Drv_Init(UART_HandleTypeDef *huart)
{
  s_ble_uart = huart;
  s_rx_len = 0U;
  s_has_pending_frame = 0U;
  s_rx_debug_count = 0U;
  s_rx_debug_last_byte = 0U;
  s_rx_debug_frame_count = 0U;
  s_rx_debug_last_cmd = 0U;
  s_rx_debug_uart_error_flags = 0U;
  s_rx_it_byte = 0U;
  s_rx_ring_head = 0U;
  s_rx_ring_tail = 0U;
  memset(&s_pending_frame, 0, sizeof(s_pending_frame));
  ble_start_receive_it();
}

void BLE_Drv_Process(void)
{
  uint8_t ch;

  if ((s_ble_uart == NULL) || (s_has_pending_frame != 0U))
  {
    return;
  }

  while (ble_ring_pop(&ch) != 0U)
  {
    s_rx_debug_count++;
    s_rx_debug_last_byte = ch;

    if ((s_rx_len == 0U) && (ch != BLE_FRAME_HEAD0))
    {
      continue;
    }

    if ((s_rx_len == 1U) && (ch != BLE_FRAME_HEAD1))
    {
      s_rx_len = 0U;
      continue;
    }

    if (s_rx_len < BLE_RX_BUF_SIZE)
    {
      s_rx_buf[s_rx_len++] = ch;
    }
    else
    {
      s_rx_len = 0U;
      continue;
    }

    if ((s_rx_len >= 3U) && (s_rx_buf[2] < 2U))
    {
      s_rx_len = 0U;
      continue;
    }

    if ((s_rx_len >= 3U) && (s_rx_len == (uint8_t)(s_rx_buf[2] + 3U)))
    {
      if (BLE_Protocol_TryParse(s_rx_buf, s_rx_len, &s_pending_frame) == HAL_OK)
      {
        s_has_pending_frame = 1U;
        s_rx_debug_frame_count++;
        s_rx_debug_last_cmd = s_pending_frame.cmd;
      }
      s_rx_len = 0U;
      break;
    }
  }
}

uint8_t BLE_Drv_GetNextCommand(BLE_Frame *frame)
{
  if ((frame == NULL) || (s_has_pending_frame == 0U))
  {
    return 0U;
  }

  *frame = s_pending_frame;
  s_has_pending_frame = 0U;
  return 1U;
}

HAL_StatusTypeDef BLE_Drv_SendAck(uint8_t req_cmd, uint8_t result)
{
  uint8_t payload[2];

  payload[0] = req_cmd;
  payload[1] = result;

  return ble_send_frame(BLE_RSP_ACK, payload, sizeof(payload));
}

HAL_StatusTypeDef BLE_Drv_SendStatus(const BLE_StatusPayload *status)
{
  uint8_t payload[38];

  if (status == NULL)
  {
    return HAL_ERROR;
  }

  memset(payload, 0, sizeof(payload));
  payload[0] = status->state;
  payload[1] = status->mode;
  payload[2] = status->temp_now;
  payload[3] = status->temp_set;
  payload[4] = status->pressure_present;
  payload[5] = status->ble_connected;
  payload[6] = (uint8_t)(status->minutes_left >> 8);
  payload[7] = (uint8_t)(status->minutes_left & 0xFFU);
  payload[8] = status->fault_code;
  payload[9] = status->nfc_present;
  payload[10] = status->nfc_fault;
  payload[11] = status->nfc_uid_len;
  memcpy(&payload[12], status->nfc_uid, 7U);
  payload[19] = status->nfc_user_state;
  payload[20] = status->nfc_user_slot;
  payload[21] = status->nfc_enroll_pending;
  payload[22] = status->nfc_save_result;
  payload[23] = status->nfc_diag_stage;
  payload[24] = status->nfc_diag_hal;
  payload[25] = (uint8_t)(status->nfc_diag_i2c_error & 0xFFU);
  payload[26] = (uint8_t)((status->nfc_diag_i2c_error >> 8) & 0xFFU);
  payload[27] = (uint8_t)((status->nfc_diag_i2c_error >> 16) & 0xFFU);
  payload[28] = (uint8_t)((status->nfc_diag_i2c_error >> 24) & 0xFFU);
  payload[29] = status->nfc_fw_ok;
  payload[30] = status->nfc_fw_ic;
  payload[31] = status->nfc_fw_ver;
  payload[32] = status->nfc_fw_rev;
  payload[33] = status->ble_rx_count;
  payload[34] = status->ble_last_rx_byte;
  payload[35] = status->ble_frame_count;
  payload[36] = status->ble_last_cmd;
  payload[37] = status->ble_uart_error_flags;

  return ble_send_frame(BLE_RSP_STATUS, payload, sizeof(payload));
}

HAL_StatusTypeDef BLE_Drv_SendFault(uint8_t fault_code)
{
  return ble_send_frame(BLE_RSP_FAULT, &fault_code, 1U);
}

HAL_StatusTypeDef BLE_Drv_SendSessionCount(uint8_t count)
{
  return ble_send_frame(BLE_RSP_SESSION_COUNT, &count, 1U);
}

HAL_StatusTypeDef BLE_Drv_SendSession(uint8_t index, uint8_t total,
                                      uint32_t start_epoch, uint32_t end_epoch,
                                      uint16_t duration_min)
{
  uint8_t payload[12];

  payload[0] = index;
  payload[1] = total;
  payload[2] = (uint8_t)((start_epoch >> 24) & 0xFFU);
  payload[3] = (uint8_t)((start_epoch >> 16) & 0xFFU);
  payload[4] = (uint8_t)((start_epoch >> 8) & 0xFFU);
  payload[5] = (uint8_t)(start_epoch & 0xFFU);
  payload[6] = (uint8_t)((end_epoch >> 24) & 0xFFU);
  payload[7] = (uint8_t)((end_epoch >> 16) & 0xFFU);
  payload[8] = (uint8_t)((end_epoch >> 8) & 0xFFU);
  payload[9] = (uint8_t)(end_epoch & 0xFFU);
  payload[10] = (uint8_t)((duration_min >> 8) & 0xFFU);
  payload[11] = (uint8_t)(duration_min & 0xFFU);

  return ble_send_frame(BLE_RSP_SESSION, payload, sizeof(payload));
}

void BLE_Drv_GetDebugStats(uint8_t *rx_count, uint8_t *last_rx_byte,
                           uint8_t *frame_count, uint8_t *last_cmd,
                           uint8_t *uart_error_flags)
{
  if (rx_count != NULL)
  {
    *rx_count = s_rx_debug_count;
  }

  if (last_rx_byte != NULL)
  {
    *last_rx_byte = s_rx_debug_last_byte;
  }

  if (frame_count != NULL)
  {
    *frame_count = s_rx_debug_frame_count;
  }

  if (last_cmd != NULL)
  {
    *last_cmd = s_rx_debug_last_cmd;
  }

  if (uart_error_flags != NULL)
  {
    *uart_error_flags = s_rx_debug_uart_error_flags;
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((s_ble_uart == NULL) || (huart != s_ble_uart))
  {
    return;
  }

  (void)ble_ring_push(s_rx_it_byte);
  ble_start_receive_it();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((s_ble_uart == NULL) || (huart != s_ble_uart))
  {
    return;
  }

  ble_capture_uart_errors();
  huart->ErrorCode = HAL_UART_ERROR_NONE;
  ble_start_receive_it();
}

static void ble_start_receive_it(void)
{
  if (s_ble_uart == NULL)
  {
    return;
  }

  if (s_ble_uart->RxState == HAL_UART_STATE_READY)
  {
    (void)HAL_UART_Receive_IT(s_ble_uart, &s_rx_it_byte, 1U);
  }
}

static void ble_capture_uart_errors(void)
{
  uint32_t sr;
  uint8_t flags = 0U;

  if (s_ble_uart == NULL)
  {
    return;
  }

  sr = s_ble_uart->Instance->SR;
  if ((sr & USART_SR_PE) != 0U)
  {
    flags |= BLE_UART_ERR_PE;
  }
  if ((sr & USART_SR_FE) != 0U)
  {
    flags |= BLE_UART_ERR_FE;
  }
  if ((sr & USART_SR_NE) != 0U)
  {
    flags |= BLE_UART_ERR_NE;
  }
  if ((sr & USART_SR_ORE) != 0U)
  {
    flags |= BLE_UART_ERR_ORE;
  }

  if (flags != 0U)
  {
    s_rx_debug_uart_error_flags |= flags;
    __HAL_UART_CLEAR_PEFLAG(s_ble_uart);
  }
}

static uint8_t ble_ring_push(uint8_t byte)
{
  uint16_t next_head;

  next_head = (uint16_t)((s_rx_ring_head + 1U) % BLE_RX_RING_SIZE);
  if (next_head == s_rx_ring_tail)
  {
    s_rx_debug_uart_error_flags |= BLE_UART_ERR_RING_OVF;
    return 0U;
  }

  s_rx_ring[s_rx_ring_head] = byte;
  s_rx_ring_head = next_head;
  return 1U;
}

static uint8_t ble_ring_pop(uint8_t *out_byte)
{
  if ((out_byte == NULL) || (s_rx_ring_tail == s_rx_ring_head))
  {
    return 0U;
  }

  *out_byte = s_rx_ring[s_rx_ring_tail];
  s_rx_ring_tail = (uint16_t)((s_rx_ring_tail + 1U) % BLE_RX_RING_SIZE);
  return 1U;
}

static HAL_StatusTypeDef ble_send_frame(uint8_t cmd, const uint8_t *payload, uint8_t payload_len)
{
  uint8_t frame_buf[BLE_FRAME_MAX_SIZE];
  uint8_t frame_len;

  if (s_ble_uart == NULL)
  {
    return HAL_ERROR;
  }

  frame_len = BLE_Protocol_BuildFrame(cmd, payload, payload_len, frame_buf, sizeof(frame_buf));
  if (frame_len == 0U)
  {
    return HAL_ERROR;
  }

  return HAL_UART_Transmit(s_ble_uart, frame_buf, frame_len, 100U);
}