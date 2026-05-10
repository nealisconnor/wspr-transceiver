CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
TARGET = si5351_prog

all: $(TARGET)

$(TARGET): si5351_prog.c Si5351A-RevB-Registers.h
	$(CC) $(CFLAGS) -o $(TARGET) si5351_prog.c

clean:
	rm -f $(TARGET)
