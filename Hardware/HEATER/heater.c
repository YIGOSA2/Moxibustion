#include "heater.h"
#include "bsp_system.h"

#define HEATER_CTRL_GPIO_PORT        GPIOA
#define HEATER_CTRL_GPIO_PIN         GPIO_PIN_8

#define HEATER_OUTPUT_ACTIVE_HIGH    1U

#define HEATER_TEMP_OVER_LIMIT_X10   650

/*
 * 低频时间比例 PWM：2 秒一个窗口，窗口内前 duty% 时间输出接通。
 * 加热垫热时间常数远大于 2 秒，且不依赖驱动电路类型（MOSFET/继电器均可）。
 * 若确认为 MOSFET 驱动，PA8 为 TIM1_CH1，可平移到硬件 PWM。
 */
#define HEATER_WINDOW_MS             2000U

/* 预热 -> 保温：提前 3.0 ℃ 退出全功率，靠余热爬到目标（抑制过冲）；
   保温 -> 预热：低于目标 5.0 ℃ 才重回全功率 */
#define HEATER_PREHEAT_DONE_X10      30
#define HEATER_PREHEAT_REENTER_X10   50

/* 测温传感器有延迟，越接近目标越要放慢加热：
   距目标 > 8 ℃ 全功率；8 ~ 3 ℃ 降为 60%，给传感器留出跟上的时间 */
#define HEATER_PREHEAT_SLOW_X10      120
#define HEATER_PREHEAT_SLOW_DUTY     30

/* 保温段：比例项 1% / 0.1℃（上限 40%），到达/超过目标立即 0 输出；
   慢积分项每 4 秒调一格，补偿稳态散热，避免温度停在目标下方 */
#define HEATER_APPROACH_MAX_DUTY     18
#define HEATER_OVER_CUT_X10          5
#define HEATER_I_MAX                 24
#define HEATER_I_PERIOD_MS           5000U

static uint8_t g_heater_run_enable = 0U;
static uint8_t g_heater_output_enable = 0U;
static uint8_t g_heater_phase = HEATER_PHASE_IDLE;
static uint8_t g_heater_duty_percent = 0U;
static int16_t g_heater_target_temp_x10 = 0;
static uint32_t g_heater_window_tick = 0U;
static int16_t g_heater_i_duty = 0;
static uint32_t g_heater_i_tick = 0U;

static void HEATER_SetOutput(uint8_t enable);

static void HEATER_SetOutput(uint8_t enable)
{
  GPIO_PinState state;

  if (enable != 0U)
  {
    state = (HEATER_OUTPUT_ACTIVE_HIGH != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
  }
  else
  {
    state = (HEATER_OUTPUT_ACTIVE_HIGH != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET;
  }

  HAL_GPIO_WritePin(HEATER_CTRL_GPIO_PORT, HEATER_CTRL_GPIO_PIN, state);
  g_heater_output_enable = enable;
}

void HEATER_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitStruct.Pin = HEATER_CTRL_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HEATER_CTRL_GPIO_PORT, &GPIO_InitStruct);

  g_heater_run_enable = 0U;
  g_heater_output_enable = 0U;
  g_heater_phase = HEATER_PHASE_IDLE;
  g_heater_duty_percent = 0U;
  g_heater_target_temp_x10 = 0;
  g_heater_window_tick = 0U;
  HEATER_SetOutput(0U);
}

void HEATER_RunStart(uint8_t target_temp)
{
  g_heater_run_enable = 1U;
  g_heater_phase = HEATER_PHASE_PREHEAT;
  g_heater_duty_percent = 0U;
  g_heater_target_temp_x10 = (int16_t)target_temp * 10;
  g_heater_window_tick = HAL_GetTick();
  g_heater_i_duty = 0;
  g_heater_i_tick = HAL_GetTick();
  HEATER_SetOutput(0U);
}

