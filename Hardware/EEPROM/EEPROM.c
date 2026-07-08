#include "eeprom.h"
#include "bsp_system.h"

#define EEPROM_ADDR_7BIT        0x50U
#define EEPROM_ADDR             (EEPROM_ADDR_7BIT << 1)

#define EEPROM_ADDR_CARD_INFO   0x0000U
#define EEPROM_ADDR_RECORD_BASE 0x0010U
#define EEPROM_RECORD_SIZE      16U
/* 当前先预留 20 张卡的参数空间，后面 EEPROM 还要留给其他掉电数据使用。 */
#define EEPROM_MAX_CARD_COUNT   20U
#define EEPROM_DB_TOTAL_SIZE    (EEPROM_ADDR_RECORD_BASE + (EEPROM_RECORD_SIZE * EEPROM_MAX_CARD_COUNT))

/* 使用记录日志区：与卡片区隔开，起始 0x0200，循环缓冲 64 条 × 16 字节。 */
#define EEPROM_ADDR_SESSION_INFO 0x0200U
#define EEPROM_ADDR_SESSION_BASE 0x0210U
#define EEPROM_SESSION_SIZE      16U
#define EEPROM_MAX_SESSION_COUNT 3U

#define EEPROM_CARD_INFO_MAGIC  0x5AU
#define EEPROM_RECORD_MAGIC1    0xA5U
#define EEPROM_RECORD_MAGIC2    0x5AU
#define EEPROM_SESSION_INFO_MAGIC 0x3CU
#define EEPROM_SESSION_MAGIC1   0xC3U
#define EEPROM_SESSION_MAGIC2   0x3CU

static HAL_StatusTypeDef EEPROM_WaitReady(uint32_t timeout_ms);
static HAL_StatusTypeDef EEPROM_ReadBytes(uint16_t mem_addr, uint8_t *buf, uint16_t len);
static HAL_StatusTypeDef EEPROM_WriteBytes(uint16_t mem_addr, const uint8_t *buf, uint16_t len);
static uint16_t EEPROM_GetRecordAddr(uint8_t index);
static uint8_t EEPROM_UidMatch(const EEPROM_CardRecord_t *record, const uint8_t *uid, uint8_t uid_len);
static uint8_t EEPROM_CalcRecordCheck(const EEPROM_CardRecord_t *record);
static uint8_t EEPROM_CalcBufferCheck(const uint8_t *buf);
static void EEPROM_RecordToBuffer(const EEPROM_CardRecord_t *record, uint8_t *buf);
static void EEPROM_BufferToRecord(const uint8_t *buf, EEPROM_CardRecord_t *record);
static uint8_t EEPROM_RecordBufferIsValid(const uint8_t *buf);
static uint8_t EEPROM_FindRecordIndexByUid(const uint8_t *uid, uint8_t uid_len, EEPROM_CardRecord_t *record, uint8_t *index);
static uint8_t EEPROM_FindFirstEmptyIndex(uint8_t *index);
static uint8_t EEPROM_CountValidRecords(void);
static uint8_t EEPROM_ReadCardCountRaw(uint8_t *count);
static void EEPROM_SetCardCount(uint8_t count);

static HAL_StatusTypeDef EEPROM_WaitReady(uint32_t timeout_ms)
{
  return HAL_I2C_IsDeviceReady(&hi2c2, EEPROM_ADDR, 5U, timeout_ms);
}

static HAL_StatusTypeDef EEPROM_ReadBytes(uint16_t mem_addr, uint8_t *buf, uint16_t len)
{
  if ((buf == NULL) || (len == 0U))
  {
    return HAL_ERROR;
  }

  return HAL_I2C_Mem_Read(&hi2c2,
                          EEPROM_ADDR,
                          mem_addr,
                          I2C_MEMADD_SIZE_16BIT,
                          buf,
                          len,
                          200U);
}

static HAL_StatusTypeDef EEPROM_WriteBytes(uint16_t mem_addr, const uint8_t *buf, uint16_t len)
{
  HAL_StatusTypeDef ret;

  if ((buf == NULL) || (len == 0U))
  {
    return HAL_ERROR;
  }

  ret = HAL_I2C_Mem_Write(&hi2c2,
                          EEPROM_ADDR,
                          mem_addr,
                          I2C_MEMADD_SIZE_16BIT,
                          (uint8_t *)buf,
                          len,
                          200U);
  if (ret != HAL_OK)
  {
    return ret;
  }

  return EEPROM_WaitReady(100U);
}

