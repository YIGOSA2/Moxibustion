#include "app_ble.h"

#include <string.h>
#include "eeprom.h"

static uint32_t s_clock_epoch_base = 0U;
static uint32_t s_clock_tick_base = 0U;

static uint32_t app_ble_get_epoch(void)
{
  uint32_t now_tick;
  uint32_t elapsed_sec;

  if (s_clock_epoch_base == 0U)
  {
    return 0U;
  }

  now_tick = HAL_GetTick();
  elapsed_sec = (now_tick - s_clock_tick_base) / 1000U;
  return s_clock_epoch_base + elapsed_sec;
}

static void app_ble_send_status_frame(const AppState *state)
{
  BLE_StatusPayload status;

  memset(&status, 0, sizeof(status));
  status.state = state->state;
  status.mode = state->mode;
  status.temp_now = state->temp_now;
  status.temp_set = state->temp_set;
  status.pressure_present = state->pressure_present;
  status.pressure_adc = state->pressure_adc;
  status.ble_connected = 1U;
  status.minutes_left = state->minutes_left;
  status.fault_code = state->fault_code;
  status.nfc_present = state->nfc_present;
  status.nfc_fault = state->nfc_fault;
  status.nfc_uid_len = state->nfc_uid_len;
  memcpy(status.nfc_uid, state->nfc_uid, sizeof(status.nfc_uid));
  status.nfc_user_state = state->nfc_user_state;
  status.nfc_user_slot = state->nfc_user_slot;
  status.nfc_enroll_pending = state->nfc_enroll_pending;
  status.nfc_save_result = state->nfc_save_result;
  status.nfc_fw_ok = state->nfc_fw_ok;
  status.nfc_fw_ic = state->nfc_fw_ic;
  status.nfc_fw_ver = state->nfc_fw_ver;
  status.nfc_fw_rev = state->nfc_fw_rev;
  status.nfc_fw_support = state->nfc_fw_support;
  BLE_Drv_GetDebugStats(&status.ble_rx_count, &status.ble_last_rx_byte,
                        &status.ble_frame_count, &status.ble_last_cmd,
                        &status.ble_uart_error_flags);

  (void)BLE_Drv_SendStatus(&status);
}

void AppBle_Init(UART_HandleTypeDef *huart)
{
  BLE_Drv_Init(huart);
}

void AppBle_Process(void)
{
  BLE_Drv_Process();
}

uint32_t AppBle_GetEpoch(void)
{
  return app_ble_get_epoch();
}

void AppBle_SendStatus(const AppState *state)
{
  if (state == NULL)
  {
    return;
  }

  app_ble_send_status_frame(state);
}

void AppBle_ProcessCommands(AppState *state, HAL_StatusTypeDef (*save_fn)(const AppState *state),
                            const AppBle_Callbacks *callbacks)
{
  BLE_Frame frame;
  uint8_t ack;
  uint32_t epoch;
  uint8_t count;
  uint8_t index;
  uint32_t start_epoch;
  uint32_t end_epoch;
  uint16_t duration_min;
  EEPROM_SessionRecord_t session;

  if (state == NULL)
  {
    return;
  }

  while (BLE_Drv_GetNextCommand(&frame) != 0U)
  {
    ack = BLE_ACK_OK;

    switch (frame.cmd)
    {
      case BLE_CMD_SET_CLOCK:
        if (frame.payload_len == 4U)
        {
          epoch = ((uint32_t)frame.payload[0] << 24) | ((uint32_t)frame.payload[1] << 16) |
                  ((uint32_t)frame.payload[2] << 8) | (uint32_t)frame.payload[3];
          s_clock_epoch_base = epoch;
          s_clock_tick_base = HAL_GetTick();
        }
        else
        {
          ack = BLE_ACK_INVALID_PARAM;
        }
        (void)BLE_Drv_SendAck(frame.cmd, ack);
        break;

      case BLE_CMD_GET_SESSION_COUNT:
        count = (callbacks != NULL && callbacks->get_session_count_fn != NULL)
                    ? callbacks->get_session_count_fn()
                    : EEPROM_GetSessionCount();
        (void)BLE_Drv_SendAck(frame.cmd, BLE_ACK_OK);
        (void)BLE_Drv_SendSessionCount(count);
        break;

      case BLE_CMD_GET_SESSION:
        if (frame.payload_len == 1U)
        {
          index = frame.payload[0];
          if (callbacks != NULL && callbacks->get_session_fn != NULL)
          {
            if (callbacks->get_session_fn(index, &start_epoch, &end_epoch, &duration_min) != 0U)
            {
              count = (callbacks->get_session_count_fn != NULL)
                          ? callbacks->get_session_count_fn()
                          : EEPROM_GetSessionCount();
              (void)BLE_Drv_SendAck(frame.cmd, BLE_ACK_OK);
              (void)BLE_Drv_SendSession(index, count, start_epoch, end_epoch, duration_min);
            }
            else
            {
              ack = BLE_ACK_INVALID_PARAM;
              (void)BLE_Drv_SendAck(frame.cmd, ack);
            }
          }
          else
          {
            if (EEPROM_GetSessionByIndex(index, &session) != 0U)
            {
              count = EEPROM_GetSessionCount();
              (void)BLE_Drv_SendAck(frame.cmd, BLE_ACK_OK);
              (void)BLE_Drv_SendSession(index, count, session.start_epoch,
                                        session.end_epoch, session.duration_min);
            }
            else
            {
              ack = BLE_ACK_INVALID_PARAM;
              (void)BLE_Drv_SendAck(frame.cmd, ack);
            }
          }
        }
        else
        {
          ack = BLE_ACK_INVALID_PARAM;
          (void)BLE_Drv_SendAck(frame.cmd, ack);
        }
        break;

      case BLE_CMD_CLEAR_SESSIONS:
        if (callbacks != NULL && callbacks->clear_sessions_fn != NULL)
        {
          callbacks->clear_sessions_fn();
        }
        else
        {
          EEPROM_ClearSessionLog();
        }
        (void)BLE_Drv_SendAck(frame.cmd, BLE_ACK_OK);
        break;

      default:
        AppState_ApplyCommand(state, &frame, &ack);

        if ((ack == BLE_ACK_OK) && (save_fn != NULL) &&
            ((frame.cmd == BLE_CMD_SET_TEMP) || (frame.cmd == BLE_CMD_SET_TIME)))
        {
          if (save_fn(state) != HAL_OK)
          {
            ack = BLE_ACK_INTERNAL_ERROR;
          }
        }

        (void)BLE_Drv_SendAck(frame.cmd, ack);
        app_ble_send_status_frame(state);
        break;
    }
  }
}