/* linker.ld - places the kernel at 1 MiB, with the Multiboot2 header first. */

ENTRY(_start)

SECTIONS
{
    . = 1M;

    /* GRUB looks for the header in the first 32 KiB of the image. */
    .multiboot_header : ALIGN(8)
    {
        KEEP(*(.multiboot_header))
    }

    .text : ALIGN(4K)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4K)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.note .note.*)
    }
}