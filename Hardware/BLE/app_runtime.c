#include "app_runtime.h"

#include <string.h>

#include "app_ble.h"
#include "eeprom.h"
#include "gui.h"
#include "heater.h"
#include "nfc.h"
#include "pressure.h"
#include "T.h"
#include "usart.h"
#include "voice.h"

#define STATUS_PERIOD_MS 1000U
#define TEMP_PERIOD_MS   500U

static AppState s_app_state;
static EEPROM_CardRecord_t s_card_record;
static uint8_t s_uid[7];
static uint8_t s_uid_len;
static uint8_t s_run_active;
static uint8_t s_temp_valid;
static int16_t s_temp_x10;
static uint32_t s_run_remaining_seconds;
static uint32_t s_last_temp_tick;
static uint32_t s_last_second_tick;
static uint32_t s_last_status_tick;
static uint8_t s_config_temp;
static uint16_t s_config_work_time;
static uint16_t s_config_sit_time;

static HAL_StatusTypeDef AppParams_Save(const AppState *state);
static void AppRuntime_LoadDefaults(void);
static void AppRuntime_LoadCurrentCard(void);
static void AppRuntime_ShowCurrentCardPage(void);
static void AppRuntime_HandleNfc(void);
static void AppRuntime_HandleGui(void);
static void AppRuntime_SyncGuiSetpoints(void);
static void AppRuntime_UpdateConfigSnapshot(void);
static void AppRuntime_RestoreConfiguredSetpoints(void);
static void AppRuntime_StartRun(void);
static void AppRuntime_StopRun(void);
static void AppRuntime_ApplyStateTransitions(void);
static void AppRuntime_UpdateMeasurements(void);
static void AppRuntime_UpdateRunCountdown(void);
static void AppRuntime_SyncOutputs(void);
static void AppRuntime_ReportStatus(void);
static uint8_t AppRuntime_IsHeatingState(uint8_t state);

void AppRuntime_Init(void)
{
  AppState_Init(&s_app_state);
  memset(&s_card_record, 0, sizeof(s_card_record));
  memset(s_uid, 0, sizeof(s_uid));
  s_uid_len = 0U;
  s_run_active = 0U;
  s_temp_valid = 0U;
  s_temp_x10 = 0;
  s_run_remaining_seconds = 0U;
  s_last_temp_tick = 0U;
  s_last_second_tick = 0U;
  s_last_status_tick = 0U;
  s_config_temp = 0U;
  s_config_work_time = 0U;
  s_config_sit_time = 0U;

  GUI_HMI_Init();
  HAL_Delay(500U);
  EEPROM_Init();
  HEATER_Init();
  PRESSURE_Init();
  T_Init();
  VOICE_Init();
  NFC_Init();
  AppBle_Init(&huart2);

  AppRuntime_LoadDefaults();
  AppRuntime_ShowCurrentCardPage();
  AppRuntime_ReportStatus();
}

void AppRuntime_Process(void)
{
  AppState state_before_cmd;
  uint8_t pressure_before;
  uint16_t pressure_adc_before;

  NFC_Task();
  AppBle_Process();
  state_before_cmd = s_app_state;
  pressure_before = s_app_state.pressure_present;
  pressure_adc_before = s_app_state.pressure_adc;
  AppBle_Callbacks ble_callbacks;
  ble_callbacks.get_epoch_fn = NULL;
  ble_callbacks.get_session_count_fn = EEPROM_GetSessionCount;
  ble_callbacks.get_session_fn = NULL;
  ble_callbacks.clear_sessions_fn = EEPROM_ClearSessionLog;
  AppBle_ProcessCommands(&s_app_state, AppParams_Save, &ble_callbacks);
  GUI_HMI_Task();
  PRESSURE_Task();

  if ((state_before_cmd.temp_set != s_app_state.temp_set) ||
      (state_before_cmd.minutes_left != s_app_state.minutes_left))
  {
    AppRuntime_SyncGuiSetpoints();
  }

  AppRuntime_HandleNfc();
  AppRuntime_HandleGui();
  AppRuntime_ApplyStateTransitions();
  AppRuntime_UpdateMeasurements();
  AppRuntime_UpdateRunCountdown();
  AppRuntime_SyncOutputs();

  if ((pressure_before != s_app_state.pressure_present) ||
      (pressure_adc_before != s_app_state.pressure_adc))
  {
    AppRuntime_ReportStatus();
  }

  if ((HAL_GetTick() - s_last_status_tick) >= STATUS_PERIOD_MS)
  {
    AppRuntime_ReportStatus();
  }
}

