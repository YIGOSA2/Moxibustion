#ifndef APP_STATE_H
#define APP_STATE_H

#include "main.h"
#include "ble_protocol.h"

#define APP_NFC_UID_MAX_LEN 7U

typedef enum
{
  APP_STATE_IDLE = 0U,
  APP_STATE_PREHEAT = 1U,
  APP_STATE_KEEP_WARM = 2U,
  APP_STATE_PAUSE = 3U,
  APP_STATE_FINISHED = 4U,
  APP_STATE_FAULT = 5U
} AppRunState;

typedef struct
{
  uint8_t state;
  uint8_t mode;
  uint8_t temp_now;
  uint8_t temp_set;
  uint8_t pressure_present;
  uint16_t pressure_adc;
  uint16_t minutes_left;
  uint8_t fault_code;
  uint8_t nfc_present;
  uint8_t nfc_uid_len;
  uint8_t nfc_uid[APP_NFC_UID_MAX_LEN];
  uint8_t nfc_fault;
  uint8_t nfc_user_state;
  uint8_t nfc_user_slot;
  uint8_t nfc_enroll_pending;
  uint8_t nfc_save_result;
  uint8_t nfc_fw_ok;
  uint8_t nfc_fw_ic;
  uint8_t nfc_fw_ver;
  uint8_t nfc_fw_rev;
  uint8_t nfc_fw_support;
} AppState;

void AppState_Init(AppState *state);
void AppState_Start(AppState *state);
void AppState_Stop(AppState *state);
void AppState_ClearFault(AppState *state);
void AppState_UpdateMeasuredTemp(AppState *state, int16_t temp_x10, uint8_t sensor_fault);
void AppState_UpdateNfc(AppState *state, uint8_t present, const uint8_t *uid, uint8_t uid_len, uint8_t fault);
void AppState_ApplyCommand(AppState *state, const BLE_Frame *frame, uint8_t *ack);

#endif