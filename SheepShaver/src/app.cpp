#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include <strings.h>
#include "app.hpp"

#define DEBUG 1
#include "debug.h"


sheepshaver_state::sheepshaver_state()
{
	video_buffer = NULL;
	video_buffer_size = 0;
	initialize_tvect();
	time_state.microseconds = 0;
	time_state.base_time = TimeToMacTime(time(NULL));
	record_recording = play_recording = NULL;
	pthread_barrier_init(&tick_barrier, NULL, 2);
	tick_stepping = false;
}

void sheepshaver_state::initialize_tvect(void)
{
	bzero(&macos_tvect, sizeof macos_tvect);
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

void sheepshaver_state::save_state(int slot)
{
	save_slot = slot;
	save_op = OP_SAVE_STATE;
	ppc_cpu->spcflags().set(SPCFLAG_HANDLE_SAVESTATE);
}

void sheepshaver_state::load_state(int slot)
{
	save_slot = slot;
	save_op = OP_LOAD_STATE;
	ppc_cpu->spcflags().set(SPCFLAG_HANDLE_SAVESTATE);
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
		write_exactly(&macos_tvect, fd, sizeof macos_tvect);
		write_exactly(&time_state, fd, sizeof time_state);
		write_exactly(&video_buffer_size, fd, sizeof video_buffer_size);
		if (video_buffer_size) {
			write_exactly(video_buffer, fd, video_buffer_size);
		}
		write_exactly(ppc_cpu->regs_ptr(), fd, sizeof(powerpc_registers));
		write_exactly(&video_state, fd, sizeof video_state);
		save_descs(fd);
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
		read_exactly(&macos_tvect, fd, sizeof macos_tvect);
		read_exactly(&time_state, fd, sizeof time_state);
		read_exactly(&video_buffer_size, fd, sizeof video_buffer_size);
		if (video_buffer_size) {
			if (video_buffer) {
				free(video_buffer);
			}
			video_buffer = (uint8 *)malloc(video_buffer_size);
			read_exactly(video_buffer, fd, video_buffer_size);
		}
		read_exactly(ppc_cpu->regs_ptr(), fd, sizeof(powerpc_registers));
		read_exactly(&video_state, fd, sizeof video_state);
		clear_descs();
		//load_descs(fd);
		ppc_cpu->invalidate_cache();
		reopen_video();
		video_set_palette();
	}
	close(fd);
}


void sheepshaver_state::start_recording(void)
{
	if (record_recording) return;
	D(bug("started recording\n"));
	record_recording = new recording_t(&time_state);
}

void sheepshaver_state::load_recording(char *filename)
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
	}
}
