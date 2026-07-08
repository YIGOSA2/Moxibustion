#ifndef __PRESSURE_H_
#define __PRESSURE_H_

#include <stdint.h>

void PRESSURE_Init(void);
void PRESSURE_RunStart(uint16_t sit_time_min);
void PRESSURE_RunStop(void);
void PRESSURE_Task(void);
uint8_t PRESSURE_IsOccupied(void);
uint16_t PRESSURE_GetRemainMinutes(void);
uint16_t PRESSURE_GetAdcValue(void);

#endif
