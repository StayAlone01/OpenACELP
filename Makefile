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

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