static uint16_t EEPROM_GetRecordAddr(uint8_t index)
{
  return (uint16_t)(EEPROM_ADDR_RECORD_BASE + ((uint16_t)index * EEPROM_RECORD_SIZE));
}

static uint8_t EEPROM_UidMatch(const EEPROM_CardRecord_t *record, const uint8_t *uid, uint8_t uid_len)
{
  if ((record == NULL) || (uid == NULL))
  {
    return 0U;
  }

  if (record->uid_len != uid_len)
  {
    return 0U;
  }

  if (memcmp(record->uid, uid, uid_len) != 0)
  {
    return 0U;
  }

  return 1U;
}

static uint8_t EEPROM_CalcRecordCheck(const EEPROM_CardRecord_t *record)
{
  uint8_t check = 0U;
  uint8_t i;

  if (record == NULL)
  {
    return 0U;
  }

  check ^= record->uid_len;
  for (i = 0U; i < record->uid_len && i < 7U; i++)
  {
    check ^= record->uid[i];
  }
  check ^= record->temp;
  check ^= (uint8_t)((record->work_time >> 8) & 0xFFU);
  check ^= (uint8_t)(record->work_time & 0xFFU);
  check ^= (uint8_t)((record->sit_time >> 8) & 0xFFU);
  check ^= (uint8_t)(record->sit_time & 0xFFU);
  return check;
}

static uint8_t EEPROM_CalcBufferCheck(const uint8_t *buf)
{
  uint8_t check = 0U;
  uint8_t uid_len;
  uint8_t i;

  if (buf == NULL)
  {
    return 0U;
  }

  uid_len = buf[0];
  if ((uid_len == 0U) || (uid_len > 7U))
  {
    return 0U;
  }

  check ^= buf[0];
  for (i = 0U; i < uid_len; i++)
  {
    check ^= buf[1U + i];
  }
  check ^= buf[8];
  check ^= buf[9];
  check ^= buf[10];
  check ^= buf[11];
  check ^= buf[12];
  return check;
}

static void EEPROM_RecordToBuffer(const EEPROM_CardRecord_t *record, uint8_t *buf)
{
  if ((record == NULL) || (buf == NULL))
  {
    return;
  }

  memset(buf, 0, EEPROM_RECORD_SIZE);
  buf[0] = record->uid_len;
  memcpy(&buf[1], record->uid, 7U);
  buf[8] = record->temp;
  buf[9] = (uint8_t)((record->work_time >> 8) & 0xFFU);
  buf[10] = (uint8_t)(record->work_time & 0xFFU);
  buf[11] = (uint8_t)((record->sit_time >> 8) & 0xFFU);
  buf[12] = (uint8_t)(record->sit_time & 0xFFU);
  buf[13] = EEPROM_RECORD_MAGIC1;
  buf[14] = EEPROM_CalcRecordCheck(record);
  buf[15] = EEPROM_RECORD_MAGIC2;
}

static void EEPROM_BufferToRecord(const uint8_t *buf, EEPROM_CardRecord_t *record)
{
  if ((buf == NULL) || (record == NULL))
  {
    return;
  }

  memset(record, 0, sizeof(EEPROM_CardRecord_t));
  record->uid_len = buf[0];
  memcpy(record->uid, &buf[1], 7U);
  record->temp = buf[8];
  record->work_time = (uint16_t)((uint16_t)buf[9] << 8);
  record->work_time |= buf[10];
  record->sit_time = (uint16_t)((uint16_t)buf[11] << 8);
  record->sit_time |= buf[12];
  record->reserve[0] = buf[13];
  record->reserve[1] = buf[14];
  record->reserve[2] = buf[15];
}

static uint8_t EEPROM_RecordBufferIsValid(const uint8_t *buf)
{
  if (buf == NULL)
  {
    return 0U;
  }

  if ((buf[13] != EEPROM_RECORD_MAGIC1) || (buf[15] != EEPROM_RECORD_MAGIC2))
  {
    return 0U;
  }

  if ((buf[0] == 0U) || (buf[0] > 7U))
  {
    return 0U;
  }

  if (buf[14] != EEPROM_CalcBufferCheck(buf))
  {
    return 0U;
  }

  return 1U;
}

