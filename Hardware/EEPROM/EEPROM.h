#ifndef __EEPROM_
#define __EEPROM_

#include <stdint.h>

typedef struct
{
  uint8_t uid_len;
  uint8_t uid[7];
  uint8_t temp;
  uint16_t work_time;
  uint16_t sit_time;
  uint8_t reserve[3];
} EEPROM_CardRecord_t;

/* 单次使用记录：开始/结束时间(Unix 秒, UTC+8 由手机换算年月日)与使用时长(分钟)。 */
typedef struct
{
  uint32_t start_epoch;
  uint32_t end_epoch;
  uint16_t duration_min;
} EEPROM_SessionRecord_t;

void EEPROM_Init(void);
void EEPROM_ClearCardDatabase(void);
uint8_t EEPROM_GetCardCount(void);
uint8_t EEPROM_FindCardByUid(const uint8_t *uid, uint8_t uid_len, EEPROM_CardRecord_t *record, uint8_t *index);
void EEPROM_LoadDefaultRecord(const uint8_t *uid, uint8_t uid_len, EEPROM_CardRecord_t *record);
uint8_t EEPROM_SaveCardRecord(const EEPROM_CardRecord_t *record, uint8_t *index_out);

/* 使用记录日志：循环缓冲，满后覆盖最旧一条。index 0 表示最旧。 */
void EEPROM_ClearSessionLog(void);
uint8_t EEPROM_GetSessionCount(void);
uint8_t EEPROM_AppendSession(const EEPROM_SessionRecord_t *record);
uint8_t EEPROM_GetSessionByIndex(uint8_t index, EEPROM_SessionRecord_t *record);

#endif
