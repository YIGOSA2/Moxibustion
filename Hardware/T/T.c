#include "T.h"
#include "bsp_system.h"
#include "gui.h"
#include <math.h>

/* MAX31865 相关寄存器地址。 */
#define MAX31865_REG_CONFIG               0x00U
#define MAX31865_REG_RTD_MSB             0x01U
#define MAX31865_REG_RTD_LSB             0x02U
#define MAX31865_REG_FAULT_STATUS        0x07U

/* MAX31865 配置寄存器位定义。 */
#define MAX31865_CFG_VBIAS               0x80U
#define MAX31865_CFG_1SHOT               0x20U
#define MAX31865_CFG_3WIRE               0x10U
#define MAX31865_CFG_FAULT_STATUS_CLEAR  0x02U
#define MAX31865_CFG_FILTER_50HZ         0x01U

/* MAX31865 故障寄存器位定义。 */
#define MAX31865_FAULT_HIGH_THRESHOLD    0x80U
#define MAX31865_FAULT_LOW_THRESHOLD     0x40U
#define MAX31865_FAULT_REFIN_LOW         0x20U
#define MAX31865_FAULT_REFIN_HIGH        0x10U
#define MAX31865_FAULT_RTDIN_LOW         0x08U
#define MAX31865_FAULT_OVUV              0x04U

/* 当前板级硬件连接。 */
#define MAX31865_CS_GPIO_PORT            GPIOA
#define MAX31865_CS_GPIO_PIN             GPIO_PIN_4
#define MAX31865_RDY_GPIO_PORT           GPIOB
#define MAX31865_RDY_GPIO_PIN            GPIO_PIN_0

/* 当前 MAX31865 模块实测参考电阻为 432Ω。 */
#define MAX31865_RREF_OHM                432.0f
#define MAX31865_RTD_NOMINAL_OHM         100.0f
/* 按当前单独测试 demo 的结果，对换算后的 PT100 电阻做 0.4Ω 补偿。 */
#define MAX31865_RTD_OFFSET_OHM          0.4f

/* Callendar-Van Dusen 系数，PT100/PT1000 常用。 */
#define RTD_A                            3.9083e-3f
#define RTD_B                           -5.775e-7f

static uint32_t g_t_last_tick = 0U;
static uint8_t g_t_init_ok = 0U;

static void MAX31865_CS_Low(void);
static void MAX31865_CS_High(void);
static HAL_StatusTypeDef MAX31865_WriteRegister(uint8_t reg, uint8_t value);
static HAL_StatusTypeDef MAX31865_ReadRegister(uint8_t reg, uint8_t *value);
static HAL_StatusTypeDef MAX31865_ReadRegisters(uint8_t reg, uint8_t *buf, uint16_t len);
static void MAX31865_ClearFault(void);
static void MAX31865_SetWireMode3(void);
static HAL_StatusTypeDef MAX31865_ReadFault(uint8_t *fault);
static HAL_StatusTypeDef MAX31865_ReadRaw(uint16_t *raw);
static float MAX31865_RawToResistance(uint16_t raw);
static float MAX31865_ResistanceToTemperature(float resistance);
static void MAX31865_PrintFaultDetail(uint8_t fault);
static void MAX31865_PrintScaledValue(const char *name, float value, const char *unit);

/**
  * @brief  拉低 MAX31865 片选。
  */
static void MAX31865_CS_Low(void)
{
  HAL_GPIO_WritePin(MAX31865_CS_GPIO_PORT, MAX31865_CS_GPIO_PIN, GPIO_PIN_RESET);
}

/**
  * @brief  拉高 MAX31865 片选。
  */
static void MAX31865_CS_High(void)
{
  HAL_GPIO_WritePin(MAX31865_CS_GPIO_PORT, MAX31865_CS_GPIO_PIN, GPIO_PIN_SET);
}

/**
  * @brief  向 MAX31865 写 1 字节寄存器。
  * @param  reg   寄存器地址
  * @param  value 要写入的值
  * @retval HAL_OK 表示写成功
  */
static HAL_StatusTypeDef MAX31865_WriteRegister(uint8_t reg, uint8_t value)
{
  uint8_t tx_buf[2];

  tx_buf[0] = (uint8_t)(reg | 0x80U);
  tx_buf[1] = value;

  MAX31865_CS_Low();
  if (HAL_SPI_Transmit(&hspi1, tx_buf, 2U, 100U) != HAL_OK)
  {
    MAX31865_CS_High();
    return HAL_ERROR;
  }
  MAX31865_CS_High();

  return HAL_OK;
}

