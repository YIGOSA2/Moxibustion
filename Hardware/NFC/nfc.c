#include "nfc.h"
#include "bsp_system.h"
#include "gui.h"

/* PN532 的 I2C 固定地址以及协议相关常量。 */
#define PN532_I2C_ADDR_7BIT            0x24U
#define PN532_I2C_ADDR                 (PN532_I2C_ADDR_7BIT << 1)
#define PN532_PREAMBLE                 0x00U
#define PN532_STARTCODE1               0x00U
#define PN532_STARTCODE2               0xFFU
#define PN532_POSTAMBLE                0x00U
#define PN532_HOST_TO_PN532            0xD4U
#define PN532_PN532_TO_HOST            0xD5U
#define PN532_CMD_GET_FW_VERSION       0x02U
#define PN532_CMD_SAMCONFIG            0x14U
#define PN532_CMD_INLISTPASSIVETARGET  0x4AU
#define PN532_ACK_FRAME_LEN            6U
#define PN532_READY                    0x01U

/* 模块内部状态全部放在 nfc.c 内部，避免 main.c 依赖 PN532 的实现细节。 */
static uint8_t g_pn532_ack_ok = 0U;
static uint8_t g_pn532_sam_ok = 0U;
static uint32_t g_pn532_version = 0U;
static HAL_StatusTypeDef g_pn532_last_error = HAL_OK;
static uint8_t g_card_uid[7] = {0};
static uint8_t g_card_uid_len = 0U;
static uint8_t g_last_card_uid[7] = {0};
static uint8_t g_last_card_uid_len = 0U;
static uint8_t g_new_card_uid[7] = {0};
static uint8_t g_new_card_uid_len = 0U;
static uint8_t g_new_card_ready = 0U;

static HAL_StatusTypeDef PN532_WaitReady(uint32_t timeout_ms);
static HAL_StatusTypeDef PN532_WriteCommand(const uint8_t *cmd, uint8_t cmd_len);
static HAL_StatusTypeDef PN532_ReadAck(void);
static HAL_StatusTypeDef PN532_ReadFrame(uint8_t *buf, uint16_t len);
static HAL_StatusTypeDef PN532_GetFirmwareVersion(uint32_t *version);
static HAL_StatusTypeDef PN532_SAMConfiguration(void);
static HAL_StatusTypeDef PN532_InListPassiveTarget(uint8_t *uid, uint8_t *uid_len);

/**
  * @brief  等待 PN532 告知“响应帧已经准备好”。
  * @param  timeout_ms 最大等待时间，单位毫秒
  * @retval HAL_OK 表示状态字变成 0x01，其余为超时或通信错误
  * @note   PN532 在 I2C 模式下，在真正的数据帧之前会先返回一个 1 字节的状态值。
  *         先轮询这个状态字，可以避免在数据还没准备完成时提前去读帧。
  */
static HAL_StatusTypeDef PN532_WaitReady(uint32_t timeout_ms)
{
  uint8_t status = 0;
  uint32_t tick_start = HAL_GetTick();

  while ((HAL_GetTick() - tick_start) < timeout_ms)
  {
    if (HAL_I2C_Master_Receive(&hi2c1, PN532_I2C_ADDR, &status, 1, 20) == HAL_OK)
    {
      if (status == PN532_READY)
      {
        return HAL_OK;
      }
    }

    HAL_Delay(2);
  }

  return HAL_TIMEOUT;
}

/**
  * @brief  组装一帧完整的 PN532 命令并通过 I2C 发送。
  * @param  cmd     指向命令负载数据的指针
  * @param  cmd_len 命令负载长度
  * @retval HAL_I2C_Master_Transmit 返回的状态值
  * @note   PN532 命令帧格式为：
  *         Preamble + StartCode + LEN + LCS + TFI + DATA + DCS + Postamble
  *         这里的 DATA 以命令字节开头，而 TFI 固定为 0xD4，表示主机发往 PN532。
  */
