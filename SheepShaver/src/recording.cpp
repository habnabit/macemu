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

recording_frame_block_t::~recording_frame_block_t()
{
	if (next) delete next;
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

void recording_frame_block_t::save_to(int fd)
{
	for (int i = 0; i < RECORDING_BLOCK_FRAMES; ++i) {
		recording_frame_t *f = &frames[i];
		unsigned char op = f->op;
		if (op == OP_NO_OP) break;
		write_exactly(&op, fd, sizeof op);
		write_exactly(&f->microseconds, fd, sizeof f->microseconds);
		write_exactly(&f->arg, fd, sizeof f->arg);
	}
}

void recording_frame_block_t::load_from(int fd, uint32 *n_frames)
{
	for (int i = 0; i < RECORDING_BLOCK_FRAMES; ++i) {
		if (!*n_frames) return;
		*n_frames -= 1;
		recording_frame_t *f = &frames[i];
		unsigned char op;
		read_exactly(&op, fd, sizeof op);
		f->op = (recording_op_t)op;
		read_exactly(&f->microseconds, fd, sizeof f->microseconds);
		read_exactly(&f->arg, fd, sizeof f->arg);
	}
}

recording_t::recording_t(time_state_t *time_state)
{
	first_block = current_block = new recording_frame_block_t;
	current_frame = 0;
	memcpy(&header.time_state, time_state, sizeof *time_state);
	header.frame_blocks = 1;
	countdown = 0;
	done = false;
}

recording_t::~recording_t()
{
	delete first_block;
}

recording_t::recording_t(const char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("recording_t: open:");
		return;
	}
	load_from(fd);
	close(fd);
}

recording_t::recording_t(int fd)
{
	load_from(fd);
}

void recording_t::load_from(int fd)
{
	recording_frame_block_t *prev = NULL, *fb = NULL;
	time_state_t *t = &header.time_state;
	uint32 frames;
	read_exactly(&t->microseconds, fd, sizeof t->microseconds);
	read_exactly(&t->base_time, fd, sizeof t->base_time);
	read_exactly(&frames, fd, sizeof frames);
	while (frames) {
		fb = new recording_frame_block_t;
		fb->load_from(fd, &frames);
		if (prev) {
			prev->next = fb;
			fb->prev = prev;
		} else {
			first_block = current_block = fb;
		}
		prev = fb;
	}
	current_frame = 0;
	countdown = 0;
	done = false;
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
	int fd = open("recording", O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		perror("save: open:");
		return;
	}
	save_to(fd);
	close(fd);
}

void recording_t::save_to(int fd)
{
	D(bug("writing recording\n"));
	time_state_t *t = &header.time_state;
	uint32 frames = (header.frame_blocks - 1) * RECORDING_BLOCK_FRAMES + current_frame;
	write_exactly(&t->microseconds, fd, sizeof t->microseconds);
	write_exactly(&t->base_time, fd, sizeof t->base_time);
	write_exactly(&frames, fd, sizeof frames);
	for (recording_frame_block_t *b = first_block; b; b = b->next) {
		b->save_to(fd);
	}
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
			the_app->key_down(f->arg);
			break;
		case OP_KEY_UP:
			the_app->key_up(f->arg);
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
		case OP_INVALIDATE_CACHE:
			the_app->ppc_cpu->invalidate_cache();
			the_app->record(OP_INVALIDATE_CACHE, f->arg);
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

void recording_t::advance_to_end()
{
	recording_frame_block_t *fb = first_block;
	while (fb->next) fb = fb->next;
	current_block = fb;
	current_frame = 0;
	while (!advance_frame());
	D(bug("advanced to %hu\n", current_frame));
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
