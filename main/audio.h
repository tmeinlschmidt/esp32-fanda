// audio.h — tiny tone player over I2S (MAX98357A).
// The play_* calls are BLOCKING: they synthesise and stream samples on the
// caller's task until the tone finishes. If a tone is already playing they
// return immediately (drop-on-busy). Caller is the input-polling task; the
// ~150–400 ms block is acceptable for human-input cadence.
#pragma once

void audio_init(void);
void audio_play_button_tone(void);
void audio_play_reed_tone(void);
