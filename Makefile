# Compiler & linker
ASM           = nasm
LIN           = ld
CC            = gcc

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage

# RAM in MiB, used by the kernel and QEMU. Example: make run SYSTEM_MEMORY_MB=6
SYSTEM_MEMORY_MB ?= 128

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER) -DSYSTEM_MEMORY_MB=$(SYSTEM_MEMORY_MB)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386

run: all insert-shell
	qemu-system-i386 -m $(SYSTEM_MEMORY_MB)M -s -S -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso
all: build
build: iso
clean:
	rm -rf $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/kernel
kernel:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/cpu/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/syscall.c -o $(OUTPUT_FOLDER)/syscall.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/disk.c -o $(OUTPUT_FOLDER)/disk.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/filesystem/ext2.c -o $(OUTPUT_FOLDER)/ext2.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/memory/paging.c -o $(OUTPUT_FOLDER)/paging.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/memory/swap.c -o $(OUTPUT_FOLDER)/swap.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/memory/process_memory.c -o $(OUTPUT_FOLDER)/process_memory.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/text/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/string.o
	@$(LIN) $(LFLAGS) $(OUTPUT_FOLDER)/*.o -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generate elf32...
	@rm -f *.o
iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
	@cd $(OUTPUT_FOLDER) && genisoimage -R					\
								-b boot/grub/grub1      	\
								-no-emul-boot 				\
								-boot-load-size 4          	\
								-A os                     	\
								-input-charset utf8       	\
								-quiet                    	\
								-boot-info-table          	\
								-o OS2025.iso             	\
								iso 
	@rm -r $(OUTPUT_FOLDER)/iso/
disk:
	@mkdir -p $(OUTPUT_FOLDER)
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 5M

# User programs are flat binaries at address 0. Each one gets its own object folder under bin/user,
# so its link sees only its own objects and the kernel link (bin/*.o) sees none of them.
USER_PROGRAMS = shell hello clock
USER_COMMON   = user/crt0.c user/syscall.c stdlib/string.c
USER_SOURCES_shell = user/shell.c user/grep.c user/proc.c
USER_SOURCES_hello = user/hello.c
USER_SOURCES_clock = user/clock.c
# Static pattern: a plain user-% would also match user-apps and build a program called "apps"
$(addprefix user-,$(USER_PROGRAMS)): user-%:
	@rm -rf $(OUTPUT_FOLDER)/user/$* && mkdir -p $(OUTPUT_FOLDER)/user/$*
	@for f in $(USER_COMMON) $(USER_SOURCES_$*); do \
		$(CC) $(CFLAGS) -fno-pie -fno-asynchronous-unwind-tables $(SOURCE_FOLDER)/$$f \
			-o $(OUTPUT_FOLDER)/user/$*/$$(basename $$f .c).o || exit 1; \
	done
	@$(LIN) -T $(SOURCE_FOLDER)/user/user-linker.ld -melf_i386 $(OUTPUT_FOLDER)/user/$*/*.o -o $(OUTPUT_FOLDER)/$*
	@echo Linking user program $*...
user-apps: $(addprefix user-,$(USER_PROGRAMS))

# Host program, so read/write/delete are renamed away from the C library ones.
inserter:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(CC) -std=gnu11 -Wall -Wextra -Werror -I$(SOURCE_FOLDER) -Dread=fs_read -Dwrite=fs_write -Ddelete=fs_delete \
		tools/inserter.c $(SOURCE_FOLDER)/filesystem/ext2.c -o $(OUTPUT_FOLDER)/inserter

insert-shell: user-shell inserter
	@./$(OUTPUT_FOLDER)/inserter $(OUTPUT_FOLDER)/shell 1 $(OUTPUT_FOLDER)/$(DISK_NAME).bin

insert-apps: user-apps inserter
	@for p in $(USER_PROGRAMS); do \
		./$(OUTPUT_FOLDER)/inserter $(OUTPUT_FOLDER)/$$p 1 $(OUTPUT_FOLDER)/$(DISK_NAME).bin || exit 1; \
	done
