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
	memset(this, 0, sizeof *this);
}

void recording_frame_block_t::dump(void)
{
	for (int i = 0; i < RECORDING_BLOCK_FRAMES; ++i) {
		recording_frame_t *f = &frames[i];
		if (f->op == OP_NO_OP) break;
		printf("op %d microseconds %-10lu arg %-10lu\n", f->op, f->microseconds, f->arg);
	}
}

void recording_frame_block_t::clear(void)
{
	memset(frames, 0, sizeof frames);
}

void recording_frame_block_t::clear_to_end(uint16 frame)
{
	if (frame >= RECORDING_BLOCK_FRAMES) return;
	memset(&frames[frame], 0, (sizeof *frames) * (RECORDING_BLOCK_FRAMES - frame));
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

recording_t::recording_t(const char *filename)
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
			fb->prev = prev;
		}
		prev = fb;
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
	D(bug("recording: %04x %d %lx %lu\n", current_frame, op, arg, microseconds));
	frame->op = op;
	frame->microseconds = microseconds;
	frame->arg = arg;
	if (current_frame >= RECORDING_BLOCK_FRAMES) {
		recording_frame_block_t *next;
		if (!current_block->next) {
			next = new recording_frame_block_t;
			D(bug("allocating new block\n"));
			current_block->next = next;
			next->prev = current_block;
			++header.frame_blocks;
		} else {
			next = current_block->next;
			next->clear();
		}
		current_block = next;
		current_frame = 0;
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
		recording_frame_t *f = current_frame_ptr();
		if (f->microseconds > end) {
			if (!countdown) {
				D(bug("%lu us until next\n", f->microseconds - end));
				countdown = 60;
			} else --countdown;
			break;
		}
		D(bug("playback: %04x %d %lx %lu\n", current_frame, f->op, f->arg, f->microseconds));
		switch (f->op) {
		case OP_NO_OP:
			break;
		case OP_KEY_DOWN:
			ADBKeyDown(f->arg);
			break;
		case OP_KEY_UP:
			ADBKeyUp(f->arg);
			break;
		case OP_MOUSE_DOWN:
			ADBMouseDown(f->arg);
			break;
		case OP_MOUSE_UP:
			ADBMouseUp(f->arg);
			break;
		case OP_MOUSE_XY:
			ADBMouseMoved(f->arg & 0xffffffff, f->arg >> 32);
			break;
		default:
			D(bug("invalid op: %d", f->op));
		}
		if (advance_frame()) {
			D(bug("finished playback\n"));
			done = true;
			break;
		}
	} while (1);
}

void recording_t::rewind_clearing(uint64 end)
{
	D(bug("rewinding to %lu\n", end));
	do {
		if (retreat_frame_clearing()) return;
		recording_frame_t *f = current_frame_ptr();
		if (f->microseconds <= end) {
			D(bug("done rewinding\n"));
			advance_frame();
			break;
		}
		D(bug("rewound past %lu\n", f->microseconds));
	} while (1);
	D(bug("clearing through %hu\n", current_frame));
	current_block->clear_to_end(current_frame);
}
