# 📡 PS5 High-Speed FTP Server

**By Manos**

A high-performance FTP server for PS5 with etaHEN, optimized for maximum transfer speed.

## ✨ Features

### 🚀 Performance
- **2MB Buffer Size** - Optimized for maximum throughput
- **High-speed transfers** - Fast file uploads and downloads
- **Efficient socket handling** - SO_REUSEADDR and optimized buffers

### 📡 FTP Protocol
- **Full FTP support** - LIST, RETR, STOR, DELE, CWD, PWD, MKD, RMD, RNFR, RNTO
- **Passive Mode (PASV)** - Full support for passive transfers
- **Binary transfers** - TYPE I for all file types
- **Resume support** - REST command for resuming transfers

### 🎮 PS5 Integration
- **PS5 Notifications** - Connection and transfer status
- **Custom Port** - Runs on port 2121 (configurable)
- **Full filesystem access** - Browse entire PS5 filesystem

## 🚀 How to Use

### 1. Compile
```bash
# On Windows with WSL
wsl -d Ubuntu-22.04 bash "/mnt/c/Users/HACKMAN/Desktop/ps5 test/ps5_rom_keys/ps5_ftp_server/compile.sh"
```

### 2. Upload to PS5
- Copy `ps5_ftp_server.elf` to `/data/etaHEN/payloads/`
- Use existing FTP, USB, or Web Manager

### 3. Run on PS5
- Load the payload with elfldr
- You'll see a notification: `FTP Server started on port 2121`

### 4. Connect with FTP Client

#### FileZilla:
- **Host**: `YOUR_PS5_IP`
- **Port**: `2121`
- **Username**: `anonymous`
- **Password**: (leave empty)

#### Command Line:
```bash
ftp YOUR_PS5_IP 2121
# Username: anonymous
# Password: (press Enter)
```

## 🎯 Supported Commands

- **LIST** - List directory contents
- **RETR** - Download file
- **STOR** - Upload file
- **DELE** - Delete file
- **CWD** - Change directory
- **PWD** - Print working directory
- **MKD** - Create directory
- **RMD** - Remove directory
- **RNFR/RNTO** - Rename file/directory
- **REST** - Resume transfer
- **PASV** - Passive mode
- **TYPE** - Set transfer type (Binary/ASCII)

## 🔧 Technical Details

### Backend
- **Language**: C
- **Port**: 2121 (configurable)
- **Buffer Size**: 2MB for optimal speed
- **Transfer Mode**: Binary (TYPE I)
- **Passive Mode**: Full PASV support
- **Multi-threaded**: Yes

### Performance Optimizations
- Large socket buffers (2MB)
- SO_REUSEADDR for quick restarts
- Efficient file I/O
- Binary transfer mode by default

### Configuration

To change the port, edit `main.c`:
```c
#define FTP_PORT 2121  // Change this
```

Then recompile.

## 📊 Performance

- **Transfer Speed**: Limited only by network bandwidth
- **Buffer Size**: 2MB (2,097,152 bytes)
- **Concurrent Connections**: Sequential (one at a time)
- **File Size Limit**: None (handles files of any size)

## 🛡️ Security Notes

- **Local network only** - Not exposed to internet
- **No authentication** - Anonymous login
- **Full filesystem access** - Be careful with delete operations
- **No encryption** - FTP is unencrypted

## 🐛 Troubleshooting

### Can't connect
- Check PS5 IP address in notification
- Make sure PS5 and device are on same network
- Verify port 2121 is not blocked

### Slow transfers
- Check network connection
- Use wired connection for best speed
- Disable firewall if needed

### Passive mode issues
- Enable passive mode in FTP client
- Check firewall settings

## 📝 License

Free to use and modify for the PS5 homebrew community!

## 👨‍💻 Author

**By Manos**

Created for the PS5 homebrew community with ❤️

## 🙏 Credits

- PS5 SDK
- etaHEN
- PS5 homebrew community

---

**Enjoy fast file transfers on your PS5!** 🚀
