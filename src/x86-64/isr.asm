; Copyright 2026 Rafael Peixoto
; 
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the “Software”), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so.
; 
; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
; AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
; DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
; FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
; DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
; SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
; CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
; OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
; OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

; isr.asm - entry stubs for the 32 CPU exceptions.
; Assemble with:  nasm -f elf64 isr.asm -o isr.o
; Syntax: NASM, Intel.
;
; Every exception arrives here through its own tiny stub. The stubs make the
; stack look the same for all vectors (a fake error code is pushed when the CPU
; does not push a real one), then jump to isr_common, which saves the registers
; and calls exception_handler(struct interrupt_frame *) in kernel.c.

bits 64

extern exception_handler
global isr_stub_table

section .text

; Vectors where the CPU does NOT push an error code.
%macro ISR_NOERR 1
isr_stub_%+%1:
    push qword 0                ; fake error code, keeps the frame uniform
    push qword %1               ; vector number
    jmp isr_common
%endmacro

; Vectors where the CPU already pushed an error code.
%macro ISR_ERR 1
isr_stub_%+%1:
    push qword %1               ; vector number
    jmp isr_common
%endmacro

ISR_NOERR 0                     ; #DE  Divide Error
ISR_NOERR 1                     ; #DB  Debug
ISR_NOERR 2                     ; NMI
ISR_NOERR 3                     ; #BP  Breakpoint
ISR_NOERR 4                     ; #OF  Overflow
ISR_NOERR 5                     ; #BR  BOUND Range Exceeded
ISR_NOERR 6                     ; #UD  Invalid Opcode
ISR_NOERR 7                     ; #NM  Device Not Available
ISR_ERR   8                     ; #DF  Double Fault (error code is always 0)
ISR_NOERR 9                     ; Coprocessor Segment Overrun (legacy)
ISR_ERR   10                    ; #TS  Invalid TSS
ISR_ERR   11                    ; #NP  Segment Not Present
ISR_ERR   12                    ; #SS  Stack-Segment Fault
ISR_ERR   13                    ; #GP  General Protection
ISR_ERR   14                    ; #PF  Page Fault
ISR_NOERR 15                    ; reserved
ISR_NOERR 16                    ; #MF  x87 Floating-Point Error
ISR_ERR   17                    ; #AC  Alignment Check
ISR_NOERR 18                    ; #MC  Machine Check
ISR_NOERR 19                    ; #XM  SIMD Floating-Point Exception
ISR_NOERR 20                    ; #VE  Virtualization Exception
ISR_ERR   21                    ; #CP  Control Protection
ISR_NOERR 22                    ; reserved
ISR_NOERR 23                    ; reserved
ISR_NOERR 24                    ; reserved
ISR_NOERR 25                    ; reserved
ISR_NOERR 26                    ; reserved
ISR_NOERR 27                    ; reserved
ISR_NOERR 28                    ; #HV  Hypervisor Injection
ISR_ERR   29                    ; #VC  VMM Communication
ISR_ERR   30                    ; #SX  Security Exception
ISR_NOERR 31                    ; reserved

isr_common:
    ; Save the general-purpose registers. The order here is the reverse of
    ; the field order in struct interrupt_frame (idt.h).
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp                ; 1st argument: pointer to the frame
    cld                         ; the C ABI expects the direction flag clear
    call exception_handler

    ; exception_handler normally never returns (it halts), but if a handler
    ; is ever changed to return, restore everything and resume.
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16                 ; drop the vector and the error code
    iretq

; Table of stub addresses, read by idt_init() in idt.c.
section .rodata
align 8
isr_stub_table:
%assign i 0
%rep 32
    dq isr_stub_%+i
%assign i i+1
%endrep