/**
  * @brief  从 MAX31865 读取 1 字节寄存器。
  * @param  reg   寄存器地址
  * @param  value 读取结果输出地址
  * @retval HAL_OK 表示读成功
  */
static HAL_StatusTypeDef MAX31865_ReadRegister(uint8_t reg, uint8_t *value)
{
  return MAX31865_ReadRegisters(reg, value, 1U);
}

/**
  * @brief  连续读取 MAX31865 寄存器。
  * @param  reg  起始寄存器地址
  * @param  buf  接收缓冲区
  * @param  len  连续读取长度
  * @retval HAL_OK 表示读取成功
  */
static HAL_StatusTypeDef MAX31865_ReadRegisters(uint8_t reg, uint8_t *buf, uint16_t len)
{
  uint8_t addr;

  if ((buf == NULL) || (len == 0U))
  {
    return HAL_ERROR;
  }

  addr = (uint8_t)(reg & 0x7FU);

  MAX31865_CS_Low();
  if (HAL_SPI_Transmit(&hspi1, &addr, 1U, 100U) != HAL_OK)
  {
    MAX31865_CS_High();
    return HAL_ERROR;
  }

  if (HAL_SPI_Receive(&hspi1, buf, len, 100U) != HAL_OK)
  {
    MAX31865_CS_High();
    return HAL_ERROR;
  }
  MAX31865_CS_High();

  return HAL_OK;
}

/**
  * @brief  清除 MAX31865 错误标志。
  * @note   该操作不会改变我们需要的 3 线制配置，只是清 fault 位。
  */
static void MAX31865_ClearFault(void)
{
  uint8_t cfg = 0U;

  if (MAX31865_ReadRegister(MAX31865_REG_CONFIG, &cfg) != HAL_OK)
  {
    return;
  }

  cfg &= (uint8_t)(~0x2CU);
  cfg |= MAX31865_CFG_FAULT_STATUS_CLEAR;
  (void)MAX31865_WriteRegister(MAX31865_REG_CONFIG, cfg);
}

/**
  * @brief  将 MAX31865 配置成 3 线制、60Hz 滤波、单次转换模式。
  */
static void MAX31865_SetWireMode3(void)
{
  uint8_t cfg = 0U;

  if (MAX31865_ReadRegister(MAX31865_REG_CONFIG, &cfg) != HAL_OK)
  {
    return;
  }

  cfg |= MAX31865_CFG_3WIRE;
  cfg |= MAX31865_CFG_FILTER_50HZ;
  cfg &= (uint8_t)(~MAX31865_CFG_VBIAS);
  cfg &= (uint8_t)(~MAX31865_CFG_1SHOT);
  (void)MAX31865_WriteRegister(MAX31865_REG_CONFIG, cfg);
}

/**
  * @brief  读取故障寄存器。
  * @param  fault 故障值输出地址
  * @retval HAL_OK 表示读取成功
  */
static HAL_StatusTypeDef MAX31865_ReadFault(uint8_t *fault)
{
  if (fault == NULL)
  {
    return HAL_ERROR;
  }

  return MAX31865_ReadRegister(MAX31865_REG_FAULT_STATUS, fault);
}

/**
  * @brief  读取一次 RTD 原始值。
  * @param  raw 15 位原始 RTD 数据输出地址
  * @retval HAL_OK 表示采样成功
  * @note   这里按手册推荐流程做：
  *         1. 清故障
  *         2. 打开偏置
  *         3. 等待前端稳定
  *         4. 触发单次转换
  *         5. 等待转换完成
  *         6. 读取 RTD 数据
  */
static HAL_StatusTypeDef MAX31865_ReadRaw(uint16_t *raw)
{
  uint8_t cfg = 0U;
  uint8_t rx_buf[2] = {0};
  uint16_t raw_value;

  if (raw == NULL)
  {
    return HAL_ERROR;
  }

  MAX31865_ClearFault();

  if (MAX31865_ReadRegister(MAX31865_REG_CONFIG, &cfg) != HAL_OK)
  {
    return HAL_ERROR;
  }

  cfg |= MAX31865_CFG_VBIAS;
  if (MAX31865_WriteRegister(MAX31865_REG_CONFIG, cfg) != HAL_OK)
  {
    return HAL_ERROR;
  }

  HAL_Delay(10);

  cfg |= MAX31865_CFG_1SHOT;
  if (MAX31865_WriteRegister(MAX31865_REG_CONFIG, cfg) != HAL_OK)
  {
    return HAL_ERROR;
  }

  HAL_Delay(65);

  if (MAX31865_ReadRegisters(MAX31865_REG_RTD_MSB, rx_buf, 2U) != HAL_OK)
  {
    return HAL_ERROR;
  }

  raw_value = (uint16_t)((uint16_t)rx_buf[0] << 8);
  raw_value |= rx_buf[1];
  raw_value >>= 1;

  *raw = raw_value;
  return HAL_OK;
}

