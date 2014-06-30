#ifndef RECORDING_HPP
#define RECORDING_HPP

#include "timer.h"

#define RECORDING_BLOCK_FRAMES 8192


enum recording_op_t {
	OP_NO_OP = 0,
	OP_KEY_DOWN,
	OP_KEY_UP
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
	recording_frame_block_t *next;

	recording_frame_block_t();
	void dump(void);
};

class time_state_t;

class recording_t
{
public:
	recording_header_t header;
	recording_frame_block_t *current_block;
	recording_frame_block_t *first_block;
	uint8 current_frame;
	uint8 countdown;
	bool done;

	recording_t(time_state_t *);
	recording_t(char *);
	~recording_t();
	void record(recording_op_t op, uint64 microseconds, uint64 arg);
	void dump(void);
	void save(void);
	void play_through(uint64 end);

private:
	recording_frame_block_t *_blocks;
};

#endif
