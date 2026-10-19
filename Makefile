CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic
TARGET = xeno.exe
SOURCES = main.c xenor.c

all: $(TARGET)

$(TARGET): $(SOURCES) xenor.h
	$(CC) $(CFLAGS) $(SOURCES) -o $(TARGET)

clean:
	$(RM) $(TARGET) *.generated.c

.PHONY: all clean