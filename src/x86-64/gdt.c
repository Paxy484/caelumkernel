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
 * gdt.c - Global Descriptor Table with ring 0 and ring 3 segments, plus a TSS.
 *
 * In 64-bit mode segmentation is mostly switched off: base and limit of code
 * and data segments are ignored. What still matters is the privilege level
 * (DPL) of each segment, the "long mode" bit on code segments, and the TSS,
 * which tells the CPU which stack to use when ring 3 enters the kernel.
 */

#include "gdt.h"

/* ── Descriptors ──────────────────────────────────────────────────────────── */

/*
 * Access byte:  P | DPL(2) | S | type(4)
 *   0x9A = present, ring 0, code, executable + readable
 *   0x92 = present, ring 0, data, readable + writable
 *   0xFA = present, ring 3, code, executable + readable
 *   0xF2 = present, ring 3, data, readable + writable
 *
 * Flags nibble: G | D/B | L | AVL
 *   0xA = 4 KiB granularity, L=1 (64-bit code), D=0
 *   0xC = 4 KiB granularity, D/B=1 (data segments)
 */
#define ACCESS_KERNEL_CODE 0x9A
#define ACCESS_KERNEL_DATA 0x92
#define ACCESS_USER_CODE   0xFA
#define ACCESS_USER_DATA   0xF2

#define FLAGS_CODE_64      0xA
#define FLAGS_DATA         0xC

/* A flat segment: base 0, limit 0xFFFFF (with 4 KiB granularity = 4 GiB). */
static uint64_t flat_descriptor(uint8_t access, uint8_t flags) {
    uint64_t d = 0;
    d |= 0xFFFFULL;                         /* limit, bits 0-15  */
    d |= (uint64_t)access << 40;
    d |= (uint64_t)0xF << 48;               /* limit, bits 16-19 */
    d |= (uint64_t)(flags & 0xF) << 52;
    return d;
}

/* ── Task State Segment ───────────────────────────────────────────────────── */

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;              /* stack used when entering ring 0 from ring 3 */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];            /* emergency stacks for selected interrupts   */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;        /* >= TSS size means "no I/O permission map"   */
} __attribute__((packed));

_Static_assert(sizeof(struct tss) == 104, "a 64-bit TSS is 104 bytes");

static struct tss tss;

/* Stack for ring 3 -> ring 0 transitions until the scheduler picks its own. */
#define KERNEL_STACK_SIZE 8192
static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));

/* A 64-bit TSS descriptor is 16 bytes, so it takes two GDT slots. */
static void set_tss_descriptor(int index, uint64_t base, uint32_t limit);

/* ── The table ────────────────────────────────────────────────────────────── */

#define GDT_ENTRIES 7           /* null, 4 segments, and a 2-slot TSS */

/* Must live in writable memory: ltr marks the TSS descriptor as "busy". */
static uint64_t gdt[GDT_ENTRIES];

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static void set_tss_descriptor(int index, uint64_t base, uint32_t limit) {
    uint64_t low = 0;
    low |= (uint64_t)(limit & 0xFFFF);                  /* limit, bits 0-15   */
    low |= (base & 0xFFFFFFULL) << 16;                  /* base, bits 0-23    */
    low |= (uint64_t)0x89 << 40;                        /* present, ring 0, available 64-bit TSS */
    low |= (uint64_t)((limit >> 16) & 0xF) << 48;       /* limit, bits 16-19  */
    low |= ((base >> 24) & 0xFFULL) << 56;              /* base, bits 24-31   */

    gdt[index]     = low;
    gdt[index + 1] = base >> 32;                        /* base, bits 32-63   */
}

static void gdt_build(void) {
    gdt[0] = 0;                                                         /* 0x00 null        */
    gdt[1] = flat_descriptor(ACCESS_KERNEL_CODE, FLAGS_CODE_64);        /* 0x08 kernel code */
    gdt[2] = flat_descriptor(ACCESS_KERNEL_DATA, FLAGS_DATA);           /* 0x10 kernel data */
    gdt[3] = flat_descriptor(ACCESS_USER_DATA,   FLAGS_DATA);           /* 0x18 user data   */
    gdt[4] = flat_descriptor(ACCESS_USER_CODE,   FLAGS_CODE_64);        /* 0x20 user code   */

    tss.rsp0       = (uint64_t)(uintptr_t)(kernel_stack + KERNEL_STACK_SIZE);
    tss.iomap_base = sizeof(tss);                                       /* no I/O bitmap: ring 3 gets no port access */
    set_tss_descriptor(5, (uint64_t)(uintptr_t)&tss, sizeof(tss) - 1);  /* 0x28 TSS         */
}

void tss_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(void) {
    gdt_build();

    struct gdt_ptr gdtr = {
        .limit = (uint16_t)(sizeof(gdt) - 1),
        .base  = (uint64_t)(uintptr_t)gdt,
    };
    __asm__ volatile ("lgdt %0" : : "m"(gdtr));

    /*
     * Reload the segment registers from the new table. The data segments are
     * a plain mov. CS cannot be written with mov, so push the new selector
     * and a return address and "return" far into the same place.
     */
    __asm__ volatile (
        "movw %[kdata], %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorl %%eax, %%eax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "pushq %[kcode]\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n"
        "1:"
        :
        : [kdata] "i"(GDT_KERNEL_DATA), [kcode] "i"(GDT_KERNEL_CODE)
        : "rax", "memory");

    /* Tell the CPU where the TSS is. */
    uint16_t tss_selector = GDT_TSS;
    __asm__ volatile ("ltr %0" : : "r"(tss_selector));
}