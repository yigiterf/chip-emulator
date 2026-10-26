# Makefile - tells `make` how to build the project.
# Rule format:
#   target: dependencies
#   <TAB>command

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g     # -g: debug info for gdb/valgrind
SRC     = $(wildcard src/*.c)            # all .c files in src/
OBJ     = $(SRC:src/%.c=build/%.o)       # src/main.c -> build/main.o
TARGET  = chip8

# Link step: combine all object files into one executable
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compile step: each .c -> .o  ($< = first dependency, $@ = target)
build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build $(TARGET)

.PHONY: clean