static HAL_StatusTypeDef AppParams_Save(const AppState *state)
{
  if (state == NULL)
  {
    return HAL_ERROR;
  }

  s_card_record.temp = state->temp_set;
  s_card_record.work_time = state->minutes_left;
  s_card_record.sit_time = state->minutes_left;

  if (s_uid_len == 0U)
  {
    return HAL_OK;
  }

  s_card_record.uid_len = s_uid_len;
  memcpy(s_card_record.uid, s_uid, s_uid_len);
  return (EEPROM_SaveCardRecord(&s_card_record, NULL) != 0U) ? HAL_OK : HAL_ERROR;
}

static void AppRuntime_LoadDefaults(void)
{
  s_card_record.uid_len = 0U;
  memset(s_card_record.uid, 0, sizeof(s_card_record.uid));
  s_card_record.temp = s_app_state.temp_set;
  s_card_record.work_time = s_app_state.minutes_left;
  s_card_record.sit_time = s_app_state.minutes_left;
  AppRuntime_UpdateConfigSnapshot();
  AppRuntime_SyncGuiSetpoints();
  GUI_HMI_SetValue("n2", s_app_state.minutes_left);
}

static void AppRuntime_LoadCurrentCard(void)
{
  if (s_uid_len == 0U)
  {
    return;
  }

  if (EEPROM_FindCardByUid(s_uid, s_uid_len, &s_card_record, NULL) == 0U)
  {
    EEPROM_LoadDefaultRecord(s_uid, s_uid_len, &s_card_record);
  }

  s_app_state.temp_set = s_card_record.temp;
  s_app_state.minutes_left = s_card_record.work_time;
  AppRuntime_UpdateConfigSnapshot();
  AppRuntime_SyncGuiSetpoints();
  GUI_HMI_SetValue("n2", s_card_record.sit_time);
}

static void AppRuntime_ShowCurrentCardPage(void)
{
  GUI_HMI_SendCmd("page 0");
  HAL_Delay(30U);
  AppRuntime_SyncGuiSetpoints();
  GUI_HMI_SetValue("n2", s_card_record.sit_time == 0U ? s_app_state.minutes_left : s_card_record.sit_time);
}

static void AppRuntime_SyncGuiSetpoints(void)
{
  GUI_HMI_SetValue("n0", s_app_state.temp_set);
  GUI_HMI_SetValue("n1", s_app_state.minutes_left);
}

static void AppRuntime_UpdateConfigSnapshot(void)
{
  s_config_temp = s_app_state.temp_set;
  s_config_work_time = s_app_state.minutes_left;
  s_config_sit_time = s_card_record.sit_time;
}

static void AppRuntime_RestoreConfiguredSetpoints(void)
{
  if (s_config_temp != 0U)
  {
    s_app_state.temp_set = s_config_temp;
  }

  if (s_config_work_time != 0U)
  {
    s_app_state.minutes_left = s_config_work_time;
  }

  if (s_config_sit_time != 0U)
  {
    s_card_record.sit_time = s_config_sit_time;
  }

  AppRuntime_SyncGuiSetpoints();
}

static void AppRuntime_HandleNfc(void)
{
  uint8_t new_uid[7];
  uint8_t new_uid_len;

  if (NFC_FetchNewCard(new_uid, &new_uid_len) == 0U)
  {
    return;
  }

  memcpy(s_uid, new_uid, new_uid_len);
  s_uid_len = new_uid_len;
  AppState_UpdateNfc(&s_app_state, 1U, s_uid, s_uid_len, NFC_IsReady() ? 0U : 1U);
  AppRuntime_LoadCurrentCard();
  AppRuntime_ShowCurrentCardPage();
  AppRuntime_ReportStatus();
}

static void AppRuntime_HandleGui(void)
{
  uint8_t set_temp;
  uint16_t set_work_time;
  uint16_t set_sit_time;

  if (GUI_HMI_FetchSetParam(&set_temp, &set_work_time, &set_sit_time) == 1U)
  {
    if (s_app_state.state == APP_STATE_IDLE)
    {
      s_app_state.temp_set = set_temp;
      s_app_state.minutes_left = set_work_time;
      s_card_record.sit_time = set_sit_time;
      AppRuntime_UpdateConfigSnapshot();
      (void)AppParams_Save(&s_app_state);
      AppRuntime_ReportStatus();
    }
  }

  if (GUI_HMI_FetchStartCommand() == 1U)
  {
    if (s_app_state.state == APP_STATE_IDLE)
    {
      AppState_Start(&s_app_state);
      AppRuntime_ReportStatus();
    }
  }

  if (GUI_HMI_FetchStopCommand() == 1U)
  {
    AppState_Stop(&s_app_state);
    AppRuntime_ReportStatus();
  }
}