/**
  * @brief  将 MAX31865 原始值换算成 RTD 电阻值。
  * @param  raw MAX31865 15 位原始值
  * @retval 电阻值，单位欧姆
  */
static float MAX31865_RawToResistance(uint16_t raw)
{
  float ratio;
  float resistance;

  ratio = (float)raw / 32768.0f;
  resistance = ratio * MAX31865_RREF_OHM;
  return resistance - MAX31865_RTD_OFFSET_OHM;
}

/**
  * @brief  将 RTD 电阻值换算成温度值。
  * @param  resistance RTD 电阻值，单位欧姆
  * @retval 温度值，单位摄氏度
  * @note   这里直接采用资料里 Adafruit 库的计算流程，
  *         本质上就是 Callendar-Van Dusen 公式和负温区多项式近似。
  */
static float MAX31865_ResistanceToTemperature(float resistance)
{
  float z1;
  float z2;
  float z3;
  float z4;
  float temp;
  float rpoly;

  z1 = -RTD_A;
  z2 = RTD_A * RTD_A - (4.0f * RTD_B);
  z3 = (4.0f * RTD_B) / MAX31865_RTD_NOMINAL_OHM;
  z4 = 2.0f * RTD_B;

  temp = z2 + (z3 * resistance);
  temp = (sqrtf(temp) + z1) / z4;

  if (temp >= 0.0f)
  {
    return temp;
  }

  rpoly = resistance;

  temp = -242.02f;
  temp += 2.2228f * rpoly;
  rpoly *= resistance;
  temp += 2.5859e-3f * rpoly;
  rpoly *= resistance;
  temp -= 4.8260e-6f * rpoly;
  rpoly *= resistance;
  temp -= 2.8183e-8f * rpoly;
  rpoly *= resistance;
  temp += 1.5243e-10f * rpoly;

  return temp;
}

/**
  * @brief  逐项打印故障信息，便于第一次联调定位问题。
  * @param  fault 故障寄存器值
  */
static void MAX31865_PrintFaultDetail(uint8_t fault)
{
  if (fault & MAX31865_FAULT_HIGH_THRESHOLD)
  {
    GUI_Print("Fault: RTD high threshold.\r\n");
  }
  if (fault & MAX31865_FAULT_LOW_THRESHOLD)
  {
    GUI_Print("Fault: RTD low threshold.\r\n");
  }
  if (fault & MAX31865_FAULT_REFIN_LOW)
  {
    GUI_Print("Fault: REFIN- > 0.85 x VBIAS.\r\n");
  }
  if (fault & MAX31865_FAULT_REFIN_HIGH)
  {
    GUI_Print("Fault: REFIN- < 0.85 x VBIAS, FORCE- may open.\r\n");
  }
  if (fault & MAX31865_FAULT_RTDIN_LOW)
  {
    GUI_Print("Fault: RTDIN- < 0.85 x VBIAS, FORCE- may open.\r\n");
  }
  if (fault & MAX31865_FAULT_OVUV)
  {
    GUI_Print("Fault: Under/over voltage detected.\r\n");
  }
}

/**
  * @brief  以固定三位小数格式输出浮点值，避免直接依赖 printf 的 %f 支持。
  * @param  name  数值名称前缀
  * @param  value 需要输出的数值
  * @param  unit  单位字符串
  */
static void MAX31865_PrintScaledValue(const char *name, float value, const char *unit)
{
  int32_t scaled;
  int32_t integer_part;
  int32_t fraction_part;

  scaled = (int32_t)(value * 1000.0f);
  integer_part = scaled / 1000;
  fraction_part = scaled % 1000;

  if (fraction_part < 0)
  {
    fraction_part = -fraction_part;
  }

  GUI_Printf("%s%d.%03d %s", name, integer_part, fraction_part, unit);
}

/**
  * @brief  初始化 MAX31865 最小温度 demo。
  * @note   这里只做最必要的动作：
  *         1. 检查 SPI 片选和 RDY 引脚状态
  *         2. 配置 3 线制
  *         3. 清除一次故障标志
  *         4. 通过 UART4 打印启动信息
  */
