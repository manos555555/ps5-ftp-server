# Changelog

## Version 2.0 (January 18, 2026)

### 🚀 Major Performance Improvements
- **4MB Buffer Size** (upgraded from 2MB) - 67-70 Mbps sustained transfer speed
- **Zero-Copy Transfers** - Implemented sendfile() optimization for downloads
- **TCP Optimizations** - Added TCP_NOPUSH, SO_NOSIGPIPE for better throughput

### ✨ New Features
- **Extended FTP Commands**:
  - SIZE - Get file size before download
  - MDTM - Get file modification time
  - FEAT - List all supported features
  - OPTS UTF8 - Enable UTF-8 encoding
  
- **Real-time Progress Notifications**:
  - Upload progress notifications every 500MB
  - Download progress notifications every 500MB
  - Start notification for downloads > 1MB
  - Completion notification for transfers > 1MB

### 🔧 Technical Improvements
- Better error handling with detailed errno messages
- Improved sendfile() implementation with progress tracking
- Optimized upload path with progress monitoring
- Reduced notification spam (500MB intervals instead of 100MB)

### 📊 Performance
- **Tested Speed**: 67-70 Mbps sustained
- **Buffer Size**: 4MB (4,194,304 bytes)
- **SDK**: PS5 Payload SDK v0.35
- **Compiler**: prospero-clang with -O3 optimization

---

## Version 1.0 (Initial Release)

### Features
- Basic FTP server functionality
- 2MB buffer size
- Standard FTP commands support
- PS5 notifications for connection status
- Port 2121 operation
