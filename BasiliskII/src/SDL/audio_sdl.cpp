/*
 *  audio_sdl.cpp - Audio support, SDL implementation
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "user_strings.h"
#include "audio.h"
#include "audio_defs.h"

#include <SDL_mutex.h>
#include <SDL_audio.h>

#define DEBUG 0
#include "debug.h"

#include "app.hpp"

#if defined(BINCUE)
#include "bincue_unix.h"
#endif


// The currently selected audio parameters (indices in audio_sample_rates[] etc. vectors)
static int audio_sample_rate_index = 0;
static int audio_sample_size_index = 0;
static int audio_channel_count_index = 0;

// Global variables
static SDL_sem *audio_irq_done_sem = NULL;			// Signal from interrupt to streaming thread: data block read
static uint8 silence_byte;							// Byte value to use to fill sound buffers with silence

// Prototypes
static void stream_func(void *arg, uint8 *stream, int stream_len);


/*
 *  Initialization
 */

// Set AudioStatus to reflect current audio stream format
static void set_audio_status_format(void)
{
	AudioStatus.sample_rate = audio_sample_rates[audio_sample_rate_index];
	AudioStatus.sample_size = audio_sample_sizes[audio_sample_size_index];
	AudioStatus.channels = audio_channel_counts[audio_channel_count_index];
	AudioStatus.period = 4096 * 60 * CYCLES_PER_60HZ / (AudioStatus.sample_rate >> 16) / AudioStatus.channels;
}

// Init SDL audio system
static bool open_sdl_audio(void)
{
	// SDL supports a variety of twisted little audio formats, all different
	if (audio_sample_sizes.empty()) {
		audio_sample_rates.push_back(11025 << 16);
		audio_sample_rates.push_back(22050 << 16);
		audio_sample_rates.push_back(44100 << 16);
		audio_sample_sizes.push_back(8);
		audio_sample_sizes.push_back(16);
		audio_channel_counts.push_back(1);
		audio_channel_counts.push_back(2);

		// Default to highest supported values
		audio_sample_rate_index = audio_sample_rates.size() - 1;
		audio_sample_size_index = audio_sample_sizes.size() - 1;
		audio_channel_count_index = audio_channel_counts.size() - 1;
	}

	SDL_AudioSpec audio_spec;
	audio_spec.freq = audio_sample_rates[audio_sample_rate_index] >> 16;
	audio_spec.format = (audio_sample_sizes[audio_sample_size_index] == 8) ? AUDIO_U8 : AUDIO_S16MSB;
	audio_spec.channels = audio_channel_counts[audio_channel_count_index];
	audio_spec.samples = 4096;
	audio_spec.callback = stream_func;
	audio_spec.userdata = NULL;

	// Open the audio device, forcing the desired format
	if (SDL_OpenAudio(&audio_spec, NULL) < 0) {
		fprintf(stderr, "WARNING: Cannot open audio: %s\n", SDL_GetError());
		return false;
	}

#if defined(BINCUE)
	OpenAudio_bincue(audio_spec.freq, audio_spec.format, audio_spec.channels,
	audio_spec.silence);
#endif

	char driver_name[32];
	printf("Using SDL/%s audio output\n", SDL_AudioDriverName(driver_name, sizeof(driver_name) - 1));
	silence_byte = audio_spec.silence;
	SDL_PauseAudio(0);

	// Sound buffer size = 4096 frames
	audio_frames_per_block = audio_spec.samples;
	return true;
}

static bool open_audio(void)
{
	// Try to open SDL audio
	if (!open_sdl_audio()) {
		WarningAlert(GetString(STR_NO_AUDIO_WARN));
		return false;
	}

	// Device opened, set AudioStatus
	set_audio_status_format();

	// Everything went fine
	audio_open = true;
	return true;
}

void AudioInit(void)
{
	// Init audio status and feature flags
	AudioStatus.sample_rate = 44100 << 16;
	AudioStatus.sample_size = 16;
	AudioStatus.channels = 2;
	AudioStatus.mixer = 0;
	AudioStatus.num_sources = 0;
	audio_component_flags = cmpWantsRegisterMessage | kStereoOut | k16BitOut;

	// Sound disabled in prefs? Then do nothing
	if (PrefsFindBool("nosound"))
		return;

	// Init semaphore
	audio_irq_done_sem = SDL_CreateSemaphore(0);

	// Open and initialize audio device
	open_audio();
}


/*
 *  Deinitialization
 */

static void close_audio(void)
{
	// Close audio device
	SDL_CloseAudio();
	audio_open = false;
}

void AudioExit(void)
{
	// Close audio device
	close_audio();

	// Delete semaphore
	if (audio_irq_done_sem)
		SDL_DestroySemaphore(audio_irq_done_sem);
}


/*
 *  First source added, start audio stream
 */

void audio_enter_stream()
{
}


/*
 *  Last source removed, stop audio stream
 */

void audio_exit_stream()
{
}


/*
 *  Streaming function
 */

static void stream_func(void *arg, uint8 *stream, int stream_len)
{
	if (AudioStatus.num_sources) {
		//SDL_SemWait(audio_irq_done_sem);
		size_t bytes_copied = the_app->copy_audio_out(stream, stream_len);
		D(bug("copied %zu bytes out\n", bytes_copied));
		if (bytes_copied != stream_len) {
			memset((uint8 *)stream + bytes_copied, silence_byte, stream_len - bytes_copied);
		}
	} else {
		// Audio not active, play silence
		memset(stream, silence_byte, stream_len);
	}
#if defined(BINCUE)
	MixAudio_bincue(stream, stream_len);
#endif
}


/*
 *  MacOS audio interrupt, read next data block
 */

void AudioInterrupt(void)
{
	bool got_audio = false;
	// Get data from apple mixer
	if (AudioStatus.mixer) {
		M68kRegisters r;
		r.a[0] = audio_data + adatStreamInfo;
		r.a[1] = AudioStatus.mixer;
		Execute68k(audio_data + adatGetSourceData, &r);

		// Get size of audio data
		uint32 apple_stream_info = ReadMacInt32(audio_data + adatStreamInfo);
		if (apple_stream_info) {
			int work_size = ReadMacInt32(apple_stream_info + scd_sampleCount) * (AudioStatus.sample_size >> 3) * AudioStatus.channels;
			uint8 *buffer = Mac2HostAddr(ReadMacInt32(apple_stream_info + scd_buffer));
			size_t bytes_copied = the_app->copy_audio_in(buffer, work_size);
			the_app->record_audio(buffer);
			D(bug("copied %zu bytes in\n", bytes_copied));
			got_audio = true;
			//SDL_SemPost(audio_irq_done_sem);
		}
	} else
		WriteMacInt32(audio_data + adatStreamInfo, 0);

	if (!got_audio) {
		the_app->record_audio();
	}
}


/*
 *  Set sampling parameters
 *  "index" is an index into the audio_sample_rates[] etc. vectors
 *  It is guaranteed that AudioStatus.num_sources == 0
 */

bool audio_set_sample_rate(int index)
{
	close_audio();
	audio_sample_rate_index = index;
	return open_audio();
}

bool audio_set_sample_size(int index)
{
	close_audio();
	audio_sample_size_index = index;
	return open_audio();
}

bool audio_set_channels(int index)
{
	close_audio();
	audio_channel_count_index = index;
	return open_audio();
}


/*
 *  Get/set volume controls (volume values received/returned have the left channel
 *  volume in the upper 16 bits and the right channel volume in the lower 16 bits;
 *  both volumes are 8.8 fixed point values with 0x0100 meaning "maximum volume"))
 */

bool audio_get_main_mute(void)
{
	return false;
}

uint32 audio_get_main_volume(void)
{
	return 0x01000100;
}

bool audio_get_speaker_mute(void)
{
	return false;
}

uint32 audio_get_speaker_volume(void)
{
	return 0x01000100;
}

void audio_set_main_mute(bool mute)
{
}

void audio_set_main_volume(uint32 vol)
{
}

void audio_set_speaker_mute(bool mute)
{
}

void audio_set_speaker_volume(uint32 vol)
{
}
