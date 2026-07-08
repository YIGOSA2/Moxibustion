#ifndef __VOICE_H_
#define __VOICE_H_

#include <stdint.h>

void VOICE_Init(void);
void VOICE_Stop(void);
void VOICE_PlayIndex(uint16_t index);
void VOICE_PlayFile(uint8_t folder, uint8_t file);
void VOICE_SetVolume(uint16_t volume);

#endif
