#ifndef RECORDING_HPP
#define RECORDING_HPP

#include "timer.h"

#define RECORDING_BLOCK_FRAMES 8192


enum recording_op_t {
	OP_NO_OP = 0,
	OP_KEY_DOWN,
	OP_KEY_UP,
	OP_MOUSE_DOWN,
	OP_MOUSE_UP,
	OP_MOUSE_XY
};

struct recording_header_t
{
	time_state_t time_state;
	uint32 frame_blocks;
};

struct recording_frame_t
{
	recording_op_t op;
	uint64 microseconds;
	uint64 arg;
};

class recording_frame_block_t
{
public:
	recording_frame_t frames[RECORDING_BLOCK_FRAMES];
	recording_frame_block_t *prev;
	recording_frame_block_t *next;

	recording_frame_block_t();
	~recording_frame_block_t();
	void dump(void);
	void clear(void);
	void clear_to_end(uint16);
};

class time_state_t;

class recording_t
{
public:
	recording_header_t header;
	recording_frame_block_t *current_block;
	recording_frame_block_t *first_block;
	uint16 current_frame;
	uint8 countdown;
	bool done;

	recording_t(time_state_t *);
	recording_t(const char *);
	recording_t(int);
	~recording_t();
	void load_from(int);
	void record(recording_op_t op, uint64 microseconds, uint64 arg);
	void dump(void);
	void save(void);
	void save_to(int);

	inline recording_frame_t *current_frame_ptr(void)
	{
		return &current_block->frames[current_frame];
	}

	inline bool advance_frame(void)
	{
		if (++current_frame >= RECORDING_BLOCK_FRAMES) {
			if (!current_block->next) {
				current_frame = RECORDING_BLOCK_FRAMES - 1;
				return true;
			}
			current_block = current_block->next;
			current_frame = 0;
		}
		return current_frame_ptr()->op == OP_NO_OP;
	}

	inline bool retreat_frame_clearing(void)
	{
		if (--current_frame == (uint16)-1) {
			current_block->clear();
			if (!current_block->prev) {
				current_frame = 0;
				return true;
			}
			current_block = current_block->prev;
			current_frame = RECORDING_BLOCK_FRAMES - 1;
		}
		return false;
	}

	void play_through(uint64 end);
	void advance_to_end(void);
	void rewind_clearing(uint64);
};

#endif
