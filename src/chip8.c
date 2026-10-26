/*
 * chip8.c - Implementation of the CHIP-8 machine model.
 *
 * Every function here takes a `struct chip8 *c` as first parameter.
 * That pointer is how the function knows WHICH machine to operate on
 * (C has no `this` keyword like C++ or Java - we pass it explicitly).
 */

#include <stdio.h>    /* fopen, fread, printf ... */
#include <string.h>   /* memset: fill a block of memory with a byte */

#include "chip8.h"

/*
 * The built-in font.
 * CHIP-8 displays hex digits 0-F using 4x5 pixel sprites.
 * Each sprite is 5 bytes; each byte is one row, and the HIGH 4 bits
 * are the pixels (sprites are 4 pixels wide). Example '0':
 *   0xF0 = 1111 0000  ->  XXXX
 *   0x90 = 1001 0000  ->  X..X
 *   0x90 = 1001 0000  ->  X..X
 *   0x90 = 1001 0000  ->  X..X
 *   0xF0 = 1111 0000  ->  XXXX
 * This table is copied into RAM at FONT_ADDR at startup, exactly
 * like the original interpreter did.
 */
static const uint8_t font[FONT_SIZE] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0,  /* 0 */
    0x20, 0x60, 0x20, 0x20, 0x70,  /* 1 */
    0xF0, 0x10, 0xF0, 0x80, 0xF0,  /* 2 */
    0xF0, 0x10, 0xF0, 0x10, 0xF0,  /* 3 */
    0x90, 0x90, 0xF0, 0x10, 0x10,  /* 4 */
    0xF0, 0x80, 0xF0, 0x10, 0xF0,  /* 5 */
    0xF0, 0x80, 0xF0, 0x90, 0xF0,  /* 6 */
    0xF0, 0x10, 0x20, 0x40, 0x40,  /* 7 */
    0xF0, 0x90, 0xF0, 0x90, 0xF0,  /* 8 */
    0xF0, 0x90, 0xF0, 0x10, 0xF0,  /* 9 */
    0xF0, 0x90, 0xF0, 0x90, 0x90,  /* A */
    0xE0, 0x90, 0xE0, 0x90, 0xE0,  /* B */
    0xF0, 0x80, 0x80, 0x80, 0xF0,  /* C */
    0xE0, 0x90, 0x90, 0x90, 0xE0,  /* D */
    0xF0, 0x80, 0xF0, 0x80, 0xF0,  /* E */
    0xF0, 0x80, 0xF0, 0x80, 0x80   /* F */
};

void chip8_init(struct chip8 *c)
{
    /* memset = "set N bytes to this value". Here: wipe all RAM to 0. */
    memset(c->memory, 0, MEM_SIZE);
    memset(c->V, 0, sizeof(c->V));
    memset(c->stack, 0, sizeof(c->stack));

    c->I  = 0;
    c->pc = PROG_START;   /* programs start executing at 0x200, always  */
    c->sp = 0;            /* empty stack (counts used entries)           */
    c->delay_timer = 0;
    c->sound_timer = 0;
    c->prog_size = 0;

    /* Copy the font sprites into the interpreter area of RAM. */
    for (int i = 0; i < FONT_SIZE; i++)
        c->memory[FONT_ADDR + i] = font[i];
}

int chip8_load_rom(struct chip8 *c, const char *path)
{
    /* "rb" = read binary. Not "r": text mode would translate newlines
       and corrupt binary data on some systems. Always "rb" for bytes. */
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error: cannot open ROM '%s'\n", path);
        return -1;
    }

    /* Find file size: seek to the end, ask for the position, go back. */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Safety check: the ROM must fit between 0x200 and 0xFFF. */
    if (size <= 0 || size > MEM_SIZE - PROG_START) {
        fprintf(stderr, "error: ROM '%s' has an invalid size (%ld bytes)\n",
                path, size);
        fclose(f);
        return -2;
    }

    /* fread(destination_address, element_size, count, file).
       &c->memory[PROG_START] is the ADDRESS of the 0x200th byte:
       fread copies file bytes straight into RAM from there. */
    size_t read = fread(&c->memory[PROG_START], 1, (size_t)size, f);
    fclose(f);

    if ((long)read != size) {
        fprintf(stderr, "error: short read on '%s' (%zu/%ld bytes)\n",
                path, read, size);
        return -3;
    }

    c->prog_size = (uint16_t)size;
    return 0;
}

void chip8_dump(const struct chip8 *c, uint16_t start, uint16_t len)
{
    /* Classic hex-dump format:
         0200  6A 02 68 0F  ...   j...        address + bytes + printable */
    for (uint16_t i = 0; i < len; i++) {
        uint16_t addr = (uint16_t)(start + i);

        if (i % 16 == 0)                       /* new row: print address */
            printf("\n%04X  ", addr);

        printf("%02X ", c->memory[addr]);
    }
    printf("\n");
}
