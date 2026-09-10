#pragma once

#include <stdint.h>
#include <stdbool.h>

// CYD P4 speaker via SC8002B on GPIO 26. LEDC channel 2 (not DAC2:
// DAC shares ADC2 with WiFi and drops the radio).

enum class AudioCue {
  Click,
  Swipe,
  Notify,
  NotifyHigh,
  Mesh,
  Alert,
};

#define AUDIO_VOL_MAX 10

void audio_begin();
void audio_set_volume(uint8_t vol);
uint8_t audio_volume();
void audio_play(AudioCue cue);
void audio_play_pcm_u8(const uint8_t *samples, uint16_t n, uint16_t rate);
bool audio_busy();
void audio_loop();