static HAL_StatusTypeDef PN532_WriteCommand(const uint8_t *cmd, uint8_t cmd_len)
{
  uint8_t frame[32];
  uint8_t checksum = 0;
  uint8_t frame_len = (uint8_t)(cmd_len + 1U);
  uint8_t i = 0;

  if ((cmd == NULL) || (cmd_len == 0U) || (cmd_len > 24U))
  {
    return HAL_ERROR;
  }

  frame[0] = PN532_PREAMBLE;
  frame[1] = PN532_STARTCODE1;
  frame[2] = PN532_STARTCODE2;
  frame[3] = frame_len;
  frame[4] = (uint8_t)(~frame_len + 1U);
  frame[5] = PN532_HOST_TO_PN532;
  checksum = PN532_HOST_TO_PN532;

  for (i = 0; i < cmd_len; i++)
  {
    frame[6U + i] = cmd[i];
    checksum = (uint8_t)(checksum + cmd[i]);
  }

  frame[6U + cmd_len] = (uint8_t)(~checksum + 1U);
  frame[7U + cmd_len] = PN532_POSTAMBLE;

  return HAL_I2C_Master_Transmit(&hi2c1, PN532_I2C_ADDR, frame, (uint16_t)(cmd_len + 8U), 100);
}

/**
  * @brief  读取并校验 PN532 固定格式的 ACK 帧。
  * @retval HAL_OK 表示 ACK 内容严格等于 00 00 FF 00 FF 00
  * @note   每一条正确接收的 PN532 命令，通常都会先返回一个 ACK 帧，
  *         之后才会继续返回真正的数据响应帧。
  */
