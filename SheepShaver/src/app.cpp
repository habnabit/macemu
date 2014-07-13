#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <strings.h>
#include <dirent.h>
#include "sysdeps.h"
#include "adb.h"
#include "app.hpp"

#define DEBUG 1
#include "debug.h"


#define AUDIO_BUFFER_SIZE 1048577


sheepshaver_state::sheepshaver_state()
{
	memset(this, 0, sizeof *this);
	time_state.base_time = 3487370000;
	audio_buffer = (uint8 *)malloc(AUDIO_BUFFER_SIZE);
	audio_buffer_end = audio_buffer + AUDIO_BUFFER_SIZE;
	audio_read_ptr = audio_write_ptr = audio_buffer;
}

void sheepshaver_state::initialize_tvect(void)
{
	memset(&macos_tvect, 0, sizeof macos_tvect);
}

void read_exactly(void *dest, int fd, size_t length)
{
	ssize_t just_read;
	char *dest_c = (char *)dest;
	while (length) {
		if ((just_read = read(fd, dest_c, length)) == -1) {
			if (errno == EINTR) continue;
			perror("read_exactly");
			return;
		}
		dest_c += just_read;
		length -= just_read;
	}
}

void write_exactly(void *src, int fd, size_t length)
{
	ssize_t just_wrote;
	char *src_c = (char *)src;
	while (length) {
		if ((just_wrote = write(fd, src_c, length)) == -1) {
			if (errno == EINTR) continue;
			perror("write_exactly");
			return;
		}
		src_c += just_wrote;
		length -= just_wrote;
	}
}

static int determine_highest_savestate(void)
{
	int highest = 0;
	DIR *dir;
	struct dirent *dp;

	if (!(dir = opendir("."))) {
		perror("determine_highest_savestate: opendir");
		return -1;
	}
	while ((dp = readdir(dir))) {
		int cur;
		if (sscanf(dp->d_name, "%d.save", &cur) != 1) continue;
		if (cur > highest) highest = cur;
	}
	closedir(dir);
	return highest;
}

void sheepshaver_state::save_state(void)
{
	if ((save_slot = determine_highest_savestate()) < 0) return;
	++save_slot;
	D(bug("saving %d\n", save_slot));
	save_op = OP_SAVE_STATE;
	do_save_load();
}

void sheepshaver_state::load_state(int slot)
{
	if ((save_slot = determine_highest_savestate()) < 0) return;
	save_slot -= slot;
	D(bug("loading %d\n", save_slot));
	save_op = OP_LOAD_STATE;
	do_save_load();
}

void sheepshaver_state::do_save_load(void)
{
	char filename[32];
	int fd;
	snprintf(filename, sizeof filename, "%d.save", save_slot);
	if (save_op == OP_SAVE_STATE) {
		if ((fd = open(filename, O_WRONLY | O_CREAT, 0666)) < 0) {
			perror("do_save_load: open");
			return;
		}
		VideoSaveBuffer();
		if (RAMBase) {
			write_exactly(Mac2HostAddr(0), fd, 0x3000);
		}
		write_exactly(RAMBaseHost, fd, RAMSize);
		write_exactly(Mac2HostAddr(KERNEL_DATA_BASE), fd, KERNEL_AREA_SIZE);
		write_exactly(Mac2HostAddr(KERNEL_DATA2_BASE), fd, KERNEL_AREA_SIZE);
		write_exactly(Mac2HostAddr(DR_EMULATOR_BASE), fd, DR_EMULATOR_SIZE);
		write_exactly(Mac2HostAddr(DR_CACHE_BASE), fd, DR_CACHE_SIZE);
		write_exactly((void *)&interrupt_flags, fd, sizeof interrupt_flags);
		write_exactly(&macos_tvect, fd, sizeof macos_tvect);
		write_exactly(&time_state, fd, sizeof time_state);
		write_exactly(keys_actually_down, fd, sizeof keys_actually_down);
		write_exactly(&video_buffer_size, fd, sizeof video_buffer_size);
		if (video_buffer_size) {
			write_exactly(video_buffer, fd, video_buffer_size);
		}
		write_exactly(&video_state, fd, sizeof video_state);
		save_descs(fd);
		ppc_cpu->save_to(fd);
		uint8 recording_types = 0;
		if (record_recording) recording_types |= HAS_RECORD_RECORDING;
		write_exactly(&recording_types, fd, sizeof recording_types);
		if (record_recording) {
			record(OP_INVALIDATE_CACHE, 0);
			record_recording->save_to(fd);
		}
	} else if (save_op == OP_LOAD_STATE) {
		if ((fd = open(filename, O_RDONLY)) < 0) {
			perror("do_save_load: open");
			return;
		}
		if (RAMBase) {
			read_exactly(Mac2HostAddr(0), fd, 0x3000);
		}
		read_exactly(RAMBaseHost, fd, RAMSize);
		read_exactly(Mac2HostAddr(KERNEL_DATA_BASE), fd, KERNEL_AREA_SIZE);
		read_exactly(Mac2HostAddr(KERNEL_DATA2_BASE), fd, KERNEL_AREA_SIZE);
		read_exactly(Mac2HostAddr(DR_EMULATOR_BASE), fd, DR_EMULATOR_SIZE);
		read_exactly(Mac2HostAddr(DR_CACHE_BASE), fd, DR_CACHE_SIZE);
		read_exactly((void *)&interrupt_flags, fd, sizeof interrupt_flags);
		read_exactly(&macos_tvect, fd, sizeof macos_tvect);
		read_exactly(&time_state, fd, sizeof time_state);
		read_exactly(keys_actually_down, fd, sizeof keys_actually_down);
		read_exactly(&video_buffer_size, fd, sizeof video_buffer_size);
		if (video_buffer_size) {
			if (video_buffer) {
				free(video_buffer);
			}
			video_buffer = (uint8 *)malloc(video_buffer_size);
			read_exactly(video_buffer, fd, video_buffer_size);
		}
		read_exactly(&video_state, fd, sizeof video_state);
		load_descs(fd);
		ppc_cpu->load_from(fd);
		uint8 recording_types = 0;
		read_exactly(&recording_types, fd, sizeof recording_types);
		if (recording_types & HAS_RECORD_RECORDING) {
			if (record_recording) delete record_recording;
			record_recording = new recording_t(fd);
			record_recording->advance_to_end();
		}
		reopen_video();
		video_set_palette();
		memset(keys_down, 0, sizeof keys_down);
		tick_stepping = true;
		tick_step = 0;
	}
	ppc_cpu->invalidate_cache();
	close(fd);
}


