/*
 * main.c - Entry point of the CHIP-8 emulator.
 *
 * Stage 3: real execution. The machine can now run programs:
 * registers change, PC jumps, the stack works.
 *
 * Ways to run:
 *   ./chip8 <rom>                  -> machine state + memory dumps
 *   ./chip8 --trace <rom>          -> disassembler (no execution)
 *   ./chip8 --run <rom>            -> execute up to MAX_CYCLES instructions
 *   ./chip8 --run --trace <rom>    -> execute, printing each instruction
 */

#include <stdio.h>
#include <string.h>   /* strcmp: compare two strings for equality */

#include "chip8.h"

#define MAX_CYCLES 1000   /* safety net: stops even endless JMP loops */

/*
 * Translate one opcode into human-readable text ("mnemonic").
 * This is DECODE: pull the opcode apart with bit masks.
 *
 *  opcode = n1 n2 n3 n4   (each is 4 bits = one hex digit)
 *   6A02  =  6  A  0  2   ->  "SET VA, 0x02"
 */
static void print_mnemonic(uint16_t opcode)
{
    /* Masking: AND with 0xF keeps only the low 4 bits of a group,
       >> slides those bits to the right so they become a small number. */
    uint8_t n1 = (opcode & 0xF000) >> 12;   /* instruction type       */
    uint8_t n2 = (opcode & 0x0F00) >>  8;   /* usually a register #   */
    uint8_t NN =  opcode & 0x00FF;          /* low byte (8-bit const) */
    uint16_t NNN = opcode & 0x0FFF;         /* low 12 bits (address)  */

    switch (n1) {
    case 0x0:
        if (opcode == 0x00E0)
            printf("CLS             ; clear screen");
        else if (opcode == 0x00EE)
            printf("RET             ; return from subroutine");
        else
            printf("? (n1=0, but not CLS/RET)");
        break;

    case 0x1:
        printf("JMP  0x%03X          ; jump to address", NNN);
        break;

    case 0x2:
        printf("CALL 0x%03X          ; call subroutine", NNN);
        break;

    case 0x3:
        printf("SE   V%X, 0x%02X      ; skip next if V%X == %u", n2, NN, n2, NN);
        break;

    case 0x4:
        printf("SNE  V%X, 0x%02X      ; skip next if V%X != %u", n2, NN, n2, NN);
        break;

    case 0x6:
        printf("SET  V%X, 0x%02X      ; register V%X = %u", n2, NN, n2, NN);
        break;

    case 0x7:
        printf("ADD  V%X, 0x%02X      ; register V%X += %u", n2, NN, n2, NN);
        break;

    case 0xA:
        printf("LDI  0x%03X          ; I = 0x%03X", NNN, NNN);
        break;

    default:
        printf("? (unknown type n1=%X)", n1);
        break;
    }
}

/*
 * Trace mode: walk the program area linearly, fetch each 2-byte
 * instruction and print it with its decoded meaning.
 * NOTE: this is a disassembler, not an emulator - it follows addresses
 * in order and does NOT execute jumps or loops.
 */
static void trace(const struct chip8 *machine)
{
    uint16_t pc = PROG_START;

    printf("addr   opcode  decoded\n");
    printf("----   ------  -----------------------------\n");

    while (pc < (uint16_t)(PROG_START + machine->prog_size)) {
        uint16_t opcode = chip8_fetch(machine, pc);

        printf("%04X:  %04X    ", pc, opcode);
        print_mnemonic(opcode);
        printf("\n");

        pc += 2;   /* every instruction is exactly 2 bytes wide */
    }
}

/*
 * Run mode: actually EXECUTE instructions, optionally printing each
 * one as it is about to run. Stops on unknown opcodes, stack errors,
 * or when MAX_CYCLES is reached (real games never "end" - they loop).
 */
static void run(struct chip8 *machine, int trace)
{
    for (int cycle = 0; cycle < MAX_CYCLES; cycle++) {
        uint16_t pc = machine->pc;
        uint16_t opcode = chip8_fetch(machine, pc);

        if (trace) {
            /* Cap the output: a game in an endless loop would print
               thousands of lines. First 40 are plenty for debugging. */
            static int lines = 0;
            if (lines == 40)
                printf("... (trace output limited to first 40 lines)\n");
            if (lines < 40) {
                printf("%04X:  %04X    ", pc, opcode);
                print_mnemonic(opcode);
                printf("\n");
            }
            lines++;
        }

        int err = chip8_step(machine);
        if (err == -1) {
            printf("\nstopped: unknown opcode %04X at %04X\n", opcode, pc);
            return;
        }
        if (err == -2) {
            printf("\nstopped: stack overflow/underflow at %04X\n", pc);
            return;
        }
    }

    printf("(stopped after %d cycles - probably an endless loop, which is normal)\n",
           MAX_CYCLES);
}

int main(int argc, char *argv[])
{
    /* argv handling: pick the ROM path, notice the optional flags. */
    const char *rom_path = NULL;
    int trace_mode = 0;
    int run_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0)
            trace_mode = 1;
        else if (strcmp(argv[i], "--run") == 0)
            run_mode = 1;
        else
            rom_path = argv[i];
    }

    if (rom_path == NULL) {
        fprintf(stderr, "Usage: %s [--run] [--trace] <rom-file>\n", argv[0]);
        return 1;
    }

    /* One instance of `struct chip8` = one emulated machine. */
    struct chip8 machine;

    chip8_init(&machine);

    if (chip8_load_rom(&machine, rom_path) != 0)
        return 2;   /* distinct error code: ROM problem */

    printf("machine reset: pc=0x%03X, rom '%s' (%u bytes)\n",
           machine.pc, rom_path, machine.prog_size);

    if (run_mode) {
        run(&machine, trace_mode);
        printf("\nregisters after run:\n");
        chip8_print_registers(&machine);
    } else if (trace_mode) {
        trace(&machine);
    } else {
        printf("\n--- font area (0x050) ---\n");
        chip8_dump(&machine, FONT_ADDR, 32);

        printf("\n--- program area (0x200) ---\n");
        chip8_dump(&machine, PROG_START, 64);
    }

    return 0;
}