static HAL_StatusTypeDef PN532_ReadAck(void)
{
  static const uint8_t ack_expected[PN532_ACK_FRAME_LEN] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  uint8_t rx_buf[PN532_ACK_FRAME_LEN + 1U] = {0};
  HAL_StatusTypeDef ret;

  ret = PN532_WaitReady(100);
  if (ret != HAL_OK)
  {
    return ret;
  }

  ret = HAL_I2C_Master_Receive(&hi2c1, PN532_I2C_ADDR, rx_buf, sizeof(rx_buf), 100);
  if (ret != HAL_OK)
  {
    return ret;
  }

  if (memcmp(&rx_buf[1], ack_expected, PN532_ACK_FRAME_LEN) != 0)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

/**
  * @brief  读取一帧完整的 PN532 响应内容到调用者缓冲区。
  * @param  buf 数据接收缓冲区
  * @param  len 期望读取的 PN532 帧长度，不包含前面的 I2C 状态字节
  * @retval HAL_OK 表示成功收到完整帧
  * @note   PN532 在 I2C 模式下会在正式帧前额外附带 1 个状态字节，
  *         因此这里会多读 1 个字节，再把第 1 个字节丢掉，只返回真正的帧内容。
  */
static HAL_StatusTypeDef PN532_ReadFrame(uint8_t *buf, uint16_t len)
{
  uint8_t rx_buf[40] = {0};

  if ((buf == NULL) || (len == 0U) || (len > 38U))
  {
    return HAL_ERROR;
  }

  if (PN532_WaitReady(100) != HAL_OK)
  {
    return HAL_TIMEOUT;
  }

  if (HAL_I2C_Master_Receive(&hi2c1, PN532_I2C_ADDR, rx_buf, len + 1U, 100) != HAL_OK)
  {
    return HAL_ERROR;
  }

  memcpy(buf, &rx_buf[1], len);
  return HAL_OK;
}

/**
  * @brief  向 PN532 查询芯片标识和固件版本。
  * @param  version 输出 32 位原始版本值
  * @retval HAL_OK 表示响应帧内容有效
  * @note   这个函数通常作为协议层的第一步自检，用来确认命令链路已经通。
  */
static HAL_StatusTypeDef PN532_GetFirmwareVersion(uint32_t *version)
{
  uint8_t cmd[] = {PN532_CMD_GET_FW_VERSION};
  uint8_t resp[12] = {0};
  HAL_StatusTypeDef ret;

  if (version == NULL)
  {
    return HAL_ERROR;
  }

  ret = PN532_WriteCommand(cmd, sizeof(cmd));
  if (ret != HAL_OK)
  {
    return ret;
  }

  ret = PN532_ReadAck();
  if (ret != HAL_OK)
  {
    return ret;
  }

  g_pn532_ack_ok = 1U;

  ret = PN532_ReadFrame(resp, sizeof(resp));
  if (ret != HAL_OK)
  {
    return ret;
  }

  if ((resp[0] != 0x00U) || (resp[1] != 0x00U) || (resp[2] != 0xFFU) ||
      (resp[5] != PN532_PN532_TO_HOST) || (resp[6] != (uint8_t)(PN532_CMD_GET_FW_VERSION + 1U)))
  {
    return HAL_ERROR;
  }

  *version = ((uint32_t)resp[7] << 24) |
             ((uint32_t)resp[8] << 16) |
             ((uint32_t)resp[9] << 8)  |
             ((uint32_t)resp[10]);

  return HAL_OK;
}

/**
  * @brief  让 PN532 进入正常的读卡工作模式。
  * @retval HAL_OK 表示 SAMConfiguration 命令执行成功
  * @note   很多 PN532 模块如果不先执行这一步，就不会进入预期的
  *         Mifare / ISO14443A 被动目标轮询模式。
  */
static HAL_StatusTypeDef PN532_SAMConfiguration(void)
{
  uint8_t cmd[] = {PN532_CMD_SAMCONFIG, 0x01U, 0x14U, 0x00U};
  uint8_t resp[8] = {0};
  HAL_StatusTypeDef ret;

  ret = PN532_WriteCommand(cmd, sizeof(cmd));
  if (ret != HAL_OK)
  {
    return ret;
  }

  ret = PN532_ReadAck();
  if (ret != HAL_OK)
  {
    return ret;
  }

  ret = PN532_ReadFrame(resp, sizeof(resp));
  if (ret != HAL_OK)
  {
    return ret;
  }

  if ((resp[5] != PN532_PN532_TO_HOST) || (resp[6] != (uint8_t)(PN532_CMD_SAMCONFIG + 1U)))
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

/**
  * @brief  轮询一张 ISO14443A 卡并读取它的 UID。
  * @param  uid     UID 输出缓冲区
  * @param  uid_len UID 长度输出变量
  * @retval HAL_OK 表示检测到卡并成功解析出 UID
  * @note   这里使用的是 InListPassiveTarget 命令，参数含义为：
  *         MaxTg = 1，BrTy = 0x00，也就是 106 kbps 的 Type A 卡。
  */
static HAL_StatusTypeDef PN532_InListPassiveTarget(uint8_t *uid, uint8_t *uid_len)
{
  uint8_t cmd[] = {PN532_CMD_INLISTPASSIVETARGET, 0x01U, 0x00U};
  uint8_t resp[24] = {0};
  HAL_StatusTypeDef ret;
  uint8_t length;

  if ((uid == NULL) || (uid_len == NULL))
  {
    return HAL_ERROR;
  }

  *uid_len = 0U;

  ret = PN532_WriteCommand(cmd, sizeof(cmd));
  if (ret != HAL_OK)
  {
    return ret;
  }

  ret = PN532_ReadAck();
  if (ret != HAL_OK)
  {
    return ret;
  }

  ret = PN532_ReadFrame(resp, sizeof(resp));
  if (ret != HAL_OK)
  {
    return ret;
  }

  if ((resp[5] != PN532_PN532_TO_HOST) ||
      (resp[6] != (uint8_t)(PN532_CMD_INLISTPASSIVETARGET + 1U)) ||
      (resp[7] != 0x01U))
  {
    return HAL_ERROR;
  }

  length = resp[12];
  if ((length == 0U) || (length > 7U))
  {
    return HAL_ERROR;
  }

  memcpy(uid, &resp[13], length);
  *uid_len = length;
  return HAL_OK;
}

/**
  * @brief  执行一整套 PN532 上电初始化流程，并打印每一步结果。
  * @note   所有探测和建链细节都封装在 NFC 模块内部，
  *         这样 main.c 只需要调用一个高层初始化入口即可。
  */
void NFC_Init(void)
{
  g_pn532_ack_ok = 0U;
  g_pn532_sam_ok = 0U;
  g_pn532_version = 0U;
  g_pn532_last_error = HAL_OK;
  g_card_uid_len = 0U;
  g_last_card_uid_len = 0U;
  g_new_card_uid_len = 0U;
  g_new_card_ready = 0U;
  memset(g_card_uid, 0, sizeof(g_card_uid));
  memset(g_last_card_uid, 0, sizeof(g_last_card_uid));
  memset(g_new_card_uid, 0, sizeof(g_new_card_uid));

  GUI_Print("\r\n==== PN532 I2C Probe Start ====\r\n");
  GUI_Print("UART4 ready. I2C1 ready.\r\n");
  GUI_Print("Target address: 0x24 (7-bit), 0x48 (8-bit write)\r\n");
  HAL_Delay(50);
  GUI_Print("Checking I2C address response...\r\n");

  if (HAL_I2C_IsDeviceReady(&hi2c1, PN532_I2C_ADDR, 3, 100) == HAL_OK)
  {
    GUI_Print("PN532 ACKed on I2C bus.\r\n");

    g_pn532_last_error = PN532_GetFirmwareVersion(&g_pn532_version);
    if (g_pn532_last_error == HAL_OK)
    {
      GUI_Print("GetFirmwareVersion OK.\r\n");
      GUI_Printf("Version raw: 0x%08lX\r\n", g_pn532_version);
      GUI_Printf("Chip: PN5%lu, Firmware: %lu.%lu\r\n",
                 (g_pn532_version >> 24) & 0xFFUL,
                 (g_pn532_version >> 16) & 0xFFUL,
                 (g_pn532_version >> 8) & 0xFFUL);

      g_pn532_last_error = PN532_SAMConfiguration();
      if (g_pn532_last_error == HAL_OK)
      {
        g_pn532_sam_ok = 1U;
        GUI_Print("SAMConfiguration OK.\r\n");
        GUI_Print("PN532 I2C communication established.\r\n");
        GUI_Print("Bring an ISO14443A card close to the antenna...\r\n");
      }
      else
      {
        GUI_Printf("SAMConfiguration failed, HAL status=%d\r\n", g_pn532_last_error);
      }
    }
    else
    {
      GUI_Printf("GetFirmwareVersion failed, HAL status=%d\r\n", g_pn532_last_error);
      GUI_Printf("ACK received flag: %u\r\n", g_pn532_ack_ok);
    }
  }
  else
  {
    g_pn532_last_error = HAL_ERROR;
    GUI_Print("No response from PN532 on I2C bus.\r\n");
    GUI_Print("Check power, mode switch, wiring, and GND.\r\n");
  }

  GUI_Print("==== Probe Done ====\r\n");
}

/**
  * @brief  查询 NFC 模块是否已经进入可读卡状态。
  * @retval 1 表示 SAMConfiguration 已成功，0 表示尚未就绪
  */
uint8_t NFC_IsReady(void)
{
  return g_pn532_sam_ok;
}

/**
  * @brief  获取一次新的刷卡事件。
  */
uint8_t NFC_FetchNewCard(uint8_t *uid, uint8_t *uid_len)
{
  if ((uid == NULL) || (uid_len == NULL))
  {
    return 0U;
  }

  if (g_new_card_ready == 0U)
  {
    return 0U;
  }

  memcpy(uid, g_new_card_uid, g_new_card_uid_len);
  *uid_len = g_new_card_uid_len;
  g_new_card_ready = 0U;
  return 1U;
}

/**
  * @brief  轮询一次当前卡片状态。
  * @note   这个函数应当在主循环里反复调用。
  *         它只在两类“状态边沿”出现时打印信息：
  *         1. 发现一张新卡
  *         2. 原先检测到的卡已经移开
  *         这样既能保留关键状态变化，又不会让串口日志持续刷屏。
  */
void NFC_Task(void)
{
  if (g_pn532_sam_ok != 1U)
  {
    return;
  }

  HAL_Delay(300);

  if (PN532_InListPassiveTarget(g_card_uid, &g_card_uid_len) == HAL_OK)
  {
    if ((g_card_uid_len != g_last_card_uid_len) ||
        (memcmp(g_card_uid, g_last_card_uid, g_card_uid_len) != 0))
    {
      memcpy(g_last_card_uid, g_card_uid, g_card_uid_len);
      g_last_card_uid_len = g_card_uid_len;
      memcpy(g_new_card_uid, g_card_uid, g_card_uid_len);
      g_new_card_uid_len = g_card_uid_len;
      g_new_card_ready = 1U;
      GUI_Printf("Card detected, UID len=%u, UID=", g_card_uid_len);
      GUI_PrintHex(g_card_uid, g_card_uid_len);
    }
  }
  else
  {
    if (g_last_card_uid_len != 0U)
    {
      g_last_card_uid_len = 0U;
      GUI_Print("Card removed or polling timeout.\r\n");
    }
  }
}

