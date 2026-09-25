# Compiler & linker
LLVM_PREFIX   = $(shell brew --prefix llvm)
LLD_PREFIX    = $(shell brew --prefix lld)
ASM           = nasm
LIN           = $(LLD_PREFIX)/bin/ld.lld
CC            = clang
ISO           = $(shell command -v mkisofs)

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = uvuwevwe_OSas

# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nodefaultlibs -ffreestanding
ARCH_CFLAG	  = --target=i386-elf -m32 -march=i386 -mno-sse -mno-sse2 -mno-mmx -mno-avx -msoft-float
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) $(ARCH_CFLAG) -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386


run: all
	@qemu-system-i386 -s -S -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso
all: build
build: iso
clean:
	rm -rf $(OUTPUT_FOLDER)/*.o $(OUTPUT_FOLDER)/*.iso $(OUTPUT_FOLDER)/kernel



kernel:
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/cpu/isr.s -o $(OUTPUT_FOLDER)/isr-asm.o
# TODO: Compile C file with CFLAGS
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/pic.c -o $(OUTPUT_FOLDER)/pic.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/isr.c -o $(OUTPUT_FOLDER)/isr.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/portio.c -o $(OUTPUT_FOLDER)/portio.o
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
