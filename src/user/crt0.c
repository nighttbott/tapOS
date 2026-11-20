#include "user/syscall.h"

int main(void);

/* Entry of every user program, placed at address 0 by user-linker.ld. */
__attribute__((section(".text.entry"), noreturn))
void _start(void) {
    sys_exit(main());
    // Reached only while the kernel has no SYSCALL_EXIT (it answers -1)
    for (;;) {
    }
}
