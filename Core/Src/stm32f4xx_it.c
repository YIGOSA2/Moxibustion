/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"
#include "stm32f4xx_it.h"
#include "gui.h"
extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern uint8_t g_uart4_rx_byte;
void NMI_Handler(void) { while (1) {} }
void HardFault_Handler(void) { while (1) {} }
void MemManage_Handler(void) { while (1) {} }
void BusFault_Handler(void) { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}
void SysTick_Handler(void) { HAL_IncTick(); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }
void USART3_IRQHandler(void) { HAL_UART_IRQHandler(&huart3); }
void UART4_IRQHandler(void)
{
  if (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_RXNE) != RESET)
  {
    g_uart4_rx_byte = (uint8_t)(huart4.Instance->DR & 0xFFU);
    GUI_HMI_RxByteCallback(g_uart4_rx_byte);
  }
  HAL_UART_IRQHandler(&huart4);
}