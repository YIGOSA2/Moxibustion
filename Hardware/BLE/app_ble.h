#ifndef APP_BLE_H
#define APP_BLE_H

#include "drv_ble.h"
#include "app_state.h"

typedef struct
{
  uint32_t (*get_epoch_fn)(void);
  uint8_t (*get_session_count_fn)(void);
  uint8_t (*get_session_fn)(uint8_t index, uint32_t *start, uint32_t *end, uint16_t *duration);
  void (*clear_sessions_fn)(void);
} AppBle_Callbacks;

void AppBle_Init(UART_HandleTypeDef *huart);
void AppBle_Process(void);
void AppBle_SendStatus(const AppState *state);
uint32_t AppBle_GetEpoch(void);
void AppBle_ProcessCommands(AppState *state, HAL_StatusTypeDef (*save_fn)(const AppState *state),
                            const AppBle_Callbacks *callbacks);

#endif