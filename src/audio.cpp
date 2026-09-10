#include "audio.h"
#include "hardware.h"

#include <Arduino.h>
#include <string.h>

// LEDC PWM on GPIO 26 (not DAC2). DAC shares ADC2 with the WiFi radio
// and drops the STA link after a while on ESP32.

#define SPEAKER_CH 2
#define SPEAKER_RES 10

struct AudioStep {
  uint16_t hz;
  uint16_t ms;
  uint8_t duty;
};

static uint8_t volume = 7;
static const AudioStep *phrase = nullptr;
static uint8_t phrase_len = 0;
static uint8_t phrase_i = 0;
static uint8_t phrase_pri = 0;
static uint32_t step_started = 0;
static uint16_t step_ms = 0;
static bool sounding = false;
static bool pin_attached = false;
static volatile bool pcm_run = false;
static volatile uint16_t pcm_i = 0;
static uint16_t pcm_n = 0;
static uint8_t pcm_buf[24000];
static TaskHandle_t pcm_task_h = nullptr;

static uint8_t scale_pcm(uint8_t sample) {
  int v = static_cast<int>(sample) - 128;
  v = v * static_cast<int>(volume) / AUDIO_VOL_MAX;
  int duty = 128 + v;
  if (duty < 0) {
    duty = 0;
  }
  if (duty > 255) {
    duty = 255;
  }
  return static_cast<uint8_t>(duty);
}

static void pcm_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint32_t next = micros();
    while (pcm_run && pcm_i < pcm_n) {
      ledcWrite(SPEAKER_CH, scale_pcm(pcm_buf[pcm_i]));
      pcm_i = static_cast<uint16_t>(pcm_i + 1);
      next += 125;
      int32_t wait = static_cast<int32_t>(next - micros());
      if (wait > 2) {
        delayMicroseconds(static_cast<uint32_t>(wait));
      }
    }
    pcm_run = false;
  }
}

static void speaker_off() {
  if (pin_attached) {
    ledcWrite(SPEAKER_CH, 0);
    ledcDetachPin(CYD_SPEAKER_PIN);
    pin_attached = false;
  }
  pinMode(CYD_SPEAKER_PIN, OUTPUT);
  digitalWrite(CYD_SPEAKER_PIN, LOW);
}

static uint32_t scaled_duty(uint8_t duty) {
  if (volume == 0 || duty == 0) {
    return 0;
  }
  // 10-bit peak of the 8-bit cue duty, then quadratic volume so step 1
  // is a very short pulse instead of a 3.3 V square.
  uint32_t peak = static_cast<uint32_t>(duty) * 4;
  uint32_t d = peak * volume * volume / (AUDIO_VOL_MAX * AUDIO_VOL_MAX);
  if (d < 1) {
    d = 1;
  }
  if (d > 1023) {
    d = 1023;
  }
  return d;
}

static void speaker_pwm(uint16_t hz, uint8_t duty) {
  uint32_t d = scaled_duty(duty);
  if (hz == 0 || d == 0) {
    speaker_off();
    return;
  }
  ledcSetup(SPEAKER_CH, hz, SPEAKER_RES);
  ledcAttachPin(CYD_SPEAKER_PIN, SPEAKER_CH);
  pin_attached = true;
  ledcWrite(SPEAKER_CH, d);
}

static void start_step(uint8_t i) {
  phrase_i = i;
  if (phrase == nullptr || i >= phrase_len) {
    phrase = nullptr;
    sounding = false;
    phrase_pri = 0;
    speaker_off();
    return;
  }
  const AudioStep *s = &phrase[i];
  step_started = millis();
  step_ms = s->ms;
  sounding = true;
  if (s->hz == 0) {
    speaker_off();
  } else {
    speaker_pwm(s->hz, s->duty);
  }
}

void audio_begin() {
  pinMode(CYD_SPEAKER_PIN, OUTPUT);
  digitalWrite(CYD_SPEAKER_PIN, LOW);
}

