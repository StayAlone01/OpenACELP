# OpenACELP - Free ACELP vocoder
# Target: Linux (x86_64) and STM32 Cortex-M7 (ARM FPU)

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=c99
LDFLAGS := -lm

# Include paths
INCLUDES := -Iinclude -Isrc

# Source files
SRCDIR  := src
SRCS    := $(wildcard $(SRCDIR)/*.c)
OBJS    := $(SRCS:.c=.o)

# Output binary
TARGET  := openacelp

.PHONY: all clean test

all: $(TARGET)

# Bit-packing self-test (cl. 4.2.2.7)
test: tests/test_bits.c src/bits.c
	$(CC) $(CFLAGS) $(INCLUDES) -o tests/test_bits tests/test_bits.c src/bits.c $(LDFLAGS)
	./tests/test_bits

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
