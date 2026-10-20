# Build with Clang + lld + NASM. Needs: clang lld nasm grub-pc-bin xorriso mtools qemu-system-x86
CC      := clang
LD      := ld.lld
ASM     := nasm

CFLAGS  := --target=x86_64-unknown-none-elf -std=c11 -g -Wall -Wextra \
           -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -mno-red-zone -mgeneral-regs-only
LDFLAGS := -T linker.ld -nostdlib -z max-page-size=0x1000

OBJS    := entry.o isr.o idt.o kernel.o

# Deliberately trigger an exception to test the IDT (rebuild with clean):
#   make clean run TEST=DIV     divide by zero        (vector 0)
#   make clean run TEST=PF      write to unmapped RAM (vector 14)
#   make clean run TEST=UD      invalid instruction   (vector 6)
ifdef TEST
CFLAGS += -DTEST_$(TEST)
endif

.PHONY: all iso run debug check clean

all: kernel.elf

%.o: %.asm
	$(ASM) -f elf64 $< -o $@

%.o: %.c idt.h
	$(CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

iso: kernel.elf grub.cfg
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/kernel.elf
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o caelum.iso iso

run: iso
	qemu-system-x86_64 -cdrom caelum.iso -serial stdio

# Starts QEMU paused, waiting for GDB:  gdb kernel.elf -ex "target remote :1234"
debug: iso
	qemu-system-x86_64 -cdrom caelum.iso -serial stdio -s -S

# Confirms GRUB will find the Multiboot2 header (must be inside the first 32 KiB).
check: kernel.elf
	@readelf -lW kernel.elf | grep LOAD
	@grub-file --is-x86-multiboot2 kernel.elf && echo "OK: valid Multiboot2 image"

clean:
	rm -rf *.o kernel.elf iso caelum.iso