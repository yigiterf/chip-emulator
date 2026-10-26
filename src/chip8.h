/*
 * chip8.h - The hardware model of the CHIP-8 machine.
 *
 * This header only *declares* things (shapes and names).
 * The actual code lives in chip8.c. Splitting declarations (what)
 * from implementation (how) is the classic C project layout:
 *   - .h files are read by OTHER files that want to use the chip8
 *   - .c files contain the real logic
 */

#ifndef CHIP8_H      /* include guard: if this file is #included twice, */
#define CHIP8_H      /* the second include is skipped (otherwise the     */
                     /* compiler sees the same struct defined twice).    */

#include <stdint.h>  /* uint8_t = unsigned 8-bit, uint16_t = unsigned 16-bit */

/* Memory layout constants.Giving them NAMES avoids "magic numbers" in code. */
#define MEM_SIZE        4096    /* 4 KB of RAM, addresses 0x000 - 0xFFF      */
#define PROG_START      0x200   /* ROMs always load here (historical reason) */
#define FONT_ADDR       0x050   /* we store the font sprites here            */
#define STACK_SIZE      16      /* stack holds up to 16 return addresses     */
#define FONT_SIZE       80      /* 16 characters x 5 bytes each              */

/*
 * The whole CHIP-8 machine in one struct.
 * An instance of this struct IS the emulated computer.
 */
struct chip8 {
    /* --- RAM --------------------------------------------------------- */
    uint8_t memory[MEM_SIZE];

    /* --- CPU registers ----------------------------------------------- */
    uint8_t  V[16];     /* 16 general purpose registers, V0 .. VF        */
                        /* VF is special: used as a flag by some opcodes */
    uint16_t I;         /* memory address register (12 bits really)      */
    uint16_t pc;        /* program counter: address of next instruction  */
    uint16_t stack[STACK_SIZE];  /* return addresses for CALL/RET        */
    uint8_t  sp;        /* stack pointer: how many entries are used      */

    /* --- timers (decrement 60x per second, later) --------------------- */
    uint8_t delay_timer;
    uint8_t sound_timer;

    /* --- bookkeeping (ours, not part of real CHIP-8) ------------------ */
    uint16_t prog_size; /* how many bytes the loaded ROM occupies        */
};

/*
 * Reset the machine: clear memory, load the font sprites, set PC to 0x200.
 */
void chip8_init(struct chip8 *c);

/*
 * Load a ROM file into memory at 0x200.
 * Returns 0 on success, or a negative value on failure (file missing/too big).
 */
int chip8_load_rom(struct chip8 *c, const char *path);

/*
 * Read the 2-byte instruction at address `pc` and return it as one
 * 16-bit number. CHIP-8 is BIG-ENDIAN: the high byte is stored first.
 */
uint16_t chip8_fetch(const struct chip8 *c, uint16_t pc);

/*
 * Print `len` bytes of memory starting at address `start` as hex.
 * start and len are uint16_t so we can dump any region up to 0xFFFF.
 */
void chip8_dump(const struct chip8 *c, uint16_t start, uint16_t len);

#endif /* CHIP8_H */
