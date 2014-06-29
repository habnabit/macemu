#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include "app.hpp"


static void read_exactly(void *dest, int fd, size_t length)
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

static void write_exactly(void *src, int fd, size_t length)
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
	void *buf = malloc(RAMSize);
	char filename[32];
	int fd;
	snprintf(filename, sizeof filename, "%d.save", save_slot);
	if (save_op == OP_SAVE_STATE) {
		if ((fd = open(filename, O_WRONLY | O_CREAT, 0666)) < 0) {
			perror("do_save_load: open");
			return;
		}
		Mac2Host_memcpy(buf, 0, RAMSize);
		write_exactly(buf, fd, RAMSize);
		write_exactly(ppc_cpu->regs_ptr(), fd, sizeof(powerpc_registers));
	} else if (save_op == OP_LOAD_STATE) {
		if ((fd = open(filename, O_RDONLY)) < 0) {
			perror("do_save_load: open");
			return;
		}
		read_exactly(buf, fd, RAMSize);
		read_exactly(ppc_cpu->regs_ptr(), fd, sizeof(powerpc_registers));
		Host2Mac_memcpy(0, buf, RAMSize);
		ppc_cpu->invalidate_cache();
	}
	close(fd);
	free(buf);
}
