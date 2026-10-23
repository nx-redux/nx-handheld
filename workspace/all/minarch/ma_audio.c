#include "ma_internal.h"
#include "ma_audio.h"
#include "ma_rewind.h"
#include "ma_emutime.h"
#include <msettings.h>
#include "audio_manager.h"
#include <alsa/asoundlib.h>
#include <pthread.h>

void audio_sample_callback(int16_t left, int16_t right) {
	EmuTime_countAudio(1);
	if (rewinding && !rewind_ctx.audio)
		return;
	if (!fast_forward || ff_audio) {
		if (use_core_fps || fast_forward) {
			SND_batchSamples_fixed_rate(&(const SND_Frame){left, right}, 1);
		} else {
			SND_batchSamples(&(const SND_Frame){left, right}, 1);
		}
	}
}
// NX_AUDIO_BLOCK=1 (set by a pak's launch.sh): for cores that run emulation
// on their own thread and expect the frontend to block their audio push, as
// RetroArch's audio sync does (DCX.pak's old flycast). The push waits until
// the buffer is at most half full: 1 ms steps, at most 100 ms per call so a
// stalled device can't hang the core, and never while fast-forwarding.
// With the core's threaded rendering turned off, it pushes audio from
// retro_run on minarch's main thread instead, so the wait pauses that thread:
// plain audio sync, which paces that mode correctly too.
static int audio_block = -1;
static void waitForAudioRoom(void) {
	if (audio_block < 0)
		audio_block = getenv("NX_AUDIO_BLOCK") && atoi(getenv("NX_AUDIO_BLOCK")) == 1;
	if (!audio_block || fast_forward)
		return;
	for (int i = 0; i < 100 && SND_bufferOccupancy() > 0.5f; i++)
		usleep(1000);
}
size_t audio_sample_batch_callback(const int16_t* data, size_t frames) {
	waitForAudioRoom();
	EmuTime_countAudio(frames);
	if (rewinding && !rewind_ctx.audio)
		return frames;
	// Always report every input frame as consumed, as RetroArch does: the SND_*
	// helpers return how many RESAMPLED frames they queued (0 when dropping),
	// and a core that loops until its batch is consumed (mupen64plus-next:
	// "output_frames -= ret") spins forever on 0 or wraps its size_t counter
	// when the 44.1->48 kHz resample returns more frames than it was given.
	if (!fast_forward || ff_audio) {
		if (use_core_fps || fast_forward)
			SND_batchSamples_fixed_rate((const SND_Frame*)data, frames);
		else
			SND_batchSamples((const SND_Frame*)data, frames);
	}
	return frames;
}

// We need to do this on the audio thread (aka main thread currently)
static bool resetAudio = false;

void Audio_onSinkChanged(int sink_type) {
	(void)sink_type;
	resetAudio = true;
}


// Reset audio on the main (audio) thread after a sink change was flagged.
void Audio_checkAndResetIfNeeded(void) {
	if (resetAudio) {
		resetAudio = false;
		SND_resetAudio(core.sample_rate, core.fps);
		SetVolume(GetVolume());
	}
}

static pthread_t warmup_thread;
static int warmup_started = 0;
static snd_pcm_t* warmup_pcm = NULL;

static void* Audio_warmupThread(void* arg) {
	(void)arg;
	snd_pcm_t* pcm = NULL;
	if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
		return NULL;
	if (snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1, 100000) < 0) {
		snd_pcm_close(pcm);
		return NULL;
	}
	warmup_pcm = pcm;
	return NULL;
}

void Audio_startWarmup(void) {
	// Only the speaker's dmix lets a second open share the device: a USB DAC
	// or Bluetooth sink may be exclusive.
	if (!AudioMgr_publishedSinkIsSpeaker())
		return;
	SND_overrideMute(1); // the codec powers up now: keep the startup pop protection
	warmup_started = pthread_create(&warmup_thread, NULL, Audio_warmupThread, NULL) == 0;
}

void Audio_joinWarmup(void) {
	if (!warmup_started)
		return;
	pthread_join(warmup_thread, NULL);
	warmup_started = 0;
}

void Audio_endWarmup(void) {
	Audio_joinWarmup();
	if (warmup_pcm) {
		snd_pcm_close(warmup_pcm);
		warmup_pcm = NULL;
	}
}
