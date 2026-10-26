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

uint16_t chip8_fetch(const struct chip8 *c, uint16_t pc)
{
    /* An instruction is 2 bytes, stored BIG-ENDIAN (high byte first):
         memory[pc]   = 0x6A   ->  high byte
         memory[pc+1] = 0x02   ->  low byte
       Combine them: shift the high byte 8 bits left (x256) and OR the
       low byte into the empty space. Result for this example: 0x6A02. */
    return (uint16_t)((c->memory[pc] << 8) | c->memory[pc + 1]);
}

void chip8_print_registers(const struct chip8 *c)
{
    for (int i = 0; i < 16; i++) {
        if (i % 4 == 0)
            printf("\n  ");
        printf("V%X=%02X  ", i, c->V[i]);
    }
    printf("\n  I=0x%03X  PC=0x%03X  SP=%u\n", c->I, c->pc, c->sp);
}

int chip8_step(struct chip8 *c)
{
    uint16_t opcode = chip8_fetch(c, c->pc);

    /* Pull the opcode apart (see lesson 2 on masking). */
    uint8_t n1   = (opcode & 0xF000) >> 12;
    uint8_t x    = (opcode & 0x0F00) >>  8;  /* usually a register #   */
    uint8_t NN   =  opcode & 0x00FF;         /* low byte, 8-bit const  */
    uint16_t NNN =  opcode & 0x0FFF;         /* low 12 bits, address   */

    /* DEFAULT: move to the next instruction. Jumps/calls overwrite
       this below. One default, then a switch - no per-case PC juggling. */
    c->pc += 2;

    switch (n1) {
    case 0x0:
        if (opcode == 0x00EE) {
            /* RET: pop the return address. `sp` counts used entries:
               push writes at sp then increments, pop decrements then reads. */
            if (c->sp == 0)
                return -2;              /* stack underflow (empty stack) */
            c->sp--;
            c->pc = c->stack[c->sp];
        } else if (opcode != 0x00E0) {
            /* Only 00E0 (CLS) and 00EE (RET) exist with n1=0.
               0x0000 is NOT a real instruction - refusing it loudly
               saved us from silently executing empty memory. */
            return -1;
        }
        /* 00E0 (clear screen) is a no-op until we have a framebuffer. */
        break;

    case 0x1:   /* JMP NNN: overwrite PC */
        c->pc = NNN;
        break;

    case 0x2:   /* CALL NNN: push current PC, then jump */
        if (c->sp >= STACK_SIZE)
            return -2;                  /* stack overflow */
        c->stack[c->sp] = c->pc;
        c->sp++;
        c->pc = NNN;
        break;

    case 0x3:   /* SE VX,NN: skip next instruction if equal */
        if (c->V[x] == NN)
            c->pc += 2;
        break;

    case 0x4:   /* SNE VX,NN: skip next instruction if not equal */
        if (c->V[x] != NN)
            c->pc += 2;
        break;

    case 0x6:   /* SET VX,NN */
        c->V[x] = NN;
        break;

    case 0x7:   /* ADD VX,NN - wraps at 0x100, no carry flag here */
        c->V[x] = (uint8_t)(c->V[x] + NN);
        break;

    case 0xA:   /* LD I,NNN: set the address register */
        c->I = NNN;
        break;

    default:
        c->pc -= 2;   /* rewind: unknown opcode, PC stays where it was */
        return -1;
    }

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
