#include <errno.h>
#include <stdio.h>
#include <strings.h>
#include "sysdeps.h"
#include "adb.h"
#include "app.hpp"
#include "recording.hpp"

#define DEBUG 1
#include "debug.h"


recording_frame_block_t::recording_frame_block_t()
{
	bzero(this, sizeof *this);
}

void recording_frame_block_t::dump(void)
{
	for (int i = 0; i < RECORDING_BLOCK_FRAMES; ++i) {
		recording_frame_t *f = &frames[i];
		if (f->op == OP_NO_OP) break;
		printf("op %d microseconds %-10lu arg %-10lu\n", f->op, f->microseconds, f->arg);
	}
}

recording_t::recording_t(time_state_t *time_state)
{
	first_block = current_block = new recording_frame_block_t;
	current_frame = 0;
	memcpy(&header.time_state, time_state, sizeof *time_state);
	header.frame_blocks = 1;
	_blocks = NULL;
	countdown = 0;
	done = false;
}

recording_t::recording_t(char *filename)
{
	int fd = open(filename, O_RDONLY);
	recording_frame_block_t *prev = NULL;
	if (fd < 0) {
		perror("recording_t: open:");
		return;
	}
	read_exactly(&header, fd, sizeof header);
	_blocks = new recording_frame_block_t[header.frame_blocks];
	for (uint32 i = 0; i < header.frame_blocks; ++i) {
		recording_frame_block_t *fb = &_blocks[i];
		read_exactly(fb, fd, sizeof *fb);
		if (prev) {
			prev->next = fb;
		}
		prev = fb;
	}
	if (prev) {
		prev->next = NULL;
	}
	close(fd);
	first_block = current_block = &_blocks[0];
	current_frame = 0;
	countdown = 0;
	done = false;
}

recording_t::~recording_t()
{
	if (_blocks) delete[] _blocks;
}

void recording_t::record(recording_op_t op, uint64 microseconds, uint64 arg)
{
	recording_frame_t *frame = &current_block->frames[current_frame++];
	D(bug("recording %d %lu\n", op, arg));
	frame->op = op;
	frame->microseconds = microseconds;
	frame->arg = arg;
	if (current_frame >= RECORDING_BLOCK_FRAMES) {
		recording_frame_block_t *next = new recording_frame_block_t;
		D(bug("allocating new block\n"));
		current_block->next = next;
		current_block = next;
		current_frame = 0;
		++header.frame_blocks;
	}
}

void recording_t::dump(void)
{
	printf("recording base_time %u %lu blocks %u\n",
		   header.time_state.base_time, header.time_state.microseconds, header.frame_blocks);
	for (recording_frame_block_t *b = first_block; b; b = b->next) {
		printf("frame_block %p\n", b);
		b->dump();
	}
}

void recording_t::save(void)
{
	int fd = open("recording", O_WRONLY | O_CREAT, 0666);
	D(bug("writing recording\n"));
	if (fd < 0) {
		perror("save: open:");
		return;
	}
	write_exactly(&header, fd, sizeof header);
	for (recording_frame_block_t *b = first_block; b; b = b->next) {
		write_exactly(b, fd, sizeof *b);
	}
	close(fd);
}

void recording_t::play_through(uint64 end)
{
	if (done) return;
	do {
		recording_frame_t *f = &current_block->frames[current_frame];
		if (f->microseconds > end) {
			if (!countdown) {
				D(bug("%d us until next\n", f->microseconds - end));
				countdown = 60;
			} else --countdown;
			break;
		}
		if (f->op == OP_NO_OP) {
			D(bug("finished playback\n"));
			done = true;
			break;
		}
		if (++current_frame >= RECORDING_BLOCK_FRAMES) {
			if (!current_block->next) {
				D(bug("finished playback\n"));
				done = true;
				break;
			}
			current_block = current_block->next;
		}
		D(bug("playback: %d %lu %lu\n", f->op, f->arg, f->microseconds));
		switch (f->op) {
		case OP_KEY_DOWN:
			ADBKeyDown(f->arg);
			break;
		case OP_KEY_UP:
			ADBKeyUp(f->arg);
			break;
		}
	} while (1);
}
