#ifndef __T_H_
#define __T_H_

#include <stdint.h>

/**
  * @brief  初始化 MAX31865 温度采集 demo。
  * @note   当前 demo 采用：
  *         1. SPI1 与 MAX31865 通信
  *         2. PT100
  *         3. 3 线制
  *         4. 参考电阻 430Ω
  *         5. 通过 UART4 打印温度和故障状态
  */
void T_Init(void);
uint8_t T_ReadTemperatureInt(int16_t *temperature);
uint8_t T_ReadTemperatureX10(int16_t *temperature_x10);

/**
  * @brief  周期性执行一次温度采集任务。
  * @note   该函数设计为主循环中反复调用。
  *         内部会按固定周期读取 RTD 原始值、电阻值、温度值并从 UART4 输出。
  */
void T_Task(void);

#endif

