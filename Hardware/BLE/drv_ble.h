#ifndef DRV_BLE_H
#define DRV_BLE_H

#include "ble_protocol.h"

typedef struct
{
  uint8_t state;
  uint8_t mode;
  uint8_t temp_now;
  uint8_t temp_set;
  uint8_t pressure_present;
  uint16_t pressure_adc;
  uint8_t ble_connected;
  uint16_t minutes_left;
  uint8_t fault_code;
  uint8_t nfc_present;
  uint8_t nfc_fault;
  uint8_t nfc_uid_len;
  uint8_t nfc_uid[7U];
  uint8_t nfc_user_state;
  uint8_t nfc_user_slot;
  uint8_t nfc_enroll_pending;
  uint8_t nfc_save_result;
  uint8_t nfc_diag_stage;
  uint8_t nfc_diag_hal;
  uint32_t nfc_diag_i2c_error;
  uint8_t nfc_fw_ok;
  uint8_t nfc_fw_ic;
  uint8_t nfc_fw_ver;
  uint8_t nfc_fw_rev;
  uint8_t nfc_fw_support;
  uint8_t ble_rx_count;
  uint8_t ble_last_rx_byte;
  uint8_t ble_frame_count;
  uint8_t ble_last_cmd;
  uint8_t ble_uart_error_flags;
} BLE_StatusPayload;

void BLE_Drv_Init(UART_HandleTypeDef *huart);
void BLE_Drv_Process(void);
uint8_t BLE_Drv_GetNextCommand(BLE_Frame *frame);
HAL_StatusTypeDef BLE_Drv_SendAck(uint8_t req_cmd, uint8_t result);
HAL_StatusTypeDef BLE_Drv_SendStatus(const BLE_StatusPayload *status);
HAL_StatusTypeDef BLE_Drv_SendFault(uint8_t fault_code);
HAL_StatusTypeDef BLE_Drv_SendSessionCount(uint8_t count);
HAL_StatusTypeDef BLE_Drv_SendSession(uint8_t index, uint8_t total,
                                      uint32_t start_epoch, uint32_t end_epoch,
                                      uint16_t duration_min);
void BLE_Drv_GetDebugStats(uint8_t *rx_count, uint8_t *last_rx_byte,
                           uint8_t *frame_count, uint8_t *last_cmd,
                           uint8_t *uart_error_flags);

#endif