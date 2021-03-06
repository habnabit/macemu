#ifndef APP_HPP
#define APP_HPP

#include <pthread.h>
#include "sysdeps.h"
#include "main.h"
#include "cpu_emulation.h"
#include "sigsegv.h"
#include "timer.h"
#include "video.h"
#include "macos_util.h"
#include "recording.hpp"
#include "video_recording.hpp"
#include "cpu/ppc/ppc-cpu.hpp"

#define CYCLES_PER_60HZ 5000
#define MAX_KEYSYM 256


class sheepshaver_state;

class sheepshaver_cpu
	: public powerpc_cpu
{
	void init_decoder();
	void execute_sheep(uint32 opcode);

public:

	// Constructor
	sheepshaver_cpu();

	// CR & XER accessors
	uint32 get_cr() const		{ return cr().get(); }
	void set_cr(uint32 v)		{ cr().set(v); }
	uint32 get_xer() const		{ return xer().get(); }
	void set_xer(uint32 v)		{ xer().set(v); }

	// Execute NATIVE_OP routine
	void execute_native_op(uint32 native_op);

	// Execute EMUL_OP routine
	void execute_emul_op(uint32 emul_op);

	// Execute 68k routine
	void execute_68k(uint32 entry, M68kRegisters *r);

	// Execute ppc routine
	void execute_ppc(uint32 entry);

	// Execute MacOS/PPC code
	uint32 execute_macos_code(uint32 tvect, int nargs, uint32 const *args);

#if PPC_ENABLE_JIT
	// Compile one instruction
	virtual int compile1(codegen_context_t & cg_context);
#endif
	// Resource manager thunk
	void get_resource(uint32 old_get_resource);

	// Handle MacOS interrupt
	void interrupt(uint32 entry);

	// Make sure the SIGSEGV handler can access CPU registers
	friend sigsegv_return_t sigsegv_handler(sigsegv_info_t *sip);
	friend class sheepshaver_state;
};


class macos_tvect_t
{
public:
	uint32 d_tvect;
	uint32 nps_tvect;
	uint32 cc_tvect;
	uint32 fs_tvect;
	uint32 gsl_tvect;
	uint32 cu_tvect;
};


enum save_op_t {
	OP_NO_STATE,
	OP_SAVE_STATE,
	OP_LOAD_STATE
};

enum recording_types_t {
	HAS_RECORD_RECORDING = 1 << 0,
};

class sheepshaver_state
{
public:
	sheepshaver_state();

	sheepshaver_cpu *ppc_cpu;

	void init_emul_ppc(void);
	void exit_emul_ppc(void);
	void emul_ppc(uint32 entry);

	int save_slot;
	save_op_t save_op;
	void save_state(void);
	void load_state(int);
	void do_save_load(void);

	TMDesc *tmDescList;
	void free_desc(TMDesc *);
	TMDesc *find_desc(uint32);
	void add_desc(uint32);
	void clear_descs(void);
	void dump_descs(void);
	void save_descs(int);
	void load_descs(int);

	video_state_t video_state;
	uint8 *video_buffer;
	uint32 video_buffer_size;
	void reopen_video(void);

	macos_tvect_t macos_tvect;
	void initialize_tvect(void);

	time_state_t time_state;

	recording_t *record_recording;
	recording_t *play_recording;
	bool pause_after_playback;
	bool fast_playback;
	void start_recording(void);
	void load_recording(const char *);
	void advance_microseconds(uint64);
	void kill_playback_recording(void);

	inline void record(recording_op_t op, uint64 arg)
	{
		if (record_recording) record_recording->record(op, time_state.microseconds, arg);
	}

	bool tick_stepping;
	uint8 tick_step;

	volatile uint32 interrupt_flags;

	bool keys_down[MAX_KEYSYM];
	bool keys_actually_down[MAX_KEYSYM];
	void calculate_key_differences(void);

	inline void key_state_changed(int code, bool is_down)
	{
		if (code < 0 || code >= MAX_KEYSYM) return;
		keys_down[code] = is_down;
	}

	inline void key_down(int code)
	{
		key_state_changed(code, true);
	}

	inline void key_up(int code)
	{
		key_state_changed(code, false);
	}

	uint8 *audio_buffer;
	uint8 *audio_buffer_end;
	uint8 *audio_read_ptr;
	uint8 *audio_write_ptr;
	size_t copy_audio_in(uint8 *, size_t);
	size_t copy_audio_out(uint8 *, size_t);

	bool do_record_video;
	uint16 video_nr;
	video_recording_state_t *video_recording_state;
	void init_video_recording(void);
	void start_video_recording(int, int, int);
	void finalize_video_recording(void);
	void record_video(void);
	void record_audio(uint8 *);
	void record_audio(void);
};

extern sheepshaver_state *the_app;
void read_exactly(void *, int, size_t);
void write_exactly(void *, int, size_t);

#endif
