# Compiler & linker
ASM           = nasm
LIN           = ld
CC            = gcc
OBJCOPY      ?= objcopy

.DEFAULT_GOAL := run

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage

SYSTEM_MEMORY_MB ?= 128

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER) -DSYSTEM_MEMORY_MB=$(SYSTEM_MEMORY_MB)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386

KERNEL_ASM_SOURCES = kernel-entrypoint.s cpu/intsetup.s cpu/context-switch.s
KERNEL_C_SOURCES = kernel.c cpu/gdt.c cpu/idt.c cpu/interrupt.c cpu/syscall.c \
                   cpu/portio.c driver/keyboard.c driver/disk.c filesystem/ext2.c \
                   memory/paging.c memory/swap.c text/framebuffer.c stdlib/string.c \
                   process/process.c process/loader.c process/scheduler.c
KERNEL_OBJECTS = $(addprefix $(OUTPUT_FOLDER)/,$(notdir $(KERNEL_ASM_SOURCES:.s=.o) $(KERNEL_C_SOURCES:.c=.o)))

.PHONY: run all build clean kernel iso disk user-apps inserter insert-shell insert-apps \
        kernel-c-sources kernel-asm-sources shell-module-sources shell_elf

run: all insert-shell
	qemu-system-i386 -m $(SYSTEM_MEMORY_MB)M -gdb tcp:127.0.0.1:1234 -S -drive file=$(OUTPUT_FOLDER)/$(DISK_NAME).bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso -boot order=d
all: build
build: iso
clean:
	rm -rf $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/kernel
	rm -rf $(OUTPUT_FOLDER)/user
	rm -f $(OUTPUT_FOLDER)/shell_elf $(OUTPUT_FOLDER)/inserter $(addprefix $(OUTPUT_FOLDER)/,$(USER_PROGRAMS))
kernel:
	@mkdir -p $(OUTPUT_FOLDER)
	@for source in $(KERNEL_ASM_SOURCES); do \
		$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/$$source -o $(OUTPUT_FOLDER)/$$(basename $$source .s).o || exit 1; \
	done
	@for source in $(KERNEL_C_SOURCES); do \
		$(CC) $(CFLAGS) $(SOURCE_FOLDER)/$$source -o $(OUTPUT_FOLDER)/$$(basename $$source .c).o || exit 1; \
	done
	@$(LIN) $(LFLAGS) $(KERNEL_OBJECTS) -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generate elf32...

kernel-c-sources:
	@printf '%s\n' $(addprefix $(SOURCE_FOLDER)/,$(KERNEL_C_SOURCES))

kernel-asm-sources:
	@printf '%s\n' $(addprefix $(SOURCE_FOLDER)/,$(KERNEL_ASM_SOURCES))

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
								-o $(ISO_NAME).iso             	\
								iso 
	@rm -r $(OUTPUT_FOLDER)/iso/
disk: $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@test "$$(wc -c < $<)" -ge 5242880 || { \
		echo "$< needs at least 5 MiB for EXT2 and swap. Back up the image before migrating it." >&2; \
		exit 1; \
	}

$(OUTPUT_FOLDER)/$(DISK_NAME).bin:
	@mkdir -p $(OUTPUT_FOLDER)
	@qemu-img create -f raw $@ 5M

USER_PROGRAMS = shell $(foreach app,hello clock,$(if $(wildcard $(SOURCE_FOLDER)/$(USER_SOURCES_$(app))),$(app)))
USER_COMMON   = user/syscall.c stdlib/string.c
SHELL_MODULE_SOURCES = user/shell-parser.c user/shell-pipeline.c user/fsutil.c \
                       user/command/navigation.c user/command/file.c user/command/find.c \
                       user/command/search.c user/command/process.c
USER_SOURCES_shell = user-shell.c $(SHELL_MODULE_SOURCES)
USER_SOURCES_hello = external/hello.c
USER_SOURCES_clock = external/clock.c
.PHONY: $(addprefix user-,$(USER_PROGRAMS))
$(addprefix user-,$(USER_PROGRAMS)): user-%:
	@rm -rf $(OUTPUT_FOLDER)/user/$* && mkdir -p $(OUTPUT_FOLDER)/user/$*
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o $(OUTPUT_FOLDER)/user/$*/crt0.o
	@for f in $(USER_COMMON) $(USER_SOURCES_$*); do \
		$(CC) $(CFLAGS) -fno-pie -fno-asynchronous-unwind-tables $(SOURCE_FOLDER)/$$f \
			-o $(OUTPUT_FOLDER)/user/$*/$$(basename $$f .c).o || exit 1; \
	done
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 \
		$(OUTPUT_FOLDER)/user/$*/crt0.o \
		$(addprefix $(OUTPUT_FOLDER)/user/$*/,$(notdir $(USER_COMMON:.c=.o) $(USER_SOURCES_$*:.c=.o))) \
		-o $(OUTPUT_FOLDER)/user/$*/$*.elf
	@if [ "$*" = shell ]; then \
		$(OBJCOPY) -O binary $(OUTPUT_FOLDER)/user/$*/$*.elf $(OUTPUT_FOLDER)/$* && \
		cp $(OUTPUT_FOLDER)/user/$*/$*.elf $(OUTPUT_FOLDER)/shell_elf; \
	else \
		$(OBJCOPY) --strip-all $(OUTPUT_FOLDER)/user/$*/$*.elf $(OUTPUT_FOLDER)/$*; \
	fi
	@echo Linking user program $*...
user-apps: $(addprefix user-,$(USER_PROGRAMS))
shell_elf: user-shell

shell-module-sources:
	@printf '%s\n' $(addprefix $(SOURCE_FOLDER)/,$(SHELL_MODULE_SOURCES))

inserter:
	@mkdir -p $(OUTPUT_FOLDER)
	@$(CC) -std=gnu11 -g -Wall -Wextra -Werror -I$(SOURCE_FOLDER) -Dread=fs_read -Dwrite=fs_write -Ddelete=fs_delete \
		$(SOURCE_FOLDER)/external/external-inserter.c $(SOURCE_FOLDER)/filesystem/ext2.c -o $(OUTPUT_FOLDER)/inserter

insert-shell: user-shell inserter | disk
	@./$(OUTPUT_FOLDER)/inserter -f $(OUTPUT_FOLDER)/shell 1 $(OUTPUT_FOLDER)/$(DISK_NAME).bin

insert-apps: user-apps inserter | disk
	@for p in $(USER_PROGRAMS); do \
		./$(OUTPUT_FOLDER)/inserter -f $(OUTPUT_FOLDER)/$$p 1 $(OUTPUT_FOLDER)/$(DISK_NAME).bin || exit 1; \
	done
