#ifndef __HMI_H_
#define __HMI_H_

#include <stdint.h>

void HMI_Init(void);
void HMI_Task(void);

void HMI_SendCmd(const char *cmd);
void HMI_SetPage(const char *page_name);
void HMI_SetValue(const char *obj, int32_t value);
void HMI_SetText(const char *obj, const char *text);

uint8_t HMI_GetRxFlag(void);
uint16_t HMI_GetRxLength(void);
uint8_t *HMI_GetRxBuffer(void);
void HMI_ClearRxBuffer(void);

void HMI_RxByteCallback(uint8_t data);

#endif
