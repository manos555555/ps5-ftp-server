# 📡 PS5 High-Speed FTP Server

**By Manos**

A high-performance FTP server for PS5 with etaHEN, optimized for maximum transfer speed and user experience.

## ✨ Features

### 🚀 Performance
- **4MB Buffer Size** - Optimized for maximum throughput (67-70 Mbps sustained)
- **Zero-Copy Transfers** - sendfile() optimization for downloads
- **TCP Optimizations** - TCP_NOPUSH, SO_NOSIGPIPE for optimal performance
- **High-speed transfers** - Fast file uploads and downloads

### 📡 FTP Protocol
- **Full FTP support** - LIST, RETR, STOR, DELE, CWD, PWD, MKD, RMD, RNFR, RNTO
- **Extended commands** - SIZE, MDTM, FEAT, OPTS UTF8
- **Passive Mode (PASV)** - Full support for passive transfers
- **Binary transfers** - TYPE I for all file types
- **Resume support** - REST command for resuming transfers

### 🎮 PS5 Integration
- **Real-time Progress Notifications** - Upload/Download progress every 500MB
- **Completion Notifications** - Success notifications for files > 1MB
- **Start Notifications** - Download start notification for files > 1MB
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

### Standard FTP Commands
- **LIST** - List directory contents
- **RETR** - Download file (with sendfile optimization)
- **STOR** - Upload file (with progress tracking)
- **DELE** - Delete file
- **CWD** - Change directory
- **PWD** - Print working directory
- **MKD** - Create directory
- **RMD** - Remove directory
- **RNFR/RNTO** - Rename file/directory
- **REST** - Resume transfer
- **PASV** - Passive mode
- **TYPE** - Set transfer type (Binary/ASCII)

### Extended Commands (NEW!)
- **SIZE** - Get file size before download
- **MDTM** - Get file modification time
- **FEAT** - List all supported features
- **OPTS UTF8** - Enable UTF-8 encoding

## 🔧 Technical Details

### Backend
- **Language**: C
- **SDK**: PS5 Payload SDK v0.35
- **Port**: 2121 (configurable)
- **Buffer Size**: 4MB for optimal speed
- **Transfer Mode**: Binary (TYPE I)
- **Passive Mode**: Full PASV support
- **Multi-threaded**: Yes (pthread)

### Performance Optimizations
- **Zero-Copy Transfers**: sendfile() for downloads (FreeBSD)
- **Large socket buffers**: 4MB (4,194,304 bytes)
- **TCP optimizations**: TCP_NOPUSH, TCP_NODELAY, SO_NOSIGPIPE
- **SO_REUSEADDR**: Quick server restarts
- **Efficient file I/O**: Optimized read/write loops
- **Binary transfer mode**: Default for all files

### Configuration

To change the port, edit `main.c`:
```c
#define FTP_PORT 2121  // Change this
```

To change buffer size:
```c
#define BUFFER_SIZE (4 * 1024 * 1024)  // 4MB default
```

Then recompile.

## 📊 Performance

- **Transfer Speed**: 67-70 Mbps sustained (tested)
- **Buffer Size**: 4MB (4,194,304 bytes)
- **Concurrent Connections**: Sequential (one at a time)
- **File Size Limit**: None (handles files of any size)
- **Progress Tracking**: Real-time notifications every 500MB

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
