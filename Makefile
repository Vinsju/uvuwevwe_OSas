.PHONY: run all build kernel iso disk clean user-shell inserter insert-hello user-hello insert-shell user-clock insert-clock
# Compiler & linker
LLVM_PREFIX   = $(shell brew --prefix llvm)
LLD_PREFIX    = $(shell brew --prefix lld)
ASM           = nasm
LIN           = $(shell command -v x86_64-elf-ld 2>/dev/null || echo "$(shell brew --prefix lld 2>/dev/null)/bin/ld.lld")
CC            = clang
ISO           = $(shell command -v mkisofs)

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = uvuwevwe_OSas
DISK_IMAGE    = $(OUTPUT_FOLDER)/storage.bin
DISK_SIZE     = 8M

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nodefaultlibs -ffreestanding
ARCH_CFLAG	  = --target=i386-elf -m32 -march=i386 -mno-sse -mno-sse2 -mno-mmx -mno-avx -msoft-float
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) $(ARCH_CFLAG) -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386


run: all
	@qemu-system-i386 \
		-rtc base=localtime \
		-cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso \
		-drive file=$(DISK_IMAGE),format=raw
all: build
build: iso

kernel:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/cpu/isr.s -o $(OUTPUT_FOLDER)/isr-asm.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/cpu/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
# TODO: Compile C file with CFLAGS
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/paging.c -o $(OUTPUT_FOLDER)/paging.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/swap.c -o $(OUTPUT_FOLDER)/swap.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/pic.c -o $(OUTPUT_FOLDER)/pic.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/isr.c -o $(OUTPUT_FOLDER)/isr.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/disk.c -o $(OUTPUT_FOLDER)/disk.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/string.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/ext2.c -o $(OUTPUT_FOLDER)/ext2.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/text/terminal.c -o $(OUTPUT_FOLDER)/terminal.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/syscall.c -o $(OUTPUT_FOLDER)/syscall.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/cmos.c -o $(OUTPUT_FOLDER)/cmos.o
	@$(LIN) $(LFLAGS) $(OUTPUT_FOLDER)/*.o -o $(OUTPUT_FOLDER)/kernel
	@echo Linking object files and generate elf32...
	@rm -f $(OUTPUT_FOLDER)/*.o

iso: kernel
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
# TODO: Create ISO image
	@$(ISO) -R \
		-b boot/grub/grub1 \
		-no-emul-boot \
		-boot-load-size 4 \
		-A os \
		-input-charset utf8 \
		-quiet \
		-boot-info-table \
		-o $(OUTPUT_FOLDER)/$(ISO_NAME).iso \
		$(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/

disk:
	@qemu-img create -f raw $(DISK_IMAGE) $(DISK_SIZE)

# Clean build artifacts without deleting storage.bin
clean:
	@rm -f $(OUTPUT_FOLDER)/kernel
	@rm -f $(OUTPUT_FOLDER)/$(ISO_NAME).iso
	@rm -f $(OUTPUT_FOLDER)/*.o
	@rm -f crt0.o user-shell.o string.o hello-world.o clock.o
	@rm -rf $(OUTPUT_FOLDER)/iso

# Shell (Book 3.3.3 + 3.4.2.1): flat binary untuk dimasukkan ke storage, ELF32 untuk debugging symbol
user-shell:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/user-shell.c -o user-shell.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/stdlib/string.c -o string.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o user-shell.o string.o -o $(OUTPUT_FOLDER)/shell
	@echo Linking object shell object files and generate flat binary...
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 \
		crt0.o user-shell.o string.o -o $(OUTPUT_FOLDER)/shell_elf
	@echo Linking object shell object files and generate ELF32 for debugging...
	@wc -c $(OUTPUT_FOLDER)/shell
	@rm -f *.o

inserter:
	@xcrun --sdk macosx clang \
		-isysroot $(shell xcrun --sdk macosx --show-sdk-path) \
		-g -Wall -Wextra -I$(SOURCE_FOLDER) \
		$(SOURCE_FOLDER)/stdlib/string.c \
		$(SOURCE_FOLDER)/ext2.c \
		$(SOURCE_FOLDER)/external/external-inserter.c \
		-o $(OUTPUT_FOLDER)/inserter

# Insert Hello World into EXT2 filesystem
insert-hello: inserter user-hello
	@echo Inserting Hello World into EXT2 filesystem...
	@cd $(OUTPUT_FOLDER); ./inserter -f hello 1 storage.bin

# Hello World: external user application
user-hello:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/external/hello-world.c -o hello-world.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o hello-world.o -o $(OUTPUT_FOLDER)/hello
	@echo Linking Hello World and generating flat binary...
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 \
		crt0.o hello-world.o -o $(OUTPUT_FOLDER)/hello_elf
	@echo Generating ELF32 for debugging...
	@wc -c $(OUTPUT_FOLDER)/hello
	@rm -f *.o

# Clock (Book 4.3.3): user program jam di pojok kanan bawah, dijalankan dengan exec clock
user-clock:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/crt0.s -o crt0.o
	@$(CC) $(CFLAGS) -fno-pie $(SOURCE_FOLDER)/external/clock.c -o clock.o
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 --oformat=binary \
		crt0.o clock.o -o $(OUTPUT_FOLDER)/clock
	@echo Linking Clock and generating flat binary...
	@$(LIN) -T $(SOURCE_FOLDER)/user-linker.ld -melf_i386 \
		crt0.o clock.o -o $(OUTPUT_FOLDER)/clock_elf
	@echo Generating ELF32 for debugging...
	@wc -c $(OUTPUT_FOLDER)/clock
	@rm -f *.o

# Insert Clock into EXT2 filesystem
insert-clock: inserter user-clock
	@echo Inserting Clock into EXT2 filesystem...
	@cd $(OUTPUT_FOLDER); ./inserter -f clock 1 storage.bin

# Insert Shell into EXT2 filesystem
insert-shell: inserter user-shell
	@echo Inserting Shell into EXT2 filesystem...
	@cd $(OUTPUT_FOLDER); ./inserter -f shell 1 storage.bin