static uint8_t EEPROM_FindRecordIndexByUid(const uint8_t *uid, uint8_t uid_len, EEPROM_CardRecord_t *record, uint8_t *index)
{
  uint8_t i;
  uint8_t buf[EEPROM_RECORD_SIZE];
  EEPROM_CardRecord_t temp_record;

  if (uid == NULL)
  {
    return 0U;
  }

  for (i = 0U; i < EEPROM_MAX_CARD_COUNT; i++)
  {
    if (EEPROM_ReadBytes(EEPROM_GetRecordAddr(i), buf, EEPROM_RECORD_SIZE) != HAL_OK)
    {
      continue;
    }

    if (EEPROM_RecordBufferIsValid(buf) == 0U)
    {
      continue;
    }

    EEPROM_BufferToRecord(buf, &temp_record);
    if (EEPROM_UidMatch(&temp_record, uid, uid_len) == 1U)
    {
      if (record != NULL)
      {
        memcpy(record, &temp_record, sizeof(EEPROM_CardRecord_t));
      }

      if (index != NULL)
      {
        *index = i;
      }

      return 1U;
    }
  }

  return 0U;
}

static uint8_t EEPROM_FindFirstEmptyIndex(uint8_t *index)
{
  uint8_t i;
  uint8_t buf[EEPROM_RECORD_SIZE];

  if (index == NULL)
  {
    return 0U;
  }

  for (i = 0U; i < EEPROM_MAX_CARD_COUNT; i++)
  {
    if (EEPROM_ReadBytes(EEPROM_GetRecordAddr(i), buf, EEPROM_RECORD_SIZE) != HAL_OK)
    {
      continue;
    }

    if (EEPROM_RecordBufferIsValid(buf) == 0U)
    {
      *index = i;
      return 1U;
    }
  }

  return 0U;
}

static uint8_t EEPROM_CountValidRecords(void)
{
  uint8_t i;
  uint8_t count = 0U;
  uint8_t buf[EEPROM_RECORD_SIZE];

  for (i = 0U; i < EEPROM_MAX_CARD_COUNT; i++)
  {
    if (EEPROM_ReadBytes(EEPROM_GetRecordAddr(i), buf, EEPROM_RECORD_SIZE) != HAL_OK)
    {
      continue;
    }

    if (EEPROM_RecordBufferIsValid(buf) == 1U)
    {
      count++;
    }
  }

  return count;
}

static uint8_t EEPROM_ReadCardCountRaw(uint8_t *count)
{
  uint8_t info[3] = {0};

  if (count == NULL)
  {
    return 0U;
  }

  if (EEPROM_ReadBytes(EEPROM_ADDR_CARD_INFO, info, sizeof(info)) != HAL_OK)
  {
    return 0U;
  }

  if (info[2] != EEPROM_CARD_INFO_MAGIC)
  {
    return 0U;
  }

  if ((uint8_t)(info[0] ^ info[1]) != 0xFFU)
  {
    return 0U;
  }

  if (info[0] > EEPROM_MAX_CARD_COUNT)
  {
    return 0U;
  }

  *count = info[0];
  return 1U;
}

static void EEPROM_SetCardCount(uint8_t count)
{
  uint8_t info[3];

  info[0] = count;
  info[1] = (uint8_t)(count ^ 0xFFU);
  info[2] = EEPROM_CARD_INFO_MAGIC;
  (void)EEPROM_WriteBytes(EEPROM_ADDR_CARD_INFO, info, sizeof(info));
}

void EEPROM_ClearCardDatabase(void)
{
  uint8_t clear_buf[EEPROM_RECORD_SIZE] = {0};
  uint16_t addr = 0U;

  for (addr = 0U; addr < EEPROM_DB_TOTAL_SIZE; addr = (uint16_t)(addr + EEPROM_RECORD_SIZE))
  {
    (void)EEPROM_WriteBytes(addr, clear_buf, EEPROM_RECORD_SIZE);
  }
}

void EEPROM_Init(void)
{
  uint8_t card_count = 0U;

  if (EEPROM_WaitReady(100U) != HAL_OK)
  {
    return;
  }

  if (EEPROM_ReadCardCountRaw(&card_count) == 0U)
  {
    card_count = 0U;
    EEPROM_SetCardCount(card_count);
  }
}

