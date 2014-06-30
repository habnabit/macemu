#include <errno.h>
#include <stdio.h>
#include <strings.h>
#include "app.hpp"
#include "recording.hpp"


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

recording_t::recording_t(uint32 base_time)
{
	first_block = current_block = new recording_frame_block_t;
	current_frame = 0;
	header.base_time = base_time;
	header.frame_blocks = 1;
	_blocks = NULL;
}

recording_t::recording_t(char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("recording_t: open:");
		return;
	}
	read_exactly(&header, fd, sizeof header);
	_blocks = new recording_frame_block_t[header.frame_blocks];
	for (uint32 i = 0; i < header.frame_blocks; ++i) {
		read_exactly(&_blocks[i], fd, sizeof *_blocks);
	}
	close(fd);
	first_block = current_block = &_blocks[0];
	current_frame = 0;
}

void recording_t::record(recording_op_t op, uint64 microseconds, uint64 arg)
{
	recording_frame_t *frame = &current_block->frames[current_frame++];
	frame->op = op;
	frame->microseconds = microseconds;
	frame->arg = arg;
	if (current_frame == RECORDING_BLOCK_FRAMES) {
		recording_frame_block_t *next = new recording_frame_block_t;
		current_block->next = next;
		current_block = next;
		current_frame = 0;
		++header.frame_blocks;
	}
}

void recording_t::dump(void)
{
	printf("recording base_time %u blocks %u\n", header.base_time, header.frame_blocks);
	for (recording_frame_block_t *b = first_block; b; b = b->next) {
		printf("frame_block %p\n", b);
		b->dump();
	}
}

void recording_t::save(void)
{
	int fd = open("recording", O_WRONLY | O_CREAT, 0666);
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