void T_Init(void)
{
  g_t_last_tick = 0U;
  g_t_init_ok = 1U;

  GUI_Print("\r\n==== MAX31865 Demo Start ====\r\n");
  GUI_Print("SPI1 ready. UART4 ready.\r\n");
  GUI_Print("Current demo config: PT100, 3-wire, RREF=432 ohm, R offset=-0.4 ohm.\r\n");
  GUI_Printf("RDY pin level on boot: %u\r\n",
             (uint32_t)HAL_GPIO_ReadPin(MAX31865_RDY_GPIO_PORT, MAX31865_RDY_GPIO_PIN));

  MAX31865_CS_High();
  MAX31865_SetWireMode3();
  MAX31865_ClearFault();

  GUI_Print("MAX31865 init done.\r\n");
  GUI_Print("==== Demo Running ====\r\n");
}

uint8_t T_ReadTemperatureInt(int16_t *temperature)
{
  uint16_t raw = 0U;
  uint8_t fault = 0U;
  float resistance;
  float temp_float;

  if ((temperature == NULL) || (g_t_init_ok == 0U))
  {
    return 0U;
  }

  if (MAX31865_ReadRaw(&raw) != HAL_OK)
  {
    return 0U;
  }

  if (MAX31865_ReadFault(&fault) != HAL_OK)
  {
    return 0U;
  }

  if (fault != 0U)
  {
    MAX31865_ClearFault();
    return 0U;
  }

  resistance = MAX31865_RawToResistance(raw);
  temp_float = MAX31865_ResistanceToTemperature(resistance);

  if (temp_float >= 0.0f)
  {
    *temperature = (int16_t)(temp_float + 0.5f);
  }
  else
  {
    *temperature = (int16_t)(temp_float - 0.5f);
  }

  return 1U;
}

uint8_t T_ReadTemperatureX10(int16_t *temperature_x10)
{
  uint16_t raw = 0U;
  uint8_t fault = 0U;
  float resistance;
  float temp_float;
  float scaled;

  if ((temperature_x10 == NULL) || (g_t_init_ok == 0U))
  {
    return 0U;
  }

  if (MAX31865_ReadRaw(&raw) != HAL_OK)
  {
    return 0U;
  }

  if (MAX31865_ReadFault(&fault) != HAL_OK)
  {
    return 0U;
  }

  if (fault != 0U)
  {
    MAX31865_ClearFault();
    return 0U;
  }

  resistance = MAX31865_RawToResistance(raw);
  temp_float = MAX31865_ResistanceToTemperature(resistance);
  scaled = temp_float * 10.0f;

  if (scaled >= 0.0f)
  {
    *temperature_x10 = (int16_t)(scaled + 0.5f);
  }
  else
  {
    *temperature_x10 = (int16_t)(scaled - 0.5f);
  }

  return 1U;
}

/**
  * @brief  周期性执行温度采样和串口打印。
  * @note   当前最小 demo 采用 1 秒打印一次，
  *         便于观察原始值、电阻值、温度值和故障状态是否正常。
  */
void T_Task(void)
{
  uint16_t raw = 0U;
  uint8_t fault = 0U;
  float resistance;
  float temperature;
  uint32_t now_tick;

  if (g_t_init_ok == 0U)
  {
    return;
  }

  now_tick = HAL_GetTick();
  if ((now_tick - g_t_last_tick) < 1000U)
  {
    return;
  }
  g_t_last_tick = now_tick;

  if (MAX31865_ReadRaw(&raw) != HAL_OK)
  {
    GUI_Print("MAX31865 read raw failed.\r\n");
    return;
  }

  resistance = MAX31865_RawToResistance(raw);
  temperature = MAX31865_ResistanceToTemperature(resistance);

  if (MAX31865_ReadFault(&fault) != HAL_OK)
  {
    GUI_Print("MAX31865 read fault failed.\r\n");
    return;
  }

  GUI_Printf("RTD=%u, ", raw);
  MAX31865_PrintScaledValue("R=", resistance, "ohm, ");
  MAX31865_PrintScaledValue("T=", temperature, "C, ");
  GUI_Printf("Fault=0x%02X, RDY=%u\r\n",
             fault,
             (uint32_t)HAL_GPIO_ReadPin(MAX31865_RDY_GPIO_PORT, MAX31865_RDY_GPIO_PIN));

  if (fault != 0U)
  {
    MAX31865_PrintFaultDetail(fault);
    MAX31865_ClearFault();
  }
}