uint8_t EEPROM_GetCardCount(void)
{
  uint8_t card_count = 0U;

  if (EEPROM_ReadCardCountRaw(&card_count) == 0U)
  {
    return EEPROM_CountValidRecords();
  }

  return card_count;
}

uint8_t EEPROM_FindCardByUid(const uint8_t *uid, uint8_t uid_len, EEPROM_CardRecord_t *record, uint8_t *index)
{
  if ((uid == NULL) || (record == NULL))
  {
    return 0U;
  }

  return EEPROM_FindRecordIndexByUid(uid, uid_len, record, index);
}

void EEPROM_LoadDefaultRecord(const uint8_t *uid, uint8_t uid_len, EEPROM_CardRecord_t *record)
{
  if ((uid == NULL) || (record == NULL))
  {
    return;
  }

  if (uid_len > 7U)
  {
    uid_len = 7U;
  }

  memset(record, 0, sizeof(EEPROM_CardRecord_t));
  record->uid_len = uid_len;
  memcpy(record->uid, uid, uid_len);
  record->temp = 20U;
  record->work_time = 0U;
  record->sit_time = 0U;
}

uint8_t EEPROM_SaveCardRecord(const EEPROM_CardRecord_t *record, uint8_t *index_out)
{
  uint8_t record_index = 0U;
  uint8_t buf[EEPROM_RECORD_SIZE];
  EEPROM_CardRecord_t temp_record;
  uint8_t valid_count;

  if (record == NULL)
  {
    return 0U;
  }

  if (EEPROM_FindRecordIndexByUid(record->uid, record->uid_len, &temp_record, &record_index) == 0U)
  {
    if (EEPROM_FindFirstEmptyIndex(&record_index) == 0U)
    {
      return 0U;
    }
  }

  EEPROM_RecordToBuffer(record, buf);
  if (EEPROM_WriteBytes(EEPROM_GetRecordAddr(record_index), buf, EEPROM_RECORD_SIZE) != HAL_OK)
  {
    return 0U;
  }

  valid_count = EEPROM_CountValidRecords();
  EEPROM_SetCardCount(valid_count);

  if (index_out != NULL)
  {
    *index_out = record_index;
  }

  return 1U;
}

/* ---------------- 使用记录日志（循环缓冲） ---------------- */

static uint16_t EEPROM_GetSessionAddr(uint8_t slot)
{
  return (uint16_t)(EEPROM_ADDR_SESSION_BASE + ((uint16_t)slot * EEPROM_SESSION_SIZE));
}

static uint8_t EEPROM_CalcSessionCheck(const uint8_t *buf)
{
  uint8_t check = 0U;
  uint8_t i;

  if (buf == NULL)
  {
    return 0U;
  }

  for (i = 0U; i < 10U; i++)
  {
    check ^= buf[i];
  }
  return check;
}

static void EEPROM_SessionToBuffer(const EEPROM_SessionRecord_t *record, uint8_t *buf)
{
  if ((record == NULL) || (buf == NULL))
  {
    return;
  }

  memset(buf, 0, EEPROM_SESSION_SIZE);
  buf[0] = (uint8_t)((record->start_epoch >> 24) & 0xFFU);
  buf[1] = (uint8_t)((record->start_epoch >> 16) & 0xFFU);
  buf[2] = (uint8_t)((record->start_epoch >> 8) & 0xFFU);
  buf[3] = (uint8_t)(record->start_epoch & 0xFFU);
  buf[4] = (uint8_t)((record->end_epoch >> 24) & 0xFFU);
  buf[5] = (uint8_t)((record->end_epoch >> 16) & 0xFFU);
  buf[6] = (uint8_t)((record->end_epoch >> 8) & 0xFFU);
  buf[7] = (uint8_t)(record->end_epoch & 0xFFU);
  buf[8] = (uint8_t)((record->duration_min >> 8) & 0xFFU);
  buf[9] = (uint8_t)(record->duration_min & 0xFFU);
  buf[13] = EEPROM_SESSION_MAGIC1;
  buf[14] = EEPROM_CalcSessionCheck(buf);
  buf[15] = EEPROM_SESSION_MAGIC2;
}

