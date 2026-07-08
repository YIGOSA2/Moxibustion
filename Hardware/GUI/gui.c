#include "gui.h"
#include "bsp_system.h"

#define GUI_HMI_RX_BUF_SIZE  128U

/* 当前 UART4 已切换给串口屏使用，因此默认关闭调试文本输出。 */
static uint8_t g_gui_debug_enable = 0U;
static uint8_t g_gui_hmi_rx_buf[GUI_HMI_RX_BUF_SIZE] = {0};
static uint16_t g_gui_hmi_rx_len = 0U;
static uint8_t g_gui_hmi_rx_flag = 0U;

typedef enum
{
  GUI_HMI_RX_HEAD1 = 0,
  GUI_HMI_RX_HEAD2,
  GUI_HMI_RX_CMD,
  GUI_HMI_RX_LEN,
  GUI_HMI_RX_DATA,
  GUI_HMI_RX_CHECK
} GUI_HMI_RxState_t;

static GUI_HMI_RxState_t g_gui_hmi_rx_state = GUI_HMI_RX_HEAD1;
static uint8_t g_gui_hmi_cmd = 0U;
static uint8_t g_gui_hmi_len_expect = 0U;
static uint8_t g_gui_hmi_data_buf[16] = {0};
static uint8_t g_gui_hmi_data_len = 0U;
static uint8_t g_gui_hmi_xor = 0U;

static uint8_t g_gui_hmi_param_ready = 0U;
static uint8_t g_gui_hmi_set_temp = 0U;
static uint16_t g_gui_hmi_set_work_time = 0U;
static uint16_t g_gui_hmi_set_sit_time = 0U;
static uint8_t g_gui_hmi_start_ready = 0U;
static uint8_t g_gui_hmi_stop_ready = 0U;

static void GUI_HMI_SendTerminator(void);
static void GUI_HMI_SendByte(uint8_t data);
static void GUI_HMI_ResetParser(void);
static void GUI_HMI_ParseByte(uint8_t data);

/**
  * @brief  通过 UART4 发送一段普通字符串。
  * @param  text 需要发送的字符串
  * @note   这是当前工程最底层的调试输出接口。
  *         格式化输出和十六进制输出最终都会落到这里，通过串口发出去。
  */
void GUI_Print(const char *text)
{
  if ((text == NULL) || (g_gui_debug_enable == 0U))
  {
    return;
  }

  HAL_UART_Transmit(&huart4, (uint8_t *)text, (uint16_t)strlen(text), 1000);
}

/**
  * @brief  通过 UART4 输出格式化字符串。
  * @param  fmt 标准 printf 风格的格式串
  * @note   这里使用固定长度的局部缓冲区，因为当前工程只需要输出较短的调试文本。
  *         这样实现简单，也避免在单片机环境中引入动态内存相关问题。
  */
void GUI_Printf(const char *fmt, ...)
{
  char buf[128];
  va_list args;
  int len;

  if ((fmt == NULL) || (g_gui_debug_enable == 0U))
  {
    return;
  }

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (len <= 0)
  {
    return;
  }

  if (len >= (int)sizeof(buf))
  {
    len = (int)sizeof(buf) - 1;
  }

  HAL_UART_Transmit(&huart4, (uint8_t *)buf, (uint16_t)len, 1000);
}

/**
  * @brief  以十六进制形式输出一段短字节数据，例如 UID。
  * @param  buf 输入字节数组
  * @param  len 需要输出的字节数
  * @note   当前 PN532 读到的 UID 最长不会超过 7 字节，
  *         所以这里的局部输出缓冲区就是按这个场景加上换行结尾来设计的。
  */
void GUI_PrintHex(const uint8_t *buf, uint8_t len)
{
  static const char hex[] = "0123456789ABCDEF";
  char out[3 * 7 + 3];
  uint8_t i;
  uint8_t pos = 0;

  if (g_gui_debug_enable == 0U)
  {
    return;
  }

  if ((buf == NULL) || (len == 0U))
  {
    GUI_Print("(none)\r\n");
    return;
  }

  for (i = 0; i < len && i < 7U; i++)
  {
    out[pos++] = hex[(buf[i] >> 4) & 0x0FU];
    out[pos++] = hex[buf[i] & 0x0FU];
    if (i + 1U < len)
    {
      out[pos++] = ' ';
    }
  }

  out[pos++] = '\r';
  out[pos++] = '\n';
  out[pos] = '\0';
  GUI_Print(out);
}

/**
  * @brief  向 UART4 显式发送 1 个字节。
  * @param  data 要发送的原始字节
  */
static void GUI_HMI_SendByte(uint8_t data)
{
  HAL_UART_Transmit(&huart4, &data, 1U, 1000U);
}

