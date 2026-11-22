#include "header/user/syscall.h"

int main(void) {
    static const char message[] = "Hello, World!\n";

    sys_write_text(message, sizeof(message) - 1u);
    return 0;
}
