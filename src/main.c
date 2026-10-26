/*
 * main.c - Entry point of the CHIP-8 emulator.
 *
 * The machine itself (chip8.c) is pure and has no idea about windows,
 * keyboards or audio. This file is the FRONTEND: it feeds the machine
 * instructions, updates its keypad, and draws its framebuffer.
 *
 * Ways to run:
 *   ./chip8 <rom>               -> SDL window (real emulator)
 *   ./chip8 --trace <rom>       -> disassembler (no execution)
 *   ./chip8 --run <rom>         -> execute headless, print registers
 *   ./chip8 --gfx <rom>         -> execute headless, print framebuffer
 */

#include <stdio.h>
#include <string.h>          /* strcmp: compare two strings for equality */

#include <SDL2/SDL.h>

#include "chip8.h"

#define MAX_CYCLES 1000   /* safety net for headless runs               */
#define SCALE      12     /* each CHIP-8 pixel -> 12x12 screen pixels   */
#define CYCLES_PER_FRAME 10   /* 10 instr/frame x 60 frames/s = 600 Hz  */

/* --------------------------------------------------------------------- */
/* Disassembler (decode only, for --trace)                               */
/* --------------------------------------------------------------------- */

static void print_mnemonic(uint16_t opcode)
{
    uint8_t n1   = (opcode & 0xF000) >> 12;
    uint8_t x    = (opcode & 0x0F00) >>  8;
    uint8_t y    = (opcode & 0x00F0) >>  4;
    uint8_t n4   =  opcode & 0x000F;
    uint8_t NN   =  opcode & 0x00FF;
    uint16_t NNN =  opcode & 0x0FFF;

    switch (n1) {
    case 0x0:
        if (opcode == 0x00E0)      printf("CLS");
        else if (opcode == 0x00EE) printf("RET");
        else                       printf("?");
        break;
    case 0x1: printf("JP   0x%03X", NNN); break;
    case 0x2: printf("CALL 0x%03X", NNN); break;
    case 0x3: printf("SE   V%X, 0x%02X", x, NN); break;
    case 0x4: printf("SNE  V%X, 0x%02X", x, NN); break;
    case 0x5: printf("SE   V%X, V%X", x, y); break;
    case 0x6: printf("LD   V%X, 0x%02X", x, NN); break;
    case 0x7: printf("ADD  V%X, 0x%02X", x, NN); break;
    case 0x8:
        switch (n4) {
        case 0x0: printf("LD   V%X, V%X", x, y); break;
        case 0x1: printf("OR   V%X, V%X", x, y); break;
        case 0x2: printf("AND  V%X, V%X", x, y); break;
        case 0x3: printf("XOR  V%X, V%X", x, y); break;
        case 0x4: printf("ADD  V%X, V%X", x, y); break;
        case 0x5: printf("SUB  V%X, V%X", x, y); break;
        case 0x6: printf("SHR  V%X", x); break;
        case 0x7: printf("SUBN V%X, V%X", x, y); break;
        case 0xE: printf("SHL  V%X", x); break;
        default:  printf("?"); break;
        }
        break;
    case 0x9: printf("SNE  V%X, V%X", x, y); break;
    case 0xA: printf("LD   I, 0x%03X", NNN); break;
    case 0xB: printf("JP   V0, 0x%03X", NNN); break;
    case 0xC: printf("RND  V%X, 0x%02X", x, NN); break;
    case 0xD: printf("DRW  V%X, V%X, %u", x, y, n4); break;
    case 0xE:
        if (NN == 0x9E)      printf("SKP  V%X", x);
        else if (NN == 0xA1) printf("SKNP V%X", x);
        else                 printf("?");
        break;
    case 0xF:
        switch (NN) {
        case 0x07: printf("LD   V%X, DT", x); break;
        case 0x0A: printf("LD   V%X, K", x); break;
        case 0x15: printf("LD   DT, V%X", x); break;
        case 0x18: printf("LD   ST, V%X", x); break;
        case 0x1E: printf("ADD  I, V%X", x); break;
        case 0x29: printf("LD   F, V%X", x); break;
        case 0x33: printf("LD   B, V%X", x); break;
        case 0x55: printf("LD   [I], V%X", x); break;
        case 0x65: printf("LD   V%X, [I]", x); break;
        default:   printf("?"); break;
        }
        break;
    default: printf("?"); break;
    }
}

static void trace(const struct chip8 *machine)
{
    uint16_t pc = PROG_START;

    printf("addr   opcode  decoded\n");
    printf("----   ------  ----------------\n");

    while (pc < (uint16_t)(PROG_START + machine->prog_size)) {
        uint16_t opcode = chip8_fetch(machine, pc);
        printf("%04X:  %04X    ", pc, opcode);
        print_mnemonic(opcode);
        printf("\n");
        pc += 2;
    }
}

/* --------------------------------------------------------------------- */
/* Headless execution (for --run and --gfx)                              */
/* --------------------------------------------------------------------- */

static void run_headless(struct chip8 *machine, int cycles)
{
    for (int i = 0; i < cycles; i++) {
        int err = chip8_step(machine);
        if (err == -1) {
            printf("stopped: unknown opcode at 0x%03X\n", machine->pc);
            return;
        }
        if (err == -2) {
            printf("stopped: stack overflow/underflow at 0x%03X\n", machine->pc);
            return;
        }
    }
}

/* --------------------------------------------------------------------- */
/* SDL frontend (default mode)                                           */
/* --------------------------------------------------------------------- */

/* The audio callback runs on a separate thread. It reads this flag,
   which the main loop updates whenever the sound timer is running. */
