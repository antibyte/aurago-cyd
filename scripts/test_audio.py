#!/usr/bin/env python3
"""Speaker pin, LEDC channel, clicks, and notification cues."""

from __future__ import annotations

import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class AudioTests(unittest.TestCase):
    def setUp(self) -> None:
        self.hw_h = (ROOT / "include" / "hardware.h").read_text(encoding="utf-8")
        self.audio_h = (ROOT / "include" / "audio.h").read_text(encoding="utf-8")
        self.audio_c = (ROOT / "src" / "audio.cpp").read_text(encoding="utf-8")
        self.hw_c = (ROOT / "src" / "hardware.cpp").read_text(encoding="utf-8")
        self.main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
        self.ui_c = (ROOT / "src" / "ui.cpp").read_text(encoding="utf-8")
        self.prov = (ROOT / "src" / "provision.cpp").read_text(encoding="utf-8")
        self.store_h = (ROOT / "include" / "config_store.h").read_text(encoding="utf-8")
        self.store_c = (ROOT / "src" / "config_store.cpp").read_text(encoding="utf-8")

    def test_speaker_uses_gpio26_not_backlight_channel(self) -> None:
        self.assertIn("#define CYD_SPEAKER_PIN 26", self.hw_h)
        self.assertIn("#define SPEAKER_CH 2", self.audio_c)
        self.assertNotIn("DAC_CHANNEL_2", self.audio_c)
        self.assertNotIn("dac_output_voltage", self.audio_c)
        self.assertNotIn("tone(CYD_SPEAKER_PIN", self.audio_c)
        self.assertNotIn("noTone(", self.audio_c)
        self.assertIn("ledcSetup(0, 5000, 8)", self.hw_c)
        self.assertIn("audio_begin()", self.hw_c)
        self.assertIn("ledcAttachPin(CYD_SPEAKER_PIN", self.audio_c)

    def test_cues_and_nonblocking_loop(self) -> None:
        for cue in ("Click", "Swipe", "Notify", "NotifyHigh", "Mesh", "Alert"):
            self.assertIn(cue, self.audio_h)
        self.assertIn("void audio_loop()", self.audio_h)
        self.assertIn("audio_loop()", self.main)
        self.assertIn("audio_loop()", self.prov)
        self.assertIn("audio_play(AudioCue::Click)", self.main)
        self.assertIn("audio_play(AudioCue::Click)", self.prov)
        self.assertIn("audio_play(AudioCue::Notify)", self.main)
        self.assertIn("audio_play(AudioCue::NotifyHigh)", self.main)
        self.assertIn("audio_play(AudioCue::Mesh)", self.main)
        self.assertIn("audio_play_pcm_u8", self.audio_h)
        self.assertIn("net_fetch_speak", self.main)

    def test_volume_persisted_and_settings_stepper(self) -> None:
        self.assertIn("uint8_t volume", self.store_h)
        self.assertIn('getUChar("vol"', self.store_c)
        self.assertIn('putUChar("vol"', self.store_c)
        self.assertIn("AUDIO_VOL_MAX", self.audio_h)
        self.assertIn("scaled_duty", self.audio_c)
        self.assertIn("volume * volume", self.audio_c)
        self.assertIn("audio_set_volume", self.audio_h)
        self.assertIn("audio_set_volume(cfg.volume)", self.main)
        self.assertIn("drawString(\"Vol\"", self.ui_c)
        self.assertIn("return 'q'", self.ui_c)
        self.assertIn("return 'u'", self.ui_c)
        self.assertIn("hit == 'q'", self.main)
        self.assertIn("hit == 'u'", self.main)


if __name__ == "__main__":
    unittest.main()
