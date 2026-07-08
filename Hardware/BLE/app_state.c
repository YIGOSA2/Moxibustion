#include "app_state.h"

#include <string.h>

#define APP_TIME_MINUTES_DEFAULT 30U
#define APP_TIME_MINUTES_MIN     0U
#define APP_TIME_MINUTES_MAX     65535U
#define APP_TEMP_OVER_C          65U
#define APP_TEMP_HARD_STOP_C     70U
#define APP_TEMP_READY_C         1U

static void app_state_trigger_fault(AppState *state, uint8_t fault_code)
{
  state->fault_code = fault_code;
  state->state = APP_STATE_FAULT;
}

void AppState_Init(AppState *state)
{
  if (state == NULL)
  {
    return;
  }

  memset(state, 0, sizeof(*state));
  state->state = APP_STATE_IDLE;
  state->mode = 1U;
  state->temp_set = 45U;
  state->minutes_left = APP_TIME_MINUTES_DEFAULT;
}

void AppState_Start(AppState *state)
{
  if ((state == NULL) || (state->fault_code != 0U))
  {
    return;
  }

  state->state = APP_STATE_PREHEAT;
}

void AppState_Stop(AppState *state)
{
  if (state == NULL)
  {
    return;
  }

  state->state = APP_STATE_IDLE;
}

void AppState_ClearFault(AppState *state)
{
  if (state == NULL)
  {
    return;
  }

  state->fault_code = 0U;
  if (state->state == APP_STATE_FAULT)
  {
    state->state = APP_STATE_IDLE;
  }
}

void AppState_UpdateMeasuredTemp(AppState *state, int16_t temp_x10, uint8_t sensor_fault)
{
  uint8_t temp_c;

  if (state == NULL)
  {
    return;
  }

  if (sensor_fault != 0U)
  {
    app_state_trigger_fault(state, 2U);
    return;
  }

  if (temp_x10 <= 0)
  {
    temp_c = 0U;
  }
  else if (temp_x10 >= 2550)
  {
    temp_c = 255U;
  }
  else
  {
    temp_c = (uint8_t)((temp_x10 + 5) / 10);
  }

  state->temp_now = temp_c;

  if (state->temp_now >= APP_TEMP_HARD_STOP_C)
  {
    app_state_trigger_fault(state, 5U);
    return;
  }

  if (state->temp_now >= APP_TEMP_OVER_C)
  {
    app_state_trigger_fault(state, 4U);
    return;
  }

  if ((state->state == APP_STATE_IDLE) || (state->state == APP_STATE_PAUSE) ||
      (state->state == APP_STATE_FINISHED) || (state->state == APP_STATE_FAULT))
  {
    return;
  }

  if ((uint8_t)(state->temp_now + APP_TEMP_READY_C) < state->temp_set)
  {
    state->state = APP_STATE_PREHEAT;
  }
  else
  {
    state->state = APP_STATE_KEEP_WARM;
  }
}

void AppState_UpdateNfc(AppState *state, uint8_t present, const uint8_t *uid, uint8_t uid_len, uint8_t fault)
{
  uint8_t copy_len;

  if (state == NULL)
  {
    return;
  }

  state->nfc_present = present ? 1U : 0U;
  state->nfc_fault = fault ? 1U : 0U;
  copy_len = uid_len;
  if (copy_len > APP_NFC_UID_MAX_LEN)
  {
    copy_len = APP_NFC_UID_MAX_LEN;
  }
  state->nfc_uid_len = state->nfc_present ? copy_len : 0U;
  memset(state->nfc_uid, 0, sizeof(state->nfc_uid));
  if ((uid != NULL) && (state->nfc_uid_len > 0U))
  {
    memcpy(state->nfc_uid, uid, state->nfc_uid_len);
  }
}

static uint8_t app_state_can_set_param(const AppState *state)
{
  return ((state->state == APP_STATE_IDLE) || (state->state == APP_STATE_FINISHED)) ? 1U : 0U;
}

void AppState_ApplyCommand(AppState *state, const BLE_Frame *frame, uint8_t *ack)
{
  uint16_t minutes;

  if ((state == NULL) || (frame == NULL) || (ack == NULL))
  {
    return;
  }

  *ack = BLE_ACK_OK;

  switch (frame->cmd)
  {
    case BLE_CMD_SET_TEMP:
      if ((frame->payload_len != 1U) || (frame->payload[0] < 20U) || (frame->payload[0] > 50U))
      {
        *ack = BLE_ACK_INVALID_PARAM;
      }
      else if (app_state_can_set_param(state) == 0U)
      {
        *ack = BLE_ACK_BUSY;
      }
      else
      {
        state->temp_set = frame->payload[0];
      }
      break;

    case BLE_CMD_SET_TIME:
      if (frame->payload_len != 2U)
      {
        *ack = BLE_ACK_INVALID_PARAM;
      }
      else if (app_state_can_set_param(state) == 0U)
      {
        *ack = BLE_ACK_BUSY;
      }
      else
      {
        minutes = (uint16_t)(((uint16_t)frame->payload[0] << 8) | frame->payload[1]);
        if ((minutes < APP_TIME_MINUTES_MIN) || (minutes > APP_TIME_MINUTES_MAX))
        {
          *ack = BLE_ACK_INVALID_PARAM;
        }
        else
        {
          state->minutes_left = minutes;
        }
      }
      break;

    case BLE_CMD_START:
      if (state->fault_code != 0U)
      {
        *ack = BLE_ACK_NOT_ALLOWED;
      }
      else if ((state->state == APP_STATE_IDLE) || (state->state == APP_STATE_PAUSE) ||
               (state->state == APP_STATE_FINISHED))
      {
        AppState_Start(state);
      }
      else
      {
        *ack = BLE_ACK_BUSY;
      }
      break;

    case BLE_CMD_PAUSE:
      if ((state->state == APP_STATE_PREHEAT) || (state->state == APP_STATE_KEEP_WARM))
      {
        state->state = APP_STATE_PAUSE;
      }
      else
      {
        *ack = BLE_ACK_INVALID_STATE;
      }
      break;

    case BLE_CMD_STOP:
      if (state->state == APP_STATE_IDLE)
      {
        *ack = BLE_ACK_INVALID_STATE;
      }
      else
      {
        AppState_Stop(state);
      }
      break;

    case BLE_CMD_GET_STATUS:
      break;

    default:
      *ack = BLE_ACK_NOT_ALLOWED;
      break;
  }
}