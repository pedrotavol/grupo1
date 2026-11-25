# Linux / FreeBSD (gmake). No Windows use build.bat.
CC       ?= gcc
CFLAGS   ?= -std=c99 -O2 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -pthread
SRC      := platform_thread.c
HDR      := platform_thread.h
TEST_SRC := ../testes/test_platform.c

all: benchmark test_platform

benchmark: benchmark.c $(SRC) $(HDR)
	$(CC) $(CFLAGS) -I. benchmark.c $(SRC) -o benchmark

test_platform: $(TEST_SRC) $(SRC) $(HDR)
	$(CC) $(CFLAGS) -I. $(TEST_SRC) $(SRC) -o test_platform

test: test_platform
	./test_platform

clean:
	rm -f benchmark test_platform

.PHONY: all benchmark test_platform test clean