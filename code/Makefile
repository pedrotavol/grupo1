# Linux / FreeBSD (gmake). No Windows use build.bat.
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -pthread
SRC     := platform_thread.c

all: benchmark

benchmark: benchmark.c $(SRC) platform_thread.h
	$(CC) $(CFLAGS) benchmark.c $(SRC) -o benchmark

clean:
	rm -f benchmark

.PHONY: all clean
