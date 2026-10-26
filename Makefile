# Makefile - tells `make` how to build the project.
# Rule format:
#   target: dependencies
#   <TAB>command

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g     # -g: debug info for gdb/valgrind

# SDL2 is found via pkg-config (no hard-coded paths, no guessing).
SDL_CFLAGS = $(shell pkg-config --cflags sdl2)
SDL_LIBS   = $(shell pkg-config --libs sdl2)

SRC     = $(wildcard src/*.c)            # all .c files in src/
OBJ     = $(SRC:src/%.c=build/%.o)       # src/main.c -> build/main.o
TARGET  = chip8

# Link step: combine all object files into one executable.
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(SDL_LIBS)

# Compile step: each .c -> .o  ($< = first dependency, $@ = target)
build/%.o: src/%.c | build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build $(TARGET)

.PHONY: clean