/**
  * @brief  复位参数帧解析状态机。
  */
static void GUI_HMI_ResetParser(void)
{
  g_gui_hmi_rx_state = GUI_HMI_RX_HEAD1;
  g_gui_hmi_cmd = 0U;
  g_gui_hmi_len_expect = 0U;
  g_gui_hmi_data_len = 0U;
  g_gui_hmi_xor = 0U;
  memset(g_gui_hmi_data_buf, 0, sizeof(g_gui_hmi_data_buf));
}

/**
  * @brief  解析串口屏返回的参数设置帧。
  * @note   当前协议固定为：
  *         AA 55 01 03 [temp][work_time][sit_time] [xor]
  *         校验：cmd ^ len ^ 所有数据字节
  */
static void GUI_HMI_ParseByte(uint8_t data)
{
  switch (g_gui_hmi_rx_state)
  {
    case GUI_HMI_RX_HEAD1:
      if (data == 0xAAU)
      {
        g_gui_hmi_rx_state = GUI_HMI_RX_HEAD2;
      }
      break;

    case GUI_HMI_RX_HEAD2:
      if (data == 0x55U)
      {
        g_gui_hmi_rx_state = GUI_HMI_RX_CMD;
      }
      else
      {
        GUI_HMI_ResetParser();
      }
      break;

    case GUI_HMI_RX_CMD:
      g_gui_hmi_cmd = data;
      g_gui_hmi_xor = data;
      g_gui_hmi_rx_state = GUI_HMI_RX_LEN;
      break;

    case GUI_HMI_RX_LEN:
      g_gui_hmi_len_expect = data;
      g_gui_hmi_xor ^= data;
      g_gui_hmi_data_len = 0U;

      if (g_gui_hmi_len_expect > sizeof(g_gui_hmi_data_buf))
      {
        GUI_HMI_ResetParser();
      }
      else if (g_gui_hmi_len_expect == 0U)
      {
        g_gui_hmi_rx_state = GUI_HMI_RX_CHECK;
      }
      else
      {
        g_gui_hmi_rx_state = GUI_HMI_RX_DATA;
      }
      break;

    case GUI_HMI_RX_DATA:
      g_gui_hmi_data_buf[g_gui_hmi_data_len++] = data;
      g_gui_hmi_xor ^= data;

      if (g_gui_hmi_data_len >= g_gui_hmi_len_expect)
      {
        g_gui_hmi_rx_state = GUI_HMI_RX_CHECK;
      }
      break;

    case GUI_HMI_RX_CHECK:
      if ((data == g_gui_hmi_xor) && (g_gui_hmi_cmd == 0x01U) && (g_gui_hmi_len_expect == 3U))
      {
        g_gui_hmi_set_temp = g_gui_hmi_data_buf[0];
        g_gui_hmi_set_work_time = g_gui_hmi_data_buf[1];
        g_gui_hmi_set_sit_time = g_gui_hmi_data_buf[2];
        g_gui_hmi_param_ready = 1U;
      }
      else if ((data == g_gui_hmi_xor) && (g_gui_hmi_cmd == 0x02U) && (g_gui_hmi_len_expect == 0U))
      {
        g_gui_hmi_start_ready = 1U;
      }
      else if ((data == g_gui_hmi_xor) && (g_gui_hmi_cmd == 0x03U) && (g_gui_hmi_len_expect == 0U))
      {
        g_gui_hmi_stop_ready = 1U;
      }
      GUI_HMI_ResetParser();
      break;

    default:
      GUI_HMI_ResetParser();
      break;
  }
}

/**
  * @brief  发送串口屏协议结束符。
  */
static void GUI_HMI_SendTerminator(void)
{
  GUI_HMI_SendByte(0xFF);
  GUI_HMI_SendByte(0xFF);
  GUI_HMI_SendByte(0xFF);
}

/**
  * @brief  初始化串口屏基础层。
  */
void GUI_HMI_Init(void)
{
  GUI_HMI_ClearRxBuffer();
  GUI_HMI_ResetParser();
  /* 串口屏接收采用 UART4 的逐字节中断方式。
     这里只显式打开 RXNE 中断，否则虽然 UART4 已经初始化，
     但屏幕发来的数据不会主动进入 UART4_IRQHandler。 */
  __HAL_UART_ENABLE_IT(&huart4, UART_IT_RXNE);
  GUI_Print("GUI HMI layer ready.\r\n");
}

/**
  * @brief  串口屏任务轮询入口。
  */
void GUI_HMI_Task(void)
{
}

/**
  * @brief  向串口屏发送一条完整文本命令。
  */