void HEATER_RunStop(void)
{
  g_heater_run_enable = 0U;
  g_heater_phase = HEATER_PHASE_IDLE;
  g_heater_duty_percent = 0U;
  g_heater_target_temp_x10 = 0;
  g_heater_window_tick = 0U;
  g_heater_i_duty = 0;
  g_heater_i_tick = 0U;
  HEATER_SetOutput(0U);
}

void HEATER_Update(int16_t temperature_x10, uint8_t temperature_valid, uint8_t pressure_occupied)
{
  int16_t delta_x10;
  int32_t duty;
  uint32_t now_tick;

  (void)pressure_occupied;

  if (g_heater_run_enable == 0U)
  {
    g_heater_duty_percent = 0U;
    HEATER_SetOutput(0U);
    return;
  }

  if ((temperature_valid == 0U) || (temperature_x10 >= HEATER_TEMP_OVER_LIMIT_X10))
  {
    g_heater_duty_percent = 0U;
    HEATER_SetOutput(0U);
    return;
  }

  delta_x10 = (int16_t)(g_heater_target_temp_x10 - temperature_x10);

  if ((g_heater_phase == HEATER_PHASE_PREHEAT) && (delta_x10 <= HEATER_PREHEAT_DONE_X10))
  {
    g_heater_phase = HEATER_PHASE_KEEP_WARM;
    g_heater_i_duty = 0;
    g_heater_i_tick = HAL_GetTick();
  }
  else if ((g_heater_phase == HEATER_PHASE_KEEP_WARM) && (delta_x10 > HEATER_PREHEAT_REENTER_X10))
  {
    g_heater_phase = HEATER_PHASE_PREHEAT;
    g_heater_i_duty = 0;
  }

  if (g_heater_phase == HEATER_PHASE_PREHEAT)
  {
    duty = (delta_x10 > HEATER_PREHEAT_SLOW_X10) ? 100 : HEATER_PREHEAT_SLOW_DUTY;
  }
  else
  {
    now_tick = HAL_GetTick();
    if ((now_tick - g_heater_i_tick) >= HEATER_I_PERIOD_MS)
    {
      g_heater_i_tick = now_tick;
      if ((delta_x10 > 2) && (g_heater_i_duty < HEATER_I_MAX))
      {
        g_heater_i_duty++;
      }
      else if ((delta_x10 < -2) && (g_heater_i_duty > 0))
      {
        g_heater_i_duty -= (g_heater_i_duty >= 2) ? 2 : g_heater_i_duty;
      }
    }

    duty = (int32_t)delta_x10;
    if (duty < 0)
    {
      duty = 0;
    }
    if (duty > HEATER_APPROACH_MAX_DUTY)
    {
      duty = HEATER_APPROACH_MAX_DUTY;
    }

    duty += g_heater_i_duty;
    if (duty > 100)
    {
      duty = 100;
    }

    if (delta_x10 <= -HEATER_OVER_CUT_X10)
    {
      duty = 0;
    }
  }

  g_heater_duty_percent = (uint8_t)duty;
}

void HEATER_Task(void)
{
  uint32_t now_tick;
  uint32_t elapsed_ms;
  uint32_t on_ms;

  if (g_heater_run_enable == 0U)
  {
    return;
  }

  now_tick = HAL_GetTick();
  if ((now_tick - g_heater_window_tick) >= HEATER_WINDOW_MS)
  {
    g_heater_window_tick = now_tick;
  }

  elapsed_ms = now_tick - g_heater_window_tick;
  on_ms = (HEATER_WINDOW_MS * (uint32_t)g_heater_duty_percent) / 100U;

  if (elapsed_ms < on_ms)
  {
    HEATER_SetOutput(1U);
  }
  else
  {
    HEATER_SetOutput(0U);
  }
}

uint8_t HEATER_IsEnabled(void)
{
  return g_heater_output_enable;
}

uint8_t HEATER_GetPhase(void)
{
  return g_heater_phase;
}

uint8_t HEATER_GetDutyPercent(void)
{
  return g_heater_duty_percent;
}
