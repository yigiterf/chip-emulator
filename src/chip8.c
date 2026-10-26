/*
 * chip8.c - Implementation of the CHIP-8 machine model.
 *
 * Every function here takes a `struct chip8 *c` as first parameter.
 * That pointer is how the function knows WHICH machine to operate on
 * (C has no `this` keyword like C++ or Java - we pass it explicitly).
 *
 * This file now implements the COMPLETE CHIP-8 instruction set (35 opcodes).
 */

#include <stdio.h>    /* fopen, fread, printf ... */
#include <string.h>   /* memset: fill a block of memory with a byte */
#include <stdlib.h>   /* rand(): for the CXNN "random" instruction  */

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
    memset(c->gfx, 0, sizeof(c->gfx));
    memset(c->keypad, 0, sizeof(c->keypad));

    c->I  = 0;
    c->pc = PROG_START;   /* programs start executing at 0x200, always  */
    c->sp = 0;            /* empty stack (counts used entries)           */
    c->delay_timer = 0;
    c->sound_timer = 0;
    c->prog_size = 0;
    c->draw_flag = 0;

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

int chip8_step(struct chip8 *c)
{
    uint16_t opcode = chip8_fetch(c, c->pc);

    /* Pull the opcode apart (see lesson 2 on masking). */
    uint8_t n1   = (opcode & 0xF000) >> 12;   /* instruction type       */
    uint8_t x    = (opcode & 0x0F00) >>  8;   /* register # (VX)        */
    uint8_t y    = (opcode & 0x00F0) >>  4;   /* register # (VY)        */
    uint8_t n4   =  opcode & 0x000F;          /* low nibble             */
    uint8_t NN   =  opcode & 0x00FF;          /* low byte, 8-bit const  */
    uint16_t NNN =  opcode & 0x0FFF;          /* low 12 bits, address   */

    /* DEFAULT: move to the next instruction. Jumps/calls/skips
       overwrite this below. One default, then a switch. */
    c->pc += 2;

    switch (n1) {

    /* ============ 0x0Y ============================================= */
    case 0x0:
        if (opcode == 0x00E0) {
            /* CLS: clear the display */
            memset(c->gfx, 0, sizeof(c->gfx));
            c->draw_flag = 1;
        } else if (opcode == 0x00EE) {
            /* RET: pop the return address. */
            if (c->sp == 0)
                return -2;              /* stack underflow */
            c->sp--;
            c->pc = c->stack[c->sp];
        } else {
            /* Only 00E0 and 00EE exist with n1=0. 0x0000 is NOT real. */
            return -1;
        }
        break;

    /* ============ 1NNN  JMP ========================================= */
    case 0x1:
        c->pc = NNN;
        break;

    /* ============ 2NNN  CALL ======================================== */
    case 0x2:
        if (c->sp >= STACK_SIZE)
            return -2;                  /* stack overflow */
        c->stack[c->sp] = c->pc;        /* push return address         */
        c->sp++;
        c->pc = NNN;
        break;

    /* ============ 3XNN  SE VX, byte ================================= */
    case 0x3:
        if (c->V[x] == NN)
            c->pc += 2;                 /* skip next instruction */
        break;

    /* ============ 4XNN  SNE VX, byte ================================ */
    case 0x4:
        if (c->V[x] != NN)
            c->pc += 2;
        break;

    /* ============ 5XY0  SE VX, VY =================================== */
    case 0x5:
        if (c->V[x] == c->V[y])
            c->pc += 2;
        break;

    /* ============ 6XNN  LD VX, byte ================================= */
    case 0x6:
        c->V[x] = NN;
        break;

    /* ============ 7XNN  ADD VX, byte ================================ */
    case 0x7:
        c->V[x] = (uint8_t)(c->V[x] + NN);   /* wraps, no carry flag */
        break;

    /* ============ 8XY?  arithmetic between registers ================= */
    case 0x8:
        switch (n4) {
        case 0x0:  c->V[x] = c->V[y];                             break; /* LD   */
        case 0x1:  c->V[x] |= c->V[y];                            break; /* OR   */
        case 0x2:  c->V[x] &= c->V[y];                            break; /* AND  */
        case 0x3:  c->V[x] ^= c->V[y];                            break; /* XOR  */
        case 0x4:                                                  /* ADD  */
            {
                /* 8-bit addition. Sum needs 9 bits; the top bit is the
                   carry flag written to VF. */
                uint16_t sum = c->V[x] + c->V[y];
                c->V[0xF] = (sum > 0xFF) ? 1 : 0;
                c->V[x] = (uint8_t)sum;
            }
            break;
        case 0x5:                                                  /* SUB  */
            /* VF = NOT borrow: 1 if Vx >= Vy, else 0. */
            c->V[0xF] = (c->V[x] >= c->V[y]) ? 1 : 0;
            c->V[x] = (uint8_t)(c->V[x] - c->V[y]);
            break;
        case 0x6:                                                  /* SHR  */
            /* Shift right. VF = the bit that falls off the end. */
            c->V[0xF] = c->V[x] & 0x1;
            c->V[x] >>= 1;
            break;
        case 0x7:                                                  /* SUBN */
            /* Vx = Vy - Vx. VF = NOT borrow. */
            c->V[0xF] = (c->V[y] >= c->V[x]) ? 1 : 0;
            c->V[x] = (uint8_t)(c->V[y] - c->V[x]);
            break;
        case 0xE:                                                  /* SHL  */
            /* Shift left. VF = the bit that falls off the end. */
            c->V[0xF] = (c->V[x] >> 7) & 0x1;
            c->V[x] <<= 1;
            break;
        default:
            return -1;
        }
        break;

    /* ============ 9XY0  SNE VX, VY ================================== */
    case 0x9:
        if (c->V[x] != c->V[y])
            c->pc += 2;
        break;

    /* ============ ANNN  LD I, addr ================================== */
    case 0xA:
        c->I = NNN;
        break;

    /* ============ BNNN  JP V0 + addr ================================ */
    case 0xB:
        c->pc = (uint16_t)(NNN + c->V[0]);
        break;

    /* ============ CXNN  RND VX, byte ================================ */
    case 0xC:
        c->V[x] = (uint8_t)(rand() & 0xFF) & NN;
        break;

    /* ============ DXYN  DRW VX, VY, n =============================== */
    case 0xD:
        {
            uint8_t vx = c->V[x];
            uint8_t vy = c->V[y];
            uint8_t rows = n4;   /* sprite is `n4` bytes tall, 8 px wide */

            c->V[0xF] = 0;       /* reset collision flag before drawing */

            for (uint8_t row = 0; row < rows; row++) {
                uint8_t sprite_byte = c->memory[c->I + row];

                for (uint8_t col = 0; col < 8; col++) {
                    /* Check one bit at a time, most significant first. */
                    if ((sprite_byte & (0x80 >> col)) != 0) {
                        /* Coordinates WRAP around the 64x32 screen. */
                        int px = (vx + col) % GFX_W;
                        int py = (vy + row) % GFX_H;
                        int idx = py * GFX_W + px;

                        /* XOR drawing: if we turn a pixel OFF, it
                           collided -> set VF. */
                        if (c->gfx[idx] == 1)
                            c->V[0xF] = 1;
                        c->gfx[idx] ^= 1;
                    }
                }
            }
            c->draw_flag = 1;
        }
        break;

    /* ============ EX9E / EXA1  keyboard skip ======================== */
    case 0xE:
        if (NN == 0x9E) {              /* SKP: skip if key VX pressed */
            if (c->keypad[c->V[x]] != 0)
                c->pc += 2;
        } else if (NN == 0xA1) {       /* SKNP: skip if key VX NOT pressed */
            if (c->keypad[c->V[x]] == 0)
                c->pc += 2;
        } else {
            return -1;
        }
        break;

    /* ============ FX??  misc ======================================== */
    case 0xF:
        switch (NN) {
        case 0x07:  c->V[x] = c->delay_timer;                         break; /* LD Vx, DT */
        case 0x0A:                                                  /* LD Vx, K (wait) */
            {
                int pressed = -1;
                for (int k = 0; k < KEY_COUNT; k++)
                    if (c->keypad[k]) { pressed = k; break; }
                if (pressed < 0) {
                    /* No key yet: rewind so we re-run this instruction
                       until a key arrives (blocking wait). */
                    c->pc -= 2;
                } else {
                    c->V[x] = (uint8_t)pressed;
                }
            }
            break;
        case 0x15:  c->delay_timer = c->V[x];                         break; /* LD DT, Vx */
        case 0x18:  c->sound_timer = c->V[x];                         break; /* LD ST, Vx */
        case 0x1E:  c->I = (uint16_t)(c->I + c->V[x]);                break; /* ADD I, Vx */
        case 0x29:  c->I = (uint16_t)(FONT_ADDR + c->V[x] * 5);       break; /* LD F, Vx */
        case 0x33:                                                  /* LD B, Vx (BCD) */
            /* Split VX into hundreds/tens/units and store them as
               three decimal digits at [I], [I+1], [I+2]. */
            c->memory[c->I]     = c->V[x] / 100;
            c->memory[c->I + 1] = (c->V[x] / 10) % 10;
            c->memory[c->I + 2] = c->V[x] % 10;
            break;
        case 0x55:                                                  /* LD [I], Vx */
            /* Store V0..Vx into RAM starting at I. */
            for (int i = 0; i <= x; i++)
                c->memory[c->I + i] = c->V[i];
            break;
        case 0x65:                                                  /* LD Vx, [I] */
            /* Load V0..Vx from RAM starting at I. */
            for (int i = 0; i <= x; i++)
                c->V[i] = c->memory[c->I + i];
            break;
        default:
            return -1;
        }
        break;

    default:
        c->pc -= 2;   /* rewind: unknown opcode, PC stays where it was */
        return -1;
    }

    return 0;
}

