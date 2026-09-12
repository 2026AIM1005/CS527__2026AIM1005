CC=gcc
CFLAGS=-Wall -Wextra -std=c11
TARGET=executable.exe
SRC=main.c compiler.c processor.c memory.c os.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	cmd /C "del /Q $(TARGET) 2>NUL"
	cmd /C "del /Q *.log 2>NUL"
