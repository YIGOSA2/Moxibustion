#include "HMI.h"
#include "bsp_system.h"
#include "gui.h"

/* 当前串口屏命令采用文本指令 + 0xFF 0xFF 0xFF 结尾的方式。 */
#define HMI_RX_BUF_SIZE  128U

static uint8_t g_hmi_rx_buf[HMI_RX_BUF_SIZE] = {0};
static uint16_t g_hmi_rx_len = 0U;
static uint8_t g_hmi_rx_flag = 0U;

static void HMI_SendTerminator(void);

/**
  * @brief  发送屏幕协议结束符。
  */
static void HMI_SendTerminator(void)
{
  const uint8_t end_flag[3] = {0xFF, 0xFF, 0xFF};
  HAL_UART_Transmit(&huart4, (uint8_t *)end_flag, 3U, 1000U);
}

/**
  * @brief  初始化串口屏基础层。
  * @note   第一阶段只准备发送和接收基础能力，不绑定具体页面业务。
  */
void HMI_Init(void)
{
  HMI_ClearRxBuffer();
  GUI_Print("HMI basic layer ready.\r\n");
}

/**
  * @brief  串口屏任务轮询入口。
  * @note   当前先保留空实现，后面接入页面事件解析时再扩展。
  */
void HMI_Task(void)
{
}

/**
  * @brief  向串口屏发送一条完整文本命令。
  * @param  cmd 文本命令，不包含结尾 0xFF 0xFF 0xFF
  */
void HMI_SendCmd(const char *cmd)
{
  if (cmd == NULL)
  {
    return;
  }

  HAL_UART_Transmit(&huart4, (uint8_t *)cmd, (uint16_t)strlen(cmd), 1000U);
  HMI_SendTerminator();
}

/**
  * @brief  切换串口屏页面。
  * @param  page_name 页面名称
  * @note   例如：page main
  */
void HMI_SetPage(const char *page_name)
{
  char cmd[64];

  if (page_name == NULL)
  {
    return;
  }

  snprintf(cmd, sizeof(cmd), "page %s", page_name);
  HMI_SendCmd(cmd);
}

/**
  * @brief  设置串口屏数值型控件的 val。
  * @param  obj   控件名
  * @param  value 数值
  * @note   例如：n0.val=25
  */
void HMI_SetValue(const char *obj, int32_t value)
{
  char cmd[64];

  if (obj == NULL)
  {
    return;
  }

  snprintf(cmd, sizeof(cmd), "%s.val=%ld", obj, (long)value);
  HMI_SendCmd(cmd);
}

/**
  * @brief  设置串口屏文本控件的 txt。
  * @param  obj  控件名
  * @param  text 目标文本
  * @note   例如：t0.txt="hello"
  */
void HMI_SetText(const char *obj, const char *text)
{
  char cmd[96];

  if ((obj == NULL) || (text == NULL))
  {
    return;
  }

  snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", obj, text);
  HMI_SendCmd(cmd);
}

/**
  * @brief  查询是否已经收到一帧屏幕返回数据。
  * @retval 1 表示有新数据，0 表示没有
  */
uint8_t HMI_GetRxFlag(void)
{
  return g_hmi_rx_flag;
}

/**
  * @brief  获取当前接收缓冲区长度。
  */
uint16_t HMI_GetRxLength(void)
{
  return g_hmi_rx_len;
}

/**
  * @brief  获取当前接收缓冲区首地址。
  */
uint8_t *HMI_GetRxBuffer(void)
{
  return g_hmi_rx_buf;
}

/**
  * @brief  清空屏幕接收缓冲区。
  */
void HMI_ClearRxBuffer(void)
{
  memset(g_hmi_rx_buf, 0, sizeof(g_hmi_rx_buf));
  g_hmi_rx_len = 0U;
  g_hmi_rx_flag = 0U;
}

/**
  * @brief  UART4 接收到 1 字节时喂给 HMI 缓冲区。
  * @param  data 接收到的单字节数据
  * @note   当前先做最小缓存，不做复杂协议解析。
  *         后面等你页面和返回格式定下来，再在这里扩展。
  */
void HMI_RxByteCallback(uint8_t data)
{
  if (g_hmi_rx_len < HMI_RX_BUF_SIZE)
  {
    g_hmi_rx_buf[g_hmi_rx_len++] = data;
  }
  else
  {
    g_hmi_rx_len = 0U;
  }

  g_hmi_rx_flag = 1U;
}
