/*
 * Copyright 2026 Rafael Peixoto
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 *    this list of conditions and the following disclaimer in the documentation 
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its 
 *    contributors may be used to endorse or promote products derived from 
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * idt.c - builds the Interrupt Descriptor Table for the 32 CPU exceptions.
 *
 * Interrupts stay disabled (entry.asm runs "cli"), so only exceptions can
 * reach these gates for now. Hardware interrupts come later.
 */

#include "idt.h"

#define IDT_ENTRIES     256
#define EXCEPTION_COUNT 32

/* Code segment selector from the GDT in entry.asm (null = 0x00, code = 0x08). */
#define KERNEL_CS       0x08

/* Present, ring 0, 64-bit interrupt gate (interrupts disabled on entry). */
#define GATE_INTERRUPT  0x8E

struct idt_entry {
    uint16_t offset_low;    /* handler address, bits 0-15  */
    uint16_t selector;      /* code segment                */
    uint8_t  ist;           /* interrupt stack table index (0 = none) */
    uint8_t  type_attr;     /* gate type, DPL, present     */
    uint16_t offset_mid;    /* handler address, bits 16-31 */
    uint32_t offset_high;   /* handler address, bits 32-63 */
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct idt_entry) == 16, "an IDT gate is 16 bytes");

static struct idt_entry idt[IDT_ENTRIES];

/* Addresses of the 32 assembly stubs, defined in isr.asm. */
extern const uint64_t isr_stub_table[EXCEPTION_COUNT];

static void idt_set_gate(uint8_t vector, uint64_t handler) {
    idt[vector].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[vector].selector    = KERNEL_CS;
    idt[vector].ist         = 0;
    idt[vector].type_attr   = GATE_INTERRUPT;
    idt[vector].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)(handler >> 32);
    idt[vector].reserved    = 0;
}

void idt_init(void) {
    for (uint8_t v = 0; v < EXCEPTION_COUNT; v++) {
        idt_set_gate(v, isr_stub_table[v]);
    }

    struct idt_ptr idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base  = (uint64_t)(uintptr_t)idt,
    };
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}