/*
 * Copyright 2026 Rafael Peixoto
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the “Software”), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * kernel.c - first steps of a tiny 64-bit kernel.
 * Entered from entry.asm in long mode: kmain(magic, info).
 */

#include <stddef.h>
#include <stdint.h>

#include "idt.h"
#include "gdt.h"

#define MB2_BOOT_MAGIC 0x36D76289u

/* ── Port I/O ─────────────────────────────────────────────────────────────── */

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* ── Serial port (COM1), handy because QEMU can print it to your terminal ──── */

#define COM1 0x3F8

static void serial_init(void) {
    outb(COM1 + 1, 0x00);   /* disable interrupts */
    outb(COM1 + 3, 0x80);   /* enable divisor latch */
    outb(COM1 + 0, 0x03);   /* divisor 3 = 38400 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);   /* enable FIFO */
    outb(COM1 + 4, 0x0B);   /* RTS/DSR set */
}

static void serial_putc(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) { }
    outb(COM1, (uint8_t)c);
}

/* ── VGA text mode (80x25 at 0xB8000) ─────────────────────────────────────── */

#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTR 0x0F                       /* white on black */

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static size_t vga_row = 0;
static size_t vga_col = 0;

static void vga_clear(void) {
    for (size_t i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        vga[i] = (uint16_t)(' ' | (VGA_ATTR << 8));
    }
    vga_row = vga_col = 0;
}

static void vga_scroll(void) {
    for (size_t i = 0; i < VGA_COLS * (VGA_ROWS - 1); i++) {
        vga[i] = vga[i + VGA_COLS];
    }
    for (size_t i = 0; i < VGA_COLS; i++) {
        vga[VGA_COLS * (VGA_ROWS - 1) + i] = (uint16_t)(' ' | (VGA_ATTR << 8));
    }
    vga_row = VGA_ROWS - 1;
}

static void vga_putc(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else {
        vga[vga_row * VGA_COLS + vga_col] = (uint16_t)((uint8_t)c | (VGA_ATTR << 8));
        if (++vga_col == VGA_COLS) {
            vga_col = 0;
            vga_row++;
        }
    }
    if (vga_row == VGA_ROWS) vga_scroll();
}

/* ── Console: everything goes to both the screen and the serial port ──────── */

static void kputc(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    vga_putc(c);
}

static void kputs(const char *s) {
    while (*s) kputc(*s++);
}

static void kput_hex(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    kputs("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        kputc(digits[(value >> shift) & 0xF]);
    }
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

__attribute__((noreturn)) static void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* ── CPU exceptions ───────────────────────────────────────────────────────── */

static const char *const exception_names[32] = {
    "Divide Error (#DE)",           "Debug (#DB)",
    "Non-Maskable Interrupt",       "Breakpoint (#BP)",
    "Overflow (#OF)",               "BOUND Range Exceeded (#BR)",
    "Invalid Opcode (#UD)",         "Device Not Available (#NM)",
    "Double Fault (#DF)",           "Coprocessor Segment Overrun",
    "Invalid TSS (#TS)",            "Segment Not Present (#NP)",
    "Stack-Segment Fault (#SS)",    "General Protection Fault (#GP)",
    "Page Fault (#PF)",             "Reserved",
    "x87 Floating-Point Error (#MF)", "Alignment Check (#AC)",
    "Machine Check (#MC)",          "SIMD Floating-Point Exception (#XM)",
    "Virtualization Exception (#VE)", "Control Protection Exception (#CP)",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection (#HV)",   "VMM Communication (#VC)",
    "Security Exception (#SX)",     "Reserved",
};

/* Vectors for which the CPU pushes a real error code. */
static int exception_has_error_code(uint64_t vector) {
    switch (vector) {
    case 8: case 10: case 11: case 12: case 13: case 14:
    case 17: case 21: case 29: case 30:
        return 1;
    default:
        return 0;
    }
}

static inline uint64_t read_cr2(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

/* Called from isr.asm for every CPU exception. For now they are all fatal:
   print what happened and stop, instead of triple-faulting and rebooting. */
void exception_handler(struct interrupt_frame *frame) {
    kputs("\n*** EXCEPTION ");
    kput_hex(frame->vector);
    kputs(": ");
    kputs(frame->vector < 32 ? exception_names[frame->vector] : "Unknown");
    kputs(" ***\n");

    if (exception_has_error_code(frame->vector)) {
        kputs("Error code: ");
        kput_hex(frame->error_code);
        kputs("\n");
    }
    if (frame->vector == 14) {
        kputs("Faulting address (CR2): ");
        kput_hex(read_cr2());
        kputs("\n");
    }

    kputs("RIP: ");    kput_hex(frame->rip);    kputs("\n");
    kputs("CS:  ");    kput_hex(frame->cs);     kputs("\n");
    kputs("RFLAGS: "); kput_hex(frame->rflags); kputs("\n");
    kputs("RSP: ");    kput_hex(frame->rsp);    kputs("\n");
    kputs("SS:  ");    kput_hex(frame->ss);     kputs("\n");
    kputs("\nSystem halted.\n");

    halt_forever();
}

/* ── Segment registers (used to check that the new GDT is active) ─────────── */

static inline uint16_t read_cs(void) {
    uint16_t value;
    __asm__ volatile ("mov %%cs, %0" : "=r"(value));
    return value;
}

static inline uint16_t read_ss(void) {
    uint16_t value;
    __asm__ volatile ("mov %%ss, %0" : "=r"(value));
    return value;
}

static inline uint16_t read_tr(void) {
    uint16_t value;
    __asm__ volatile ("str %0" : "=r"(value));
    return value;
}

void kmain(uint32_t magic, uint32_t info) {
    serial_init();
    vga_clear();

    kputs("Hello from my 64-bit kernel!\n\n");

    kputs("Multiboot2 magic: ");
    kput_hex(magic);
    kputs(magic == MB2_BOOT_MAGIC ? "  (ok)\n" : "  (UNEXPECTED)\n");

    kputs("Multiboot2 info:  ");
    kput_hex(info);
    kputs("\n");

    gdt_init();
    kputs("GDT and TSS loaded:  CS=");
    kput_hex(read_cs());
    kputs("  SS=");
    kput_hex(read_ss());
    kputs("  TR=");
    kput_hex(read_tr());
    kputs("\n");

    idt_init();
    kputs("IDT loaded: CPU exceptions are now handled.\n");

    /* Build with e.g. "make clean run TEST=DIV" to trigger an exception on purpose. */
#if defined(TEST_DIV)
    kputs("Test: dividing by zero...\n");
    volatile int zero = 0;
    volatile int boom = 1 / zero;
    (void)boom;
#elif defined(TEST_PF)
    kputs("Test: writing to an unmapped address...\n");
    *(volatile uint64_t *)0x80000000ULL = 1;    /* only the first 1 GiB is mapped */
#elif defined(TEST_UD)
    kputs("Test: executing an invalid instruction...\n");
    __asm__ volatile ("ud2");
#endif

    halt_forever();
}