# Linux / FreeBSD (gmake). No Windows use build.bat.
CC       ?= gcc
CFLAGS   ?= -std=c99 -O2 -Wall -Wextra -pthread
SRC      := platform_thread.c
HDR      := platform_thread.h
TEST_SRC := ../testes/test_platform.c

all: benchmark test_platform

benchmark: benchmark.c $(SRC) $(HDR)
	$(CC) $(CFLAGS) benchmark.c $(SRC) -o benchmark

test_platform: $(TEST_SRC) $(SRC) $(HDR)
	$(CC) $(CFLAGS) -I. $(TEST_SRC) $(SRC) -o test_platform

test: test_platform
	./test_platform

clean:
	rm -f benchmark test_platform

.PHONY: all test_platform test clean