#include "pressure.h"
#include "bsp_system.h"
#include "adc.h"
#include "voice.h"

#define PRESSURE_THRESHOLD_ADC   1000U
#define PRESSURE_SAMPLE_MS       100U

static uint8_t g_pressure_run_enable = 0U;
static uint8_t g_pressure_occupied = 0U;
static uint8_t g_pressure_reminded = 0U;
static uint16_t g_pressure_sit_time_min = 0U;
static uint16_t g_pressure_adc_value = 0U;
static uint32_t g_pressure_last_sample_tick = 0U;
static uint32_t g_pressure_last_count_tick = 0U;
static uint32_t g_pressure_sit_elapsed_seconds = 0U;

static uint8_t PRESSURE_ReadOccupiedByAdc(void);

static uint8_t PRESSURE_ReadOccupiedByAdc(void)
{
  uint32_t adc_value = 0U;

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    return 0U;
  }

  if (HAL_ADC_PollForConversion(&hadc1, 10U) != HAL_OK)
  {
    (void)HAL_ADC_Stop(&hadc1);
    return 0U;
  }

  adc_value = HAL_ADC_GetValue(&hadc1);
  g_pressure_adc_value = (uint16_t)adc_value;
  (void)HAL_ADC_Stop(&hadc1);

  if (adc_value > PRESSURE_THRESHOLD_ADC)
  {
    return 1U;
  }

  return 0U;
}

void PRESSURE_Init(void)
{
  g_pressure_run_enable = 0U;
  g_pressure_occupied = 0U;
  g_pressure_reminded = 0U;
  g_pressure_sit_time_min = 0U;
  g_pressure_adc_value = 0U;
  g_pressure_last_sample_tick = 0U;
  g_pressure_last_count_tick = 0U;
  g_pressure_sit_elapsed_seconds = 0U;
}

void PRESSURE_RunStart(uint16_t sit_time_min)
{
  g_pressure_run_enable = 1U;
  g_pressure_occupied = 0U;
  g_pressure_reminded = 0U;
  g_pressure_sit_time_min = sit_time_min;
  g_pressure_adc_value = 0U;
  g_pressure_last_sample_tick = HAL_GetTick();
  g_pressure_last_count_tick = HAL_GetTick();
  g_pressure_sit_elapsed_seconds = 0U;
}

void PRESSURE_RunStop(void)
{
  g_pressure_run_enable = 0U;
  g_pressure_occupied = 0U;
  g_pressure_reminded = 0U;
  g_pressure_sit_time_min = 0U;
  g_pressure_adc_value = 0U;
  g_pressure_last_sample_tick = 0U;
  g_pressure_last_count_tick = 0U;
  g_pressure_sit_elapsed_seconds = 0U;
}

void PRESSURE_Task(void)
{
  uint32_t now_tick;

  now_tick = HAL_GetTick();

  if ((now_tick - g_pressure_last_sample_tick) >= PRESSURE_SAMPLE_MS)
  {
    g_pressure_last_sample_tick = now_tick;
    g_pressure_occupied = PRESSURE_ReadOccupiedByAdc();
  }

  if (g_pressure_run_enable == 0U)
  {
    g_pressure_sit_elapsed_seconds = 0U;
    g_pressure_reminded = 0U;
    return;
  }

  if ((now_tick - g_pressure_last_count_tick) >= 1000U)
  {
    uint32_t elapsed_seconds;

    elapsed_seconds = (now_tick - g_pressure_last_count_tick) / 1000U;
    g_pressure_last_count_tick += elapsed_seconds * 1000U;

    if (g_pressure_occupied == 1U)
    {
      g_pressure_sit_elapsed_seconds += elapsed_seconds;

      if ((g_pressure_sit_time_min > 0U) &&
          (g_pressure_reminded == 0U) &&
          (g_pressure_sit_elapsed_seconds >= ((uint32_t)g_pressure_sit_time_min * 60U)))
      {
        g_pressure_reminded = 1U;
        VOICE_PlayFile(1U, 2U);
      }
    }
    else
    {
      g_pressure_sit_elapsed_seconds = 0U;
      g_pressure_reminded = 0U;
    }
  }
}

uint8_t PRESSURE_IsOccupied(void)
{
  return g_pressure_occupied;
}

uint16_t PRESSURE_GetRemainMinutes(void)
{
  uint32_t target_seconds;

  if ((g_pressure_run_enable == 0U) || (g_pressure_sit_time_min == 0U))
  {
    return 0U;
  }

  target_seconds = (uint32_t)g_pressure_sit_time_min * 60U;
  if (g_pressure_sit_elapsed_seconds >= target_seconds)
  {
    return 0U;
  }

  return (uint16_t)(((target_seconds - g_pressure_sit_elapsed_seconds) + 59U) / 60U);
}

uint16_t PRESSURE_GetAdcValue(void)
{
  return g_pressure_adc_value;
}
