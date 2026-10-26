/*
 * main.c - Entry point of the CHIP-8 emulator.
 *
 * Stage 1: we can now reset the machine, load a ROM into RAM and
 * dump memory to see it arrived. No opcodes execute yet.
 */

#include <stdio.h>

#include "chip8.h"

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom-file>\n", argv[0]);
        return 1;
    }

    /* One instance of `struct chip8` = one emulated machine.
       We keep it on main's stack; a pointer to it is passed around. */
    struct chip8 machine;

    chip8_init(&machine);          /* & gives the machine's address */
    printf("machine reset: pc=0x%03X, font loaded at 0x%03X\n",
           machine.pc, FONT_ADDR);

    if (chip8_load_rom(&machine, argv[1]) != 0)
        return 2;                  /* distinct error code: ROM problem */

    printf("loaded ROM '%s' (%u bytes) at 0x%03X\n",
           argv[1], machine.prog_size, PROG_START);

    /* Let's SEE what is in RAM now. */
    printf("\n--- font area (0x050) ---\n");
    chip8_dump(&machine, FONT_ADDR, 32);

    printf("\n--- program area (0x200) ---\n");
    chip8_dump(&machine, PROG_START, 64);

    return 0;
}
