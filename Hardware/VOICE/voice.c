#include "voice.h"
#include "bsp_system.h"

#define VOICE_FRAME_HEAD       0x7EU
#define VOICE_FRAME_TAIL       0xEFU

#define VOICE_CMD_SELECT_DEV   0x19U
#define VOICE_CMD_PLAY_INDEX   0x13U
#define VOICE_CMD_PLAY_FILE    0x1FU
#define VOICE_CMD_SET_VOLUME   0x16U
#define VOICE_CMD_STOP         0x26U

#define VOICE_DEV_SPI_FLASH    0x04U

static void VOICE_SendFrame(uint8_t cmd, const uint8_t *data, uint16_t len);

static void VOICE_SendFrame(uint8_t cmd, const uint8_t *data, uint16_t len)
{
  uint8_t frame[32];
  uint16_t i;
  uint16_t pos = 0U;

  if (len > 24U)
  {
    return;
  }

  frame[pos++] = VOICE_FRAME_HEAD;
  frame[pos++] = cmd;
  frame[pos++] = (uint8_t)((len >> 8) & 0xFFU);
  frame[pos++] = (uint8_t)(len & 0xFFU);

  for (i = 0U; i < len; i++)
  {
    frame[pos++] = data[i];
  }

  frame[pos++] = VOICE_FRAME_TAIL;

  HAL_UART_Transmit(&huart3, frame, pos, 1000U);
}

void VOICE_Init(void)
{
  uint8_t data[2];

  data[0] = 0x00U;
  data[1] = VOICE_DEV_SPI_FLASH;
  VOICE_SendFrame(VOICE_CMD_SELECT_DEV, data, 2U);
  HAL_Delay(200);
}

void VOICE_Stop(void)
{
  VOICE_SendFrame(VOICE_CMD_STOP, NULL, 0U);
}

void VOICE_PlayIndex(uint16_t index)
{
  uint8_t data[2];

  data[0] = (uint8_t)((index >> 8) & 0xFFU);
  data[1] = (uint8_t)(index & 0xFFU);
  VOICE_SendFrame(VOICE_CMD_PLAY_INDEX, data, 2U);
}

void VOICE_PlayFile(uint8_t folder, uint8_t file)
{
  uint8_t dev_data[2];
  uint8_t play_data[2];

  dev_data[0] = 0x00U;
  dev_data[1] = VOICE_DEV_SPI_FLASH;
  VOICE_SendFrame(VOICE_CMD_SELECT_DEV, dev_data, 2U);
  HAL_Delay(200);

  play_data[0] = folder;
  play_data[1] = file;
  VOICE_SendFrame(VOICE_CMD_PLAY_FILE, play_data, 2U);
}

void VOICE_SetVolume(uint16_t volume)
{
  uint8_t data[2];

  data[0] = (uint8_t)((volume >> 8) & 0xFFU);
  data[1] = (uint8_t)(volume & 0xFFU);
  VOICE_SendFrame(VOICE_CMD_SET_VOLUME, data, 2U);
}
