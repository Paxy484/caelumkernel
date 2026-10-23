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
 * gdt.h - the Global Descriptor Table and the Task State Segment.
 */

#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/*
 * Segment selectors. A selector is (GDT index * 8) | RPL, where RPL is the
 * requested privilege level in the low two bits.
 *
 * The two user entries are ordered DATA first, then CODE. This is what the
 * syscall/sysret instructions need if Caelum uses them later; it makes no
 * difference for int 0x80.
 */
#define GDT_KERNEL_CODE 0x08            /* index 1, ring 0 */
#define GDT_KERNEL_DATA 0x10            /* index 2, ring 0 */
#define GDT_USER_DATA   0x1B            /* index 3, ring 3 (0x18 | 3) */
#define GDT_USER_CODE   0x23            /* index 4, ring 3 (0x20 | 3) */
#define GDT_TSS         0x28            /* index 5-6 (a 64-bit TSS takes two slots) */

/* Build the GDT and TSS, load them, and reload every segment register. */
void gdt_init(void);

/* Stack the CPU switches to when ring 3 enters the kernel (TSS.RSP0). */
void tss_set_kernel_stack(uint64_t rsp0);

#endif