void sheepshaver_state::start_recording(void)
{
	if (record_recording) return;
	D(bug("started recording\n"));
	record_recording = new recording_t(&time_state);
}

void sheepshaver_state::load_recording(const char *filename)
{
	if (play_recording) {
		if (!play_recording->done) return;
		D(bug("deleting old recording\n"));
		delete play_recording;
	}
	D(bug("playing recording: %s\n", filename));
	play_recording = new recording_t(filename);
	D(bug("time_state before %lu %u\n", time_state.microseconds, time_state.base_time));
	memcpy(&time_state, &play_recording->header.time_state, sizeof time_state);
	D(bug("time_state after  %lu %u\n", time_state.microseconds, time_state.base_time));
}

void sheepshaver_state::advance_microseconds(uint64 delta)
{
	time_state.microseconds += delta;
	if (play_recording) {
		play_recording->play_through(time_state.microseconds);
		if (play_recording->done) {
			delete play_recording;
			play_recording = NULL;
			if (pause_after_playback) {
				tick_stepping = true;
				tick_step = 0;
				memset(keys_down, 0, sizeof keys_down);
			}
		}
	}
}

void sheepshaver_state::calculate_key_differences(void)
{
	for (int i = 0; i < MAX_KEYSYM; ++i) {
		if (keys_down[i] != keys_actually_down[i]) {
			if (keys_down[i]) {
				ADBKeyDown(i);
			} else {
				ADBKeyUp(i);
			}
			keys_actually_down[i] = keys_down[i];
		}
	}
}


size_t sheepshaver_state::copy_audio_in(uint8 *buf, size_t size)
{
	if (size == 0) return 0;
	if (size >= AUDIO_BUFFER_SIZE) size = AUDIO_BUFFER_SIZE - 1;
	if (audio_write_ptr < audio_read_ptr) {
		if (audio_write_ptr + size >= audio_read_ptr) {
			size = audio_read_ptr - audio_write_ptr - 1;
		}
		memcpy(audio_write_ptr, buf, size);
		audio_write_ptr += size;
		return size;
	}
	size_t split_rhs = audio_buffer_end - audio_write_ptr;
	if (size <= split_rhs) {
		memcpy(audio_write_ptr, buf, size);
		audio_write_ptr += size;
		if (audio_write_ptr == audio_buffer_end) audio_write_ptr = audio_buffer;
		return size;
	}
	size_t split_lhs = size - split_rhs;
	if (split_lhs + audio_buffer >= audio_read_ptr) {
		size = audio_read_ptr - audio_buffer - 1 + split_rhs;
		if (size == 0) return 0;
		split_lhs = size - split_rhs;
	}
	memcpy(audio_write_ptr, buf, split_rhs);
	memcpy(audio_buffer, buf + split_rhs, split_lhs);
	audio_write_ptr = audio_buffer + split_lhs;
	return size;
}

size_t sheepshaver_state::copy_audio_out(uint8 *buf, size_t size)
{
	if (audio_write_ptr == audio_read_ptr || size == 0) return 0;
	if (size >= AUDIO_BUFFER_SIZE) size = AUDIO_BUFFER_SIZE - 1;
	if (audio_read_ptr < audio_write_ptr) {
		if (audio_read_ptr + size > audio_write_ptr) {
			size = audio_write_ptr - audio_read_ptr;
		}
		memcpy(buf, audio_read_ptr, size);
		audio_read_ptr += size;
		return size;
	}
	size_t split_rhs = audio_buffer_end - audio_read_ptr;
	if (size <= split_rhs) {
		memcpy(buf, audio_read_ptr, size);
		audio_read_ptr += size;
		if (audio_read_ptr == audio_buffer_end) audio_read_ptr = audio_buffer;
		return size;
	}
	size_t split_lhs = size - split_rhs;
	if (split_lhs + audio_buffer > audio_write_ptr) {
		size = audio_write_ptr - audio_buffer + split_rhs;
		if (size == 0) return 0;
		split_lhs = size - split_rhs;
	}
	memcpy(buf, audio_read_ptr, split_rhs);
	memcpy(buf + split_rhs, audio_buffer, split_lhs);
	audio_read_ptr = audio_buffer + split_lhs;
	return size;
}