static void EEPROM_BufferToSession(const uint8_t *buf, EEPROM_SessionRecord_t *record)
{
  if ((buf == NULL) || (record == NULL))
  {
    return;
  }

  record->start_epoch = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                        ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
  record->end_epoch = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                      ((uint32_t)buf[6] << 8) | (uint32_t)buf[7];
  record->duration_min = (uint16_t)(((uint16_t)buf[8] << 8) | buf[9]);
}

static uint8_t EEPROM_ReadSessionInfo(uint8_t *head, uint8_t *count)
{
  uint8_t info[4] = {0};

  if ((head == NULL) || (count == NULL))
  {
    return 0U;
  }

  if (EEPROM_ReadBytes(EEPROM_ADDR_SESSION_INFO, info, sizeof(info)) != HAL_OK)
  {
    return 0U;
  }

  if (info[2] != EEPROM_SESSION_INFO_MAGIC)
  {
    return 0U;
  }

  if ((uint8_t)(info[0] ^ info[1] ^ info[2]) != info[3])
  {
    return 0U;
  }

  if ((info[0] >= EEPROM_MAX_SESSION_COUNT) || (info[1] > EEPROM_MAX_SESSION_COUNT))
  {
    return 0U;
  }

  *head = info[0];
  *count = info[1];
  return 1U;
}

static void EEPROM_WriteSessionInfo(uint8_t head, uint8_t count)
{
  uint8_t info[4];

  info[0] = head;
  info[1] = count;
  info[2] = EEPROM_SESSION_INFO_MAGIC;
  info[3] = (uint8_t)(info[0] ^ info[1] ^ info[2]);
  (void)EEPROM_WriteBytes(EEPROM_ADDR_SESSION_INFO, info, sizeof(info));
}

void EEPROM_ClearSessionLog(void)
{
  EEPROM_WriteSessionInfo(0U, 0U);
}

uint8_t EEPROM_GetSessionCount(void)
{
  uint8_t head = 0U;
  uint8_t count = 0U;

  if (EEPROM_ReadSessionInfo(&head, &count) == 0U)
  {
    return 0U;
  }

  return count;
}

uint8_t EEPROM_AppendSession(const EEPROM_SessionRecord_t *record)
{
  uint8_t head = 0U;
  uint8_t count = 0U;
  uint8_t buf[EEPROM_SESSION_SIZE];

  if (record == NULL)
  {
    return 0U;
  }

  if (EEPROM_ReadSessionInfo(&head, &count) == 0U)
  {
    head = 0U;
    count = 0U;
  }

  EEPROM_SessionToBuffer(record, buf);
  if (EEPROM_WriteBytes(EEPROM_GetSessionAddr(head), buf, EEPROM_SESSION_SIZE) != HAL_OK)
  {
    return 0U;
  }

  head = (uint8_t)((head + 1U) % EEPROM_MAX_SESSION_COUNT);
  if (count < EEPROM_MAX_SESSION_COUNT)
  {
    count++;
  }

  EEPROM_WriteSessionInfo(head, count);
  return 1U;
}

uint8_t EEPROM_GetSessionByIndex(uint8_t index, EEPROM_SessionRecord_t *record)
{
  uint8_t head = 0U;
  uint8_t count = 0U;
  uint8_t slot;
  uint8_t oldest;
  uint8_t buf[EEPROM_SESSION_SIZE];

  if (record == NULL)
  {
    return 0U;
  }

  if (EEPROM_ReadSessionInfo(&head, &count) == 0U)
  {
    return 0U;
  }

  if (index >= count)
  {
    return 0U;
  }

  /* count < MAX 时记录从 slot 0 起顺序存放；已满时最旧一条位于 head。 */
  oldest = (count < EEPROM_MAX_SESSION_COUNT) ? 0U : head;
  slot = (uint8_t)((oldest + index) % EEPROM_MAX_SESSION_COUNT);

  if (EEPROM_ReadBytes(EEPROM_GetSessionAddr(slot), buf, EEPROM_SESSION_SIZE) != HAL_OK)
  {
    return 0U;
  }

  if ((buf[13] != EEPROM_SESSION_MAGIC1) || (buf[15] != EEPROM_SESSION_MAGIC2))
  {
    return 0U;
  }

  if (buf[14] != EEPROM_CalcSessionCheck(buf))
  {
    return 0U;
  }

  EEPROM_BufferToSession(buf, record);
  return 1U;
}
