#!/bin/bash
# Compile PS5 Fast FTP Server

set -e

echo "================================================"
echo "PS5 Fast FTP Server - Compilation"
echo "================================================"
echo

SDK_PATH="/home/hackman/ps5sdk_copy"
BUILD_DIR="/home/hackman/ps5_ftp_server"

# Copy files to WSL
echo "[+] Copying files to WSL..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cp "/mnt/c/Users/HACKMAN/Desktop/ps5 test/ps5_rom_keys/ps5_ftp_server/main.c" "$BUILD_DIR/"
cp "/mnt/c/Users/HACKMAN/Desktop/ps5 test/ps5_rom_keys/ps5_ftp_server/Makefile" "$BUILD_DIR/"

cd "$BUILD_DIR"

echo "[+] Compiling..."
make clean
make

if [ -f ps5_ftp_server.elf ]; then
    echo
    echo "================================================"
    echo "[+] SUCCESS! Compiled ps5_ftp_server.elf"
    echo "================================================"
    echo
    ls -lh ps5_ftp_server.elf
    file ps5_ftp_server.elf
    echo
    echo "Copying back to Windows..."
    cp ps5_ftp_server.elf "/mnt/c/Users/HACKMAN/Desktop/ps5 test/ps5_rom_keys/ps5_ftp_server/"
    echo "[+] Done!"
    echo
    echo "Upload to PS5 and run with elfldr"
else
    echo "[!] Compilation failed!"
    exit 1
fi