static void AppRuntime_StartRun(void)
{
  uint16_t sit_time;

  if (s_run_active != 0U)
  {
    return;
  }

  AppRuntime_UpdateConfigSnapshot();
  s_run_active = 1U;
  s_run_remaining_seconds = (uint32_t)s_app_state.minutes_left * 60U;
  s_last_second_tick = HAL_GetTick();
  GUI_HMI_SendCmd("page 1");
  HAL_Delay(30U);
  GUI_HMI_SetValue("n1", s_app_state.minutes_left);
  sit_time = (s_card_record.sit_time == 0U) ? s_app_state.minutes_left : s_card_record.sit_time;
  GUI_HMI_SetValue("n2", sit_time);
  HEATER_RunStart(s_app_state.temp_set);
  PRESSURE_RunStart(sit_time);
  VOICE_PlayFile(1U, 3U);
  AppRuntime_ReportStatus();
}

static void AppRuntime_StopRun(void)
{
  if (s_run_active == 0U)
  {
    return;
  }

  s_run_active = 0U;
  s_run_remaining_seconds = 0U;
  s_last_second_tick = 0U;
  HEATER_RunStop();
  PRESSURE_RunStop();
  AppRuntime_RestoreConfiguredSetpoints();
  VOICE_PlayFile(1U, 4U);
  AppRuntime_ShowCurrentCardPage();
  AppRuntime_ReportStatus();
}

static void AppRuntime_ApplyStateTransitions(void)
{
  uint8_t should_run;

  should_run = AppRuntime_IsHeatingState(s_app_state.state);
  if ((should_run != 0U) && (s_run_active == 0U))
  {
    AppRuntime_StartRun();
  }
  else if ((should_run == 0U) && (s_run_active != 0U))
  {
    AppRuntime_StopRun();
  }
}

static void AppRuntime_UpdateMeasurements(void)
{
  uint32_t now_tick;
  int16_t temperature_x10;

  now_tick = HAL_GetTick();
  if ((now_tick - s_last_temp_tick) < TEMP_PERIOD_MS)
  {
    return;
  }

  s_last_temp_tick = now_tick;
  s_app_state.pressure_present = PRESSURE_IsOccupied();
  s_app_state.pressure_adc = PRESSURE_GetAdcValue();

  if (T_ReadTemperatureX10(&temperature_x10) == 1U)
  {
    s_temp_x10 = temperature_x10;
    s_temp_valid = 1U;
    AppState_UpdateMeasuredTemp(&s_app_state, temperature_x10, 0U);
    GUI_HMI_SetValue("x0", temperature_x10);
  }
  else
  {
    s_temp_x10 = 0;
    s_temp_valid = 0U;
    AppState_UpdateMeasuredTemp(&s_app_state, 0, 1U);
    GUI_HMI_SetValue("x0", 999);
  }
}

static void AppRuntime_UpdateRunCountdown(void)
{
  uint32_t now_tick;
  uint32_t elapsed_seconds;

  if (s_run_active == 0U)
  {
    return;
  }

  now_tick = HAL_GetTick();
  if ((now_tick - s_last_second_tick) < 1000U)
  {
    return;
  }

  elapsed_seconds = (now_tick - s_last_second_tick) / 1000U;
  s_last_second_tick += elapsed_seconds * 1000U;

  if (elapsed_seconds >= s_run_remaining_seconds)
  {
    s_run_remaining_seconds = 0U;
  }
  else
  {
    s_run_remaining_seconds -= elapsed_seconds;
  }

  s_app_state.minutes_left = (uint16_t)((s_run_remaining_seconds + 59U) / 60U);
  GUI_HMI_SetValue("n1", s_app_state.minutes_left);
  GUI_HMI_SetValue("n2", PRESSURE_GetRemainMinutes());

  if (s_run_remaining_seconds == 0U)
  {
    AppState_Stop(&s_app_state);
  }
}

static void AppRuntime_SyncOutputs(void)
{
  if (s_run_active != 0U)
  {
    HEATER_Update(s_temp_x10, s_temp_valid, s_app_state.pressure_present);
  }
}

static void AppRuntime_ReportStatus(void)
{
  s_last_status_tick = HAL_GetTick();
  AppBle_SendStatus(&s_app_state);
}

static uint8_t AppRuntime_IsHeatingState(uint8_t state)
{
  return ((state == APP_STATE_PREHEAT) || (state == APP_STATE_KEEP_WARM)) ? 1U : 0U;
}