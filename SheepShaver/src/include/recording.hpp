#ifndef RECORDING_HPP
#define RECORDING_HPP

#define RECORDING_BLOCK_FRAMES 8192


enum recording_op_t {
	OP_NO_OP = 0,
	OP_KEY_DOWN,
	OP_KEY_UP
};

struct recording_header_t
{
	uint32 base_time;
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

class recording_t
{
public:
	recording_header_t header;
	recording_frame_block_t *current_block;
	recording_frame_block_t *first_block;
	uint8 current_frame;

	recording_t(uint32);
	recording_t(char *);
	void record(recording_op_t op, uint64 microseconds, uint64 arg);
	void dump(void);
	void save(void);

private:
	recording_frame_block_t *_blocks;
};

#endif