void audio_set_volume(uint8_t vol) {
  if (vol > AUDIO_VOL_MAX) {
    vol = AUDIO_VOL_MAX;
  }
  volume = vol;
  if (vol == 0) {
    phrase = nullptr;
    sounding = false;
    phrase_pri = 0;
    pcm_run = false;
    speaker_off();
  }
}

uint8_t audio_volume() {
  return volume;
}

bool audio_busy() {
  return sounding || pcm_run;
}

void audio_play_pcm_u8(const uint8_t *samples, uint16_t n, uint16_t rate) {
  (void)rate;
  if (samples == nullptr || n < 16 || volume == 0) {
    return;
  }
  phrase = nullptr;
  sounding = false;
  phrase_pri = 0;
  pcm_run = false;
  if (n > sizeof(pcm_buf)) {
    n = static_cast<uint16_t>(sizeof(pcm_buf));
  }
  memcpy(pcm_buf, samples, n);
  pcm_n = n;
  pcm_i = 0;
  speaker_off();
  ledcSetup(SPEAKER_CH, 32000, 8);
  ledcAttachPin(CYD_SPEAKER_PIN, SPEAKER_CH);
  pin_attached = true;
  if (pcm_task_h == nullptr) {
    xTaskCreatePinnedToCore(pcm_task, "pcm", 2048, nullptr, 5, &pcm_task_h, 1);
  }
  pcm_run = true;
  xTaskNotifyGive(pcm_task_h);
}

void audio_loop() {
  if (pcm_run) {
    return;
  }
  if (pcm_n != 0 && pcm_i >= pcm_n) {
    pcm_n = 0;
    pcm_i = 0;
    speaker_off();
    return;
  }
  if (!sounding || phrase == nullptr) {
    return;
  }
  if (millis() - step_started < step_ms) {
    return;
  }
  start_step(static_cast<uint8_t>(phrase_i + 1));
}

static uint8_t pri_of(AudioCue cue) {
  switch (cue) {
    case AudioCue::NotifyHigh:
      return 6;
    case AudioCue::Notify:
      return 5;
    case AudioCue::Mesh:
      return 4;
    case AudioCue::Alert:
      return 3;
    case AudioCue::Swipe:
      return 2;
    case AudioCue::Click:
    default:
      return 1;
  }
}

void audio_play(AudioCue cue) {
  if (volume == 0 || pcm_run) {
    return;
  }
  uint8_t pri = pri_of(cue);
  if (sounding && pri < phrase_pri) {
    return;
  }

  static const AudioStep kClick[] = {{2400, 12, 90}};
  static const AudioStep kSwipe[] = {{1700, 16, 82}, {1100, 14, 64}};
  static const AudioStep kNotify[] = {{880, 70, 125}, {0, 24, 0}, {1175, 130, 145}};
  static const AudioStep kNotifyHigh[] = {{988, 70, 150}, {0, 36, 0}, {988, 70, 150}, {0, 36, 0}, {1319, 160, 170}};
  static const AudioStep kMesh[] = {{698, 55, 115}, {932, 100, 135}};
  static const AudioStep kAlert[] = {{1397, 48, 120}};

  switch (cue) {
    case AudioCue::Swipe:
      phrase = kSwipe;
      phrase_len = 2;
      break;
    case AudioCue::Notify:
      phrase = kNotify;
      phrase_len = 3;
      break;
    case AudioCue::NotifyHigh:
      phrase = kNotifyHigh;
      phrase_len = 5;
      break;
    case AudioCue::Mesh:
      phrase = kMesh;
      phrase_len = 2;
      break;
    case AudioCue::Alert:
      phrase = kAlert;
      phrase_len = 1;
      break;
    case AudioCue::Click:
    default:
      phrase = kClick;
      phrase_len = 1;
      break;
  }
  phrase_pri = pri;
  start_step(0);
}