void chip8_tick_timers(struct chip8 *c)
{
    /* Timers count down 60 times per second. The frontend calls this
       on a fixed clock; instructions run much faster than that. */
    if (c->delay_timer > 0)
        c->delay_timer--;
    if (c->sound_timer > 0)
        c->sound_timer--;
}

void chip8_dump(const struct chip8 *c, uint16_t start, uint16_t len)
{
    /* Classic hex-dump format:
         0200  6A 02 68 0F  ...   j...        address + bytes */
    for (uint16_t i = 0; i < len; i++) {
        uint16_t addr = (uint16_t)(start + i);

        if (i % 16 == 0)                       /* new row: print address */
            printf("\n%04X  ", addr);

        printf("%02X ", c->memory[addr]);
    }
    printf("\n");
}

void chip8_print_registers(const struct chip8 *c)
{
    for (int i = 0; i < 16; i++) {
        if (i % 4 == 0)
            printf("\n  ");
        printf("V%X=%02X  ", i, c->V[i]);
    }
    printf("\n  I=0x%03X  PC=0x%03X  SP=%u  DT=%u  ST=%u\n",
           c->I, c->pc, c->sp, c->delay_timer, c->sound_timer);
}

void chip8_dump_gfx(const struct chip8 *c)
{
    /* Headless "screen": print the framebuffer as ASCII art. */
    for (int py = 0; py < GFX_H; py++) {
        for (int px = 0; px < GFX_W; px++)
            putchar(c->gfx[py * GFX_W + px] ? '#' : '.');
        putchar('\n');
    }
}
