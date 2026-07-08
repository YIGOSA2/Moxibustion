#ifndef __GUIH_
#define __GUIH_

#include <stdint.h>

void GUI_Print(const char *text);
void GUI_Printf(const char *fmt, ...);
void GUI_PrintHex(const uint8_t *buf, uint8_t len);

void GUI_HMI_Init(void);
void GUI_HMI_Task(void);
void GUI_HMI_SendCmd(const char *cmd);
void GUI_HMI_SetPage(const char *page_name);
void GUI_HMI_SetValue(const char *obj, int32_t value);
void GUI_HMI_SetText(const char *obj, const char *text);

uint8_t GUI_HMI_GetRxFlag(void);
uint16_t GUI_HMI_GetRxLength(void);
uint8_t *GUI_HMI_GetRxBuffer(void);
void GUI_HMI_ClearRxBuffer(void);
void GUI_HMI_RxByteCallback(uint8_t data);
uint8_t GUI_HMI_FetchSetParam(uint8_t *temp, uint16_t *work_time, uint16_t *sit_time);
uint8_t GUI_HMI_GetCachedParam(uint8_t *temp, uint16_t *work_time, uint16_t *sit_time);
uint8_t GUI_HMI_FetchStartCommand(void);
uint8_t GUI_HMI_FetchStopCommand(void);

#endif
