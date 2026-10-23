#pragma once

#include <stddef.h>
#include <stdint.h>

void audio_sample_callback(int16_t left, int16_t right);
size_t audio_sample_batch_callback(const int16_t* data, size_t frames);
void Audio_onSinkChanged(int sink_type);
void Audio_checkAndResetIfNeeded(void);

// Launch-time codec warm-up: opening the speaker's dmix configures the codec
// in the kernel (~130 ms of hw_params). Audio_startWarmup does that on a
// thread while video and the core load. Audio_joinWarmup waits for it before
// SND_init (alsa-lib is never used from both threads at once); Audio_endWarmup,
// once SDL's device is open on the same dmix, drops the extra handle.
void Audio_startWarmup(void);
void Audio_joinWarmup(void);
void Audio_endWarmup(void);