static int g_beep = 0;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    static double phase = 0.0;
    const double freq = 440.0;      /* a simple "beep" tone (A4)   */
    const int rate = 44100;
    Sint16 *buf = (Sint16 *)stream;
    int samples = len / (int)sizeof(Sint16);

    for (int i = 0; i < samples; i++) {
        if (g_beep) {
            /* Square wave: on half the period, off the other half. */
            phase += freq / (double)rate;
            if (phase >= 1.0) phase -= 1.0;
            buf[i] = (Sint16)(phase < 0.5 ? 6000 : -6000);
        } else {
            phase = 0.0;
            buf[i] = 0;
        }
    }
}

/*
 * Map a real keyboard key to a CHIP-8 keypad index (0x0-0xF).
 * Returns -1 for keys that are not part of the CHIP-8 keypad.
 *
 * CHIP-8 keypad layout     ->   keyboard:
 *   1 2 3 C                ->   1 2 3 4
 *   4 5 6 D                ->   Q W E R
 *   7 8 9 E                ->   A S D F
 *   A 0 B F                ->   Z X C V
 */
static int sdl_key_to_chip8(SDL_Keycode key)
{
    switch (key) {
    case SDLK_1: return 0x1;
    case SDLK_2: return 0x2;
    case SDLK_3: return 0x3;
    case SDLK_4: return 0xC;
    case SDLK_q: return 0x4;
    case SDLK_w: return 0x5;
    case SDLK_e: return 0x6;
    case SDLK_r: return 0xD;
    case SDLK_a: return 0x7;
    case SDLK_s: return 0x8;
    case SDLK_d: return 0x9;
    case SDLK_f: return 0xE;
    case SDLK_z: return 0xA;
    case SDLK_x: return 0x0;
    case SDLK_c: return 0xB;
    case SDLK_v: return 0xF;
    default:     return -1;
    }
}

static int run_sdl(struct chip8 *machine)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Window & renderer. RenderSetScale lets us draw 64x32 at 12x. */
    SDL_Window *win = SDL_CreateWindow("CHIP-8 Emulator",
                                       SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED,
                                       GFX_W * SCALE, GFX_H * SCALE, 0);
    if (win == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetScale(ren, (float)SCALE, (float)SCALE);

    /* A streaming texture holds our 64x32 image. */
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         GFX_W, GFX_H);

    /* Audio device: a square wave beeps while the sound timer runs. */
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 512;
    want.callback = audio_callback;

    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio != 0)
        SDL_PauseAudioDevice(audio, 0);   /* 0 = start playing */

    /* A fixed 60 Hz frame clock. */
    const Uint32 frame_ms = 1000 / 60;
    Uint32 next_frame = SDL_GetTicks() + frame_ms;

    int running = 1;
    int halted = 0;   /* becomes 1 if the program hits a bad opcode */

    while (running) {
        /* ---- handle input ------------------------------------------ */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                } else {
                    int k = sdl_key_to_chip8(ev.key.keysym.sym);
                    if (k >= 0) machine->keypad[k] = 1;
                }
            } else if (ev.type == SDL_KEYUP) {
                int k = sdl_key_to_chip8(ev.key.keysym.sym);
                if (k >= 0) machine->keypad[k] = 0;
            }
        }

        /* ---- execute several instructions per frame ----------------- */
        if (!halted) {
            for (int i = 0; i < CYCLES_PER_FRAME; i++) {
                int err = chip8_step(machine);
                if (err != 0) {
                    fprintf(stderr,
                            "emulator stopped: bad opcode / stack error at 0x%03X\n",
                            machine->pc);
                    halted = 1;
                    break;
                }
            }
            chip8_tick_timers(machine);
        }

        /* ---- render the framebuffer --------------------------------- */
        {
            Uint32 pixels[GFX_SIZE];
            for (int i = 0; i < GFX_SIZE; i++)
                pixels[i] = machine->gfx[i] ? 0xFFFFFFFF : 0xFF000000;

            SDL_UpdateTexture(tex, NULL, pixels, GFX_W * (int)sizeof(Uint32));
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);   /* renderer, texture */
            SDL_RenderPresent(ren);
        }

        /* ---- audio -------------------------------------------------- */
        g_beep = machine->sound_timer > 0;

        /* ---- keep a steady 60 fps ----------------------------------- */
        Uint32 now = SDL_GetTicks();
        if (next_frame > now)
            SDL_Delay(next_frame - now);
        next_frame += frame_ms;
    }

    if (audio != 0) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

/* --------------------------------------------------------------------- */
/* main                                                                  */
/* --------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *rom_path = NULL;
    int trace_mode = 0;
    int run_mode = 0;
    int gfx_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0)
            trace_mode = 1;
        else if (strcmp(argv[i], "--run") == 0)
            run_mode = 1;
        else if (strcmp(argv[i], "--gfx") == 0)
            gfx_mode = 1;
        else
            rom_path = argv[i];
    }

    if (rom_path == NULL) {
        fprintf(stderr,
                "Usage: %s [--trace|--run|--gfx] <rom-file>\n", argv[0]);
        return 1;
    }

    struct chip8 machine;
    chip8_init(&machine);

    if (chip8_load_rom(&machine, rom_path) != 0)
        return 2;

    if (trace_mode) {
        trace(&machine);
    } else if (run_mode) {
        run_headless(&machine, MAX_CYCLES);
        printf("\nregisters after run:\n");
        chip8_print_registers(&machine);
    } else if (gfx_mode) {
        /* 0 = run until it stops (unknown opcode) or caps at a lot.
           CHIP-8 test ROMs run a few thousand cycles before showing
           results; 2000 is enough for all of them. */
        run_headless(&machine, 2000);
        printf("framebuffer after run:\n");
        chip8_dump_gfx(&machine);
    } else {
        /* default: open the SDL window and run for real */
        return run_sdl(&machine);
    }

    return 0;
}