void GUI_HMI_SendCmd(const char *cmd)
{
  uint16_t i;

  if (cmd == NULL)
  {
    return;
  }

  for (i = 0; i < (uint16_t)strlen(cmd); i++)
  {
    GUI_HMI_SendByte((uint8_t)cmd[i]);
  }

  GUI_HMI_SendTerminator();
}

/**
  * @brief  切换串口屏页面。
  */
void GUI_HMI_SetPage(const char *page_name)
{
  char cmd[64];

  if (page_name == NULL)
  {
    return;
  }

  snprintf(cmd, sizeof(cmd), "page %s", page_name);
  GUI_HMI_SendCmd(cmd);
}

/**
  * @brief  设置串口屏数值控件的 val。
  */
void GUI_HMI_SetValue(const char *obj, int32_t value)
{
  char cmd[64];

  if (obj == NULL)
  {
    return;
  }

  snprintf(cmd, sizeof(cmd), "%s.val=%ld", obj, (long)value);
  GUI_HMI_SendCmd(cmd);
}

/**
  * @brief  设置串口屏文本控件的 txt。
  */
void GUI_HMI_SetText(const char *obj, const char *text)
{
  char cmd[96];

  if ((obj == NULL) || (text == NULL))
  {
    return;
  }

  snprintf(cmd, sizeof(cmd), "%s.txt=\"%s\"", obj, text);
  GUI_HMI_SendCmd(cmd);
}

/**
  * @brief  查询是否收到新的屏幕返回数据。
  */
uint8_t GUI_HMI_GetRxFlag(void)
{
  return g_gui_hmi_rx_flag;
}

/**
  * @brief  获取当前接收缓冲区长度。
  */
uint16_t GUI_HMI_GetRxLength(void)
{
  return g_gui_hmi_rx_len;
}

/**
  * @brief  获取当前接收缓冲区首地址。
  */
uint8_t *GUI_HMI_GetRxBuffer(void)
{
  return g_gui_hmi_rx_buf;
}

/**
  * @brief  清空屏幕接收缓冲区。
  */
void GUI_HMI_ClearRxBuffer(void)
{
  memset(g_gui_hmi_rx_buf, 0, sizeof(g_gui_hmi_rx_buf));
  g_gui_hmi_rx_len = 0U;
  g_gui_hmi_rx_flag = 0U;
}
/**
  * @brief  UART4 接收到 1 字节时喂给 GUI 层的屏幕缓冲区。
  */
void GUI_HMI_RxByteCallback(uint8_t data)
{
  if (g_gui_hmi_rx_len < GUI_HMI_RX_BUF_SIZE)
  {
    g_gui_hmi_rx_buf[g_gui_hmi_rx_len++] = data;
  }
  else
  {
    g_gui_hmi_rx_len = 0U;
  }

  g_gui_hmi_rx_flag = 1U;
  GUI_HMI_ParseByte(data);
}

/**
  * @brief  取出一次屏幕发送过来的设置参数结果。
  * @retval 1 表示成功取到一帧新参数，0 表示当前没有新参数
  */
uint8_t GUI_HMI_FetchSetParam(uint8_t *temp, uint16_t *work_time, uint16_t *sit_time)
{
  if ((temp == NULL) || (work_time == NULL) || (sit_time == NULL))
  {
    return 0U;
  }

  if (g_gui_hmi_param_ready == 0U)
  {
    return 0U;
  }

  *temp = g_gui_hmi_set_temp;
  *work_time = g_gui_hmi_set_work_time;
  *sit_time = g_gui_hmi_set_sit_time;
  g_gui_hmi_param_ready = 0U;
  return 1U;
}

uint8_t GUI_HMI_GetCachedParam(uint8_t *temp, uint16_t *work_time, uint16_t *sit_time)
{
  if ((temp == NULL) || (work_time == NULL) || (sit_time == NULL))
  {
    return 0U;
  }

  if ((g_gui_hmi_set_temp == 0U) && (g_gui_hmi_set_work_time == 0U) && (g_gui_hmi_set_sit_time == 0U))
  {
    return 0U;
  }

  *temp = g_gui_hmi_set_temp;
  *work_time = g_gui_hmi_set_work_time;
  *sit_time = g_gui_hmi_set_sit_time;
  return 1U;
}

uint8_t GUI_HMI_FetchStartCommand(void)
{
  if (g_gui_hmi_start_ready == 0U)
  {
    return 0U;
  }

  g_gui_hmi_start_ready = 0U;
  return 1U;
}

uint8_t GUI_HMI_FetchStopCommand(void)
{
  if (g_gui_hmi_stop_ready == 0U)
  {
    return 0U;
  }

  g_gui_hmi_stop_ready = 0U;
  return 1U;
}
