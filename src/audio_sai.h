#pragma once
#include <stdint.h>

#define AUDIO_BLOCK_SIZE 48
extern int32_t audio_tx_buf[];
extern volatile uint8_t req_fill_first_half;
extern volatile uint8_t req_fill_second_half;

void AudioInit();
void AudioStart();
