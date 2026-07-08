#ifndef __HEATER_H_
#define __HEATER_H_

#include <stdint.h>

typedef enum
{
  HEATER_PHASE_IDLE = 0U,
  HEATER_PHASE_PREHEAT = 1U,
  HEATER_PHASE_KEEP_WARM = 2U
} HeaterPhase;

void HEATER_Init(void);
void HEATER_RunStart(uint8_t target_temp);
void HEATER_RunStop(void);
void HEATER_Update(int16_t temperature_x10, uint8_t temperature_valid, uint8_t pressure_occupied);
void HEATER_Task(void);
uint8_t HEATER_IsEnabled(void);
uint8_t HEATER_GetPhase(void);
uint8_t HEATER_GetDutyPercent(void);

#endif
