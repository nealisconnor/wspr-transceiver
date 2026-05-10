# WSPR Transmitter — Makefile
# ELEC3607 WSPR Transceiver Project
#
# Build:   make
# Clean:   make clean

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2
LDFLAGS = -lm
TARGET  = wspr_transmit
SRCS    = wspr_transmit.c wspr_encode.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)
