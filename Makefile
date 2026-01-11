# PS5 Fast FTP Server - Makefile

PS5_HOST ?= 192.168.0.160
PS5_PORT ?= 9021
PS5_PAYLOAD_SDK := /home/hackman/ps5sdk_copy

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := ps5_ftp_server.elf
CFLAGS := -Wall -O3 -pthread

all: $(ELF)

$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(ELF)

.PHONY: all clean
