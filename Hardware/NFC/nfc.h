#ifndef __NFC_H_
#define __NFC_H_

#include <stdint.h>

/**
  * @brief  初始化 PN532 模块，并完成一次上电自检。
  * @note   该函数会依次执行：
  *         1. 检查 I2C 地址是否有应答
  *         2. 读取 PN532 固件版本
  *         3. 发送 SAMConfiguration，让模块进入读卡工作模式
  *         4. 通过 UART4 输出详细调试信息
  */
void NFC_Init(void);

/**
  * @brief  周期性轮询 NFC 卡。
  * @note   该函数设计给主循环反复调用。
  *         当 PN532 初始化成功后，它会持续查询是否有 ISO14443A 卡靠近，
  *         并且只在“新卡出现”或“卡片离开”时打印状态，避免串口刷屏。
  */
void NFC_Task(void);

/**
  * @brief  查询 PN532 是否已经完成初始化并进入可读卡状态。
  * @retval 1 表示可以正常轮询卡片，0 表示初始化尚未成功
  */
uint8_t NFC_IsReady(void);

/**
  * @brief  获取一次“新刷到的卡”事件。
  * @param  uid     输出 UID 缓冲区
  * @param  uid_len 输出 UID 长度
  * @retval 1 表示本次调用取到了一个新的刷卡事件，0 表示没有新事件
  * @note   该接口只在检测到新卡时返回一次，避免主循环重复处理同一张卡。
  */
uint8_t NFC_FetchNewCard(uint8_t *uid, uint8_t *uid_len);

#endif

