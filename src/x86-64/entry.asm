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

; entry.asm - boots from GRUB (32-bit protected mode) into 64-bit long mode.
; Assemble with:  nasm -f elf64 entry.asm -o entry.o
; Syntax: NASM, Intel.

MB2_MAGIC       equ 0xE85250D6          ; Multiboot2 header magic
MB2_ARCH_I386   equ 0                   ; protected-mode i386 entry
MB2_BOOT_MAGIC  equ 0x36D76289          ; value GRUB leaves in EAX

; ---------------------------------------------------------------------------
; Multiboot2 header. GRUB scans the first 32 KiB of the image for it, so the
; linker script places this section first.
; ---------------------------------------------------------------------------
section .multiboot_header
align 8
mb2_header_start:
    dd MB2_MAGIC
    dd MB2_ARCH_I386
    dd mb2_header_end - mb2_header_start
    dd 0x100000000 - (MB2_MAGIC + MB2_ARCH_I386 + (mb2_header_end - mb2_header_start))
    ; end tag
    dw 0                                ; type
    dw 0                                ; flags
    dd 8                                ; size
mb2_header_end:

; ---------------------------------------------------------------------------
; 32-bit entry point. GRUB jumps here with:
;   EAX = 0x36D76289 (Multiboot2 magic), EBX = physical address of the info
;   structure, paging off, interrupts off, flat 32-bit segments.
; EDI and ESI keep the magic and the info pointer until kmain is called.
; ---------------------------------------------------------------------------
section .text
bits 32
global _start
extern kmain

_start:
    cli
    mov esp, stack_top
    mov edi, eax                        ; 1st argument for kmain: magic
    mov esi, ebx                        ; 2nd argument for kmain: info pointer

    call check_multiboot
    call check_cpuid
    call check_long_mode

    call setup_page_tables
    call enable_paging

    lgdt [gdt64.pointer]
    jmp gdt64.code:long_mode_start      ; far jump into the 64-bit code segment

; --- Checks: on failure print "ERR: <letter>" and halt ----------------------

check_multiboot:
    cmp eax, MB2_BOOT_MAGIC
    jne .fail
    ret
.fail:
    mov al, 'M'
    jmp error

check_cpuid:
    ; CPUID exists if bit 21 of EFLAGS can be flipped.
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .fail
    ret
.fail:
    mov al, 'C'
    jmp error

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .fail
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29                   ; LM bit
    jz .fail
    ret
.fail:
    mov al, 'L'
    jmp error

; --- Paging: identity-map the first 1 GiB with 2 MiB pages ------------------

setup_page_tables:
    mov eax, p3_table
    or eax, 0b11                        ; present + writable
    mov [p4_table], eax

    mov eax, p2_table
    or eax, 0b11
    mov [p3_table], eax

    xor ecx, ecx
.map_p2:
    mov eax, 0x200000                   ; 2 MiB
    mul ecx
    or eax, 0b10000011                  ; present + writable + huge
    mov [p2_table + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2
    ret

enable_paging:
    mov eax, p4_table
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5                      ; PAE
    mov cr4, eax

    mov ecx, 0xC0000080                 ; EFER MSR
    rdmsr
    or eax, 1 << 8                      ; LME: long mode enable
    wrmsr

    mov eax, cr0
    or eax, 1 << 31                     ; PG: turn paging on
    mov cr0, eax
    ret

error:
    ; Print "ERR: x" in white-on-red at the top-left of the screen, then halt.
    mov dword [0xB8000], 0x4F524F45     ; 'E' 'R'
    mov dword [0xB8004], 0x4F3A4F52     ; 'R' ':'
    mov dword [0xB8008], 0x4F204F20     ; ' ' ' '
    mov byte  [0xB800A], al
.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; 64-bit entry
; ---------------------------------------------------------------------------
bits 64
long_mode_start:
    xor eax, eax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov edi, edi                        ; zero-extend the saved 32-bit values
    mov esi, esi
    mov rsp, stack_top

    call kmain                          ; kmain(uint32_t magic, uint32_t info)

.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; Minimal 64-bit GDT (null + code segment)
; ---------------------------------------------------------------------------
section .rodata
align 8
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)    ; exec | code/data | present | 64-bit
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

; ---------------------------------------------------------------------------
; Page tables and stack
; ---------------------------------------------------------------------------
section .bss
align 4096
p4_table:     resb 4096
p3_table:     resb 4096
p2_table:     resb 4096
align 16
stack_bottom: resb 16384
stack_top: