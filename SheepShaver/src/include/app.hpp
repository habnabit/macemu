#include "sysdeps.h"
#include "cpu_emulation.h"
#include "sigsegv.h"
#include "cpu/ppc/ppc-cpu.hpp"


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

enum save_op_t {
	OP_NO_STATE,
	OP_SAVE_STATE,
	OP_LOAD_STATE
};

class sheepshaver_state
{
public:
	sheepshaver_cpu *ppc_cpu;

	void init_emul_ppc(void);
	void exit_emul_ppc(void);
	void emul_ppc(uint32 entry);

	int save_slot;
	save_op_t save_op;
	void save_state(int);
	void load_state(int);
	void do_save_load(void);
};

extern sheepshaver_state *the_app;
