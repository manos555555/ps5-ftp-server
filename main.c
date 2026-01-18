/* PS5 High-Speed FTP Server
 * Optimized for maximum transfer speed
 * Custom port support (default: 5050)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>
#include <time.h>
#include <ifaddrs.h>
#include <sys/uio.h>

#define FTP_PORT 2121
#define DATA_PORT_START 2122
#define BUFFER_SIZE (4 * 1024 * 1024)
#define MAX_PATH 1024

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

void send_notification(const char *msg) {
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.message, msg, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

typedef struct {
    int control_sock;
    int data_sock;
    int data_port;
    char current_dir[MAX_PATH];
    char rename_from[MAX_PATH];
    int passive_mode;
    off_t restart_offset;
    struct sockaddr_in data_addr;
} ftp_session_t;

typedef struct {
    int client_sock;
    struct sockaddr_in client_addr;
} client_info_t;

void send_response(int sock, const char *response) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "%s\r\n", response);
    send(sock, buffer, strlen(buffer), 0);
}

void send_error_response(int sock, int code, const char *msg) {
    char response[256];
    if (errno != 0) {
        snprintf(response, sizeof(response), "%d %s (Error: %s)", code, msg, strerror(errno));
    } else {
        snprintf(response, sizeof(response), "%d %s", code, msg);
    }
    send_response(sock, response);
}

void handle_user(ftp_session_t *session, const char *arg) {
    send_response(session->control_sock, "331 Password required");
}

void handle_pass(ftp_session_t *session, const char *arg) {
    send_response(session->control_sock, "230 User logged in");
}

void handle_syst(ftp_session_t *session) {
    send_response(session->control_sock, "215 UNIX Type: L8");
}

void handle_pwd(ftp_session_t *session) {
    char response[MAX_PATH + 32];
    snprintf(response, sizeof(response), "257 \"%s\"", session->current_dir);
    send_response(session->control_sock, response);
}

void handle_cwd(ftp_session_t *session, const char *path) {
    char new_path[MAX_PATH];
    
    if (path[0] == '/') {
        strncpy(new_path, path, MAX_PATH - 1);
    } else {
        snprintf(new_path, MAX_PATH, "%s/%s", session->current_dir, path);
    }
    
    struct stat st;
    if (stat(new_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        strncpy(session->current_dir, new_path, MAX_PATH - 1);
        send_response(session->control_sock, "250 Directory changed");
    } else {
        send_response(session->control_sock, "550 Directory not found");
    }
}

void handle_type(ftp_session_t *session, const char *type) {
    send_response(session->control_sock, "200 Type set to Binary");
}

void handle_pasv(ftp_session_t *session) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    if (session->data_sock > 0) {
        close(session->data_sock);
    }
    
    session->data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (session->data_sock < 0) {
        send_response(session->control_sock, "425 Cannot open data connection");
        return;
    }
    
    int opt = 1;
    setsockopt(session->data_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Prevent SIGPIPE on write to closed socket (BSD/PS5 specific)
    int no_sigpipe = 1;
    setsockopt(session->data_sock, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
    
    // 4MB buffers for maximum speed
    int sndbuf = BUFFER_SIZE;
    int rcvbuf = BUFFER_SIZE;
    setsockopt(session->data_sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    setsockopt(session->data_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    // Disable Nagle's algorithm for lower latency
    int nodelay = 1;
    setsockopt(session->data_sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(session->data_port);
    
    if (bind(session->data_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        send_response(session->control_sock, "425 Cannot bind data port");
        close(session->data_sock);
        session->data_sock = -1;
        return;
    }
    
    listen(session->data_sock, 1);
    
    getsockname(session->data_sock, (struct sockaddr*)&addr, &addr_len);
    
    getsockname(session->control_sock, (struct sockaddr*)&addr, &addr_len);
    unsigned char *ip = (unsigned char*)&addr.sin_addr.s_addr;
    
    char response[128];
    snprintf(response, sizeof(response), 
             "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
             ip[0], ip[1], ip[2], ip[3],
             session->data_port >> 8, session->data_port & 0xFF);
    
    send_response(session->control_sock, response);
    session->passive_mode = 1;
}

void handle_list(ftp_session_t *session, const char *path) {
    if (!session->passive_mode || session->data_sock < 0) {
        send_response(session->control_sock, "425 Use PASV first");
        return;
    }
    
    send_response(session->control_sock, "150 Opening data connection");
    
    int client_sock = accept(session->data_sock, NULL, NULL);
    if (client_sock < 0) {
        send_response(session->control_sock, "425 Cannot open data connection");
        return;
    }
    
    DIR *dir = opendir(session->current_dir);
    if (dir) {
        struct dirent *entry;
        char buffer[1024];
        
        while ((entry = readdir(dir)) != NULL) {
            struct stat st;
            char full_path[MAX_PATH];
            snprintf(full_path, MAX_PATH, "%s/%s", session->current_dir, entry->d_name);
            
            int is_dir = 0;
            long file_size = 0;
            
            if (stat(full_path, &st) == 0) {
                is_dir = S_ISDIR(st.st_mode);
                file_size = (long)st.st_size;
            } else {
                // stat() failed - use d_type as fallback
                if (entry->d_type == DT_DIR) {
                    is_dir = 1;
                } else if (entry->d_type == DT_UNKNOWN) {
                    // Try lstat as last resort
                    if (lstat(full_path, &st) == 0) {
                        is_dir = S_ISDIR(st.st_mode);
                        file_size = (long)st.st_size;
                    }
                }
                // If all fails, assume it's a file with size 0
            }
            
            snprintf(buffer, sizeof(buffer),
                     "%crwxrwxrwx 1 root root %10ld Jan  1 00:00 %s\r\n",
                     is_dir ? 'd' : '-',
                     file_size,
                     entry->d_name);
            send(client_sock, buffer, strlen(buffer), 0);
        }
        closedir(dir);
    }
    
    close(client_sock);
    send_response(session->control_sock, "226 Transfer complete");
}

void handle_retr(ftp_session_t *session, const char *filename) {
    if (!session->passive_mode || session->data_sock < 0) {
        send_response(session->control_sock, "425 Use PASV first");
        return;
    }
    
    char filepath[MAX_PATH];
    snprintf(filepath, MAX_PATH, "%s/%s", session->current_dir, filename);
    
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        send_error_response(session->control_sock, 550, "File not found");
        return;
    }
    
    struct stat st;
    if (fstat(fd, &st) < 0) {
        send_error_response(session->control_sock, 550, "Cannot stat file");
        close(fd);
        return;
    }
    
    off_t file_size = st.st_size;
    off_t offset = session->restart_offset;
    if (offset > 0) {
        session->restart_offset = 0;
    }
    
    // Send start notification for files > 1MB
    if (file_size > 1*1024*1024) {
        char notif[128];
        if (file_size > 1024*1024*1024) {
            snprintf(notif, sizeof(notif), "FTP: Starting %s (%.2f GB)", 
                    filename, (float)file_size / (1024*1024*1024));
        } else {
            snprintf(notif, sizeof(notif), "FTP: Starting %s (%.1f MB)", 
                    filename, (float)file_size / (1024*1024));
        }
        send_notification(notif);
    }
    
    send_response(session->control_sock, "150 Opening data connection");
    
    int client_sock = accept(session->data_sock, NULL, NULL);
    if (client_sock < 0) {
        send_error_response(session->control_sock, 425, "Cannot open data connection");
        close(fd);
        return;
    }
    
    // 4MB buffers for maximum speed
    int sndbuf = BUFFER_SIZE;
    int rcvbuf = BUFFER_SIZE;
    setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    // Prevent SIGPIPE
    int no_sigpipe = 1;
    setsockopt(client_sock, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
    
    // Use TCP_NOPUSH for better throughput
    int nopush = 1;
    setsockopt(client_sock, IPPROTO_TCP, TCP_NOPUSH, &nopush, sizeof(nopush));
    
    // Try zero-copy sendfile first (much faster!)
    off_t bytes_to_send = file_size - offset;
    off_t sent_total = 0;
    
    #ifdef __FreeBSD__
    // PS5 uses FreeBSD - use sendfile for zero-copy transfer with progress tracking
    off_t last_notif_bytes = 0;
    off_t current_offset = offset;
    
    while (sent_total < bytes_to_send) {
        off_t sbytes = 0;
        off_t chunk_size = bytes_to_send - sent_total;
        
        // Send in chunks for progress tracking
        int sf_result = sendfile(fd, client_sock, current_offset, chunk_size, NULL, &sbytes, 0);
        
        if (sbytes > 0) {
            sent_total += sbytes;
            current_offset += sbytes;
            
            // Progress notification every 500MB for large files
            if (file_size > 500*1024*1024 && sent_total - last_notif_bytes >= 500*1024*1024) {
                int percent = (int)((sent_total * 100) / bytes_to_send);
                char notif[128];
                snprintf(notif, sizeof(notif), "FTP: %s - %d%% (%.1f GB)", 
                        filename, percent, (float)sent_total / (1024*1024*1024));
                send_notification(notif);
                last_notif_bytes = sent_total;
            }
            // Progress notification every 10MB for medium files
            else if (file_size > 20*1024*1024 && sent_total - last_notif_bytes >= 10*1024*1024) {
                int percent = (int)((sent_total * 100) / bytes_to_send);
                char notif[128];
                snprintf(notif, sizeof(notif), "FTP: %s - %d%%", filename, percent);
                send_notification(notif);
                last_notif_bytes = sent_total;
            }
        }
        
        if (sf_result == 0) {
            // Transfer complete
            break;
        }
        
        if (sf_result < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;  // Retry
            }
            // Error - fall back to read/write
            break;
        }
    }
    
    // Check if sendfile completed successfully
    if (sent_total >= bytes_to_send) {
        // Success - send completion notification
        if (file_size > 1*1024*1024) {
            char notif[128];
            if (file_size > 1024*1024*1024) {
                snprintf(notif, sizeof(notif), "FTP: Downloaded %s (%.2f GB)", 
                        filename, (float)file_size / (1024*1024*1024));
            } else {
                snprintf(notif, sizeof(notif), "FTP: Downloaded %s (%.1f MB)", 
                        filename, (float)file_size / (1024*1024));
            }
            send_notification(notif);
        }
        
        nopush = 0;
        setsockopt(client_sock, IPPROTO_TCP, TCP_NOPUSH, &nopush, sizeof(nopush));
        close(fd);
        close(client_sock);
        send_response(session->control_sock, "226 Transfer complete");
        return;
    }
    #endif
    
    // Fallback to traditional read/write if sendfile not available or failed
    if (offset > 0) {
        lseek(fd, offset, SEEK_SET);
    }
    
    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        send_error_response(session->control_sock, 451, "Memory allocation failed");
        close(client_sock);
        close(fd);
        return;
    }
    
    ssize_t bytes_read;
    while ((bytes_read = read(fd, buffer, BUFFER_SIZE)) > 0) {
        ssize_t bytes_sent = 0;
        while (bytes_sent < bytes_read) {
            ssize_t sent = send(client_sock, buffer + bytes_sent, 
                               bytes_read - bytes_sent, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (sent == 0) break;
            bytes_sent += sent;
        }
        sent_total += bytes_sent;
        
        // Progress notification every 10MB
        if (file_size > 20*1024*1024 && sent_total - last_notif_bytes > 10*1024*1024) {
            int percent = (int)((sent_total * 100) / file_size);
            char notif[128];
            snprintf(notif, sizeof(notif), "FTP: %s - %d%%", filename, percent);
            send_notification(notif);
            last_notif_bytes = sent_total;
        }
    }
    
    nopush = 0;
    setsockopt(client_sock, IPPROTO_TCP, TCP_NOPUSH, &nopush, sizeof(nopush));
    
    free(buffer);
    close(fd);
    close(client_sock);
    
    send_response(session->control_sock, "226 Transfer complete");
}

void handle_stor(ftp_session_t *session, const char *filename) {
    if (!session->passive_mode || session->data_sock < 0) {
        send_response(session->control_sock, "425 Use PASV first");
        return;
    }
    
    char filepath[MAX_PATH];
    snprintf(filepath, MAX_PATH, "%s/%s", session->current_dir, filename);
    
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        send_response(session->control_sock, "550 Cannot create file");
        return;
    }
    
    send_response(session->control_sock, "150 Opening data connection");
    
    int client_sock = accept(session->data_sock, NULL, NULL);
    if (client_sock < 0) {
        send_response(session->control_sock, "425 Cannot open data connection");
        close(fd);
        return;
    }
    
    // 4MB receive buffer for maximum speed
    int rcvbuf = BUFFER_SIZE;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    // Prevent SIGPIPE
    int no_sigpipe = 1;
    setsockopt(client_sock, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
    
    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        send_response(session->control_sock, "451 Memory allocation failed");
        close(fd);
        close(client_sock);
        return;
    }
    
    if (session->restart_offset > 0) {
        lseek(fd, session->restart_offset, SEEK_SET);
        session->restart_offset = 0;
    }
    
    // Track upload progress
    off_t total_received = 0;
    off_t last_notif_bytes = 0;
    
    ssize_t n;
    while ((n = recv(client_sock, buffer, BUFFER_SIZE, 0)) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(fd, buffer + written, n - written);
            if (w < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (w == 0) break;
            written += w;
        }
        total_received += written;
        
        // Progress notification every 500MB for large uploads
        if (total_received - last_notif_bytes >= 500*1024*1024) {
            char notif[128];
            snprintf(notif, sizeof(notif), "FTP: Uploading %s (%.1f GB)", 
                    filename, (float)total_received / (1024*1024*1024));
            send_notification(notif);
            last_notif_bytes = total_received;
        }
    }
    
    // Send completion notification for uploads > 1MB
    if (total_received > 1*1024*1024) {
        char notif[128];
        if (total_received > 1024*1024*1024) {
            snprintf(notif, sizeof(notif), "FTP: Uploaded %s (%.2f GB)", 
                    filename, (float)total_received / (1024*1024*1024));
        } else {
            snprintf(notif, sizeof(notif), "FTP: Uploaded %s (%.1f MB)", 
                    filename, (float)total_received / (1024*1024));
        }
        send_notification(notif);
    }
    
    free(buffer);
    close(fd);
    close(client_sock);
    
    send_response(session->control_sock, "226 Transfer complete");
}

void handle_dele(ftp_session_t *session, const char *filename) {
    char filepath[MAX_PATH];
    snprintf(filepath, MAX_PATH, "%s/%s", session->current_dir, filename);
    
    struct stat st;
    if (stat(filepath, &st) != 0) {
        send_response(session->control_sock, "550 File not found");
        return;
    }
    
    if (S_ISDIR(st.st_mode)) {
        if (rmdir(filepath) == 0) {
            send_response(session->control_sock, "250 Directory deleted");
        } else {
            send_response(session->control_sock, "550 Directory not empty or delete failed");
        }
    } else {
        if (unlink(filepath) == 0) {
            send_response(session->control_sock, "250 File deleted");
        } else {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "550 Delete failed: %s", strerror(errno));
            send_response(session->control_sock, error_msg);
        }
    }
}

void* client_thread(void* arg) {
    client_info_t* client_info = (client_info_t*)arg;
    int client_sock = client_info->client_sock;
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_info->client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    
    free(client_info);
    
    ftp_session_t session;
    memset(&session, 0, sizeof(session));
    
    session.control_sock = client_sock;
    session.data_sock = -1;
    session.data_port = DATA_PORT_START + (client_sock % 100);
    strcpy(session.current_dir, "/");
    session.passive_mode = 0;
    session.restart_offset = 0;
    
    send_response(client_sock, "220 PS5 Fast FTP Server Ready");
    
    char buffer[1024];
    while (1) {
        ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;
        
        buffer[n] = '\0';
        
        char *end = strchr(buffer, '\r');
        if (end) *end = '\0';
        end = strchr(buffer, '\n');
        if (end) *end = '\0';
        
        char cmd[16] = {0};
        char arg[MAX_PATH] = {0};
        sscanf(buffer, "%15s %1023[^\r\n]", cmd, arg);
        
        for (int i = 0; cmd[i]; i++) {
            if (cmd[i] >= 'a' && cmd[i] <= 'z') {
                cmd[i] -= 32;
            }
        }
        
        if (strcmp(cmd, "USER") == 0) {
            handle_user(&session, arg);
        } else if (strcmp(cmd, "PASS") == 0) {
            handle_pass(&session, arg);
        } else if (strcmp(cmd, "SYST") == 0) {
            handle_syst(&session);
        } else if (strcmp(cmd, "PWD") == 0) {
            handle_pwd(&session);
        } else if (strcmp(cmd, "CWD") == 0) {
            handle_cwd(&session, arg);
        } else if (strcmp(cmd, "TYPE") == 0) {
            handle_type(&session, arg);
        } else if (strcmp(cmd, "PASV") == 0) {
            handle_pasv(&session);
        } else if (strcmp(cmd, "LIST") == 0) {
            handle_list(&session, arg);
        } else if (strcmp(cmd, "RETR") == 0) {
            handle_retr(&session, arg);
        } else if (strcmp(cmd, "STOR") == 0) {
            handle_stor(&session, arg);
        } else if (strcmp(cmd, "DELE") == 0) {
            handle_dele(&session, arg);
        } else if (strcmp(cmd, "REST") == 0) {
            session.restart_offset = atoll(arg);
            char response[64];
            snprintf(response, sizeof(response), "350 Restart position accepted (%lld)", (long long)session.restart_offset);
            send_response(client_sock, response);
        } else if (strcmp(cmd, "RMD") == 0 || strcmp(cmd, "XRMD") == 0) {
            char filepath[MAX_PATH];
            snprintf(filepath, MAX_PATH, "%s/%s", session.current_dir, arg);
            if (rmdir(filepath) == 0) {
                send_response(client_sock, "250 Directory removed");
            } else {
                send_response(client_sock, "550 Remove directory failed");
            }
        } else if (strcmp(cmd, "MKD") == 0 || strcmp(cmd, "XMKD") == 0) {
            char filepath[MAX_PATH];
            snprintf(filepath, MAX_PATH, "%s/%s", session.current_dir, arg);
            if (mkdir(filepath, 0755) == 0) {
                char response[MAX_PATH + 32];
                snprintf(response, sizeof(response), "257 \"%s\" created", filepath);
                send_response(client_sock, response);
            } else {
                send_response(client_sock, "550 Create directory failed");
            }
        } else if (strcmp(cmd, "QUIT") == 0) {
            send_response(client_sock, "221 Goodbye");
            break;
        } else if (strcmp(cmd, "SITE") == 0) {
            char subcmd[16] = {0};
            char subarg[MAX_PATH] = {0};
            sscanf(arg, "%15s %1023[^\r\n]", subcmd, subarg);
            
            for (int i = 0; subcmd[i]; i++) {
                if (subcmd[i] >= 'a' && subcmd[i] <= 'z') {
                    subcmd[i] -= 32;
                }
            }
            
            if (strcmp(subcmd, "CHMOD") == 0) {
                int mode;
                char filepath[MAX_PATH];
                if (sscanf(subarg, "%o %1023[^\r\n]", &mode, filepath) == 2) {
                    char fullpath[MAX_PATH];
                    snprintf(fullpath, MAX_PATH, "%s/%s", session.current_dir, filepath);
                    if (chmod(fullpath, mode) == 0) {
                        send_response(client_sock, "200 CHMOD successful");
                    } else {
                        send_response(client_sock, "550 CHMOD failed");
                    }
                } else {
                    send_response(client_sock, "501 Invalid CHMOD syntax");
                }
            } else {
                send_response(client_sock, "502 SITE command not implemented");
            }
        } else if (strcmp(cmd, "SIZE") == 0) {
            char filepath[MAX_PATH];
            snprintf(filepath, MAX_PATH, "%s/%s", session.current_dir, arg);
            struct stat st;
            if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
                char response[64];
                snprintf(response, sizeof(response), "213 %lld", (long long)st.st_size);
                send_response(client_sock, response);
            } else {
                send_error_response(client_sock, 550, "File not found or not a regular file");
            }
        } else if (strcmp(cmd, "MDTM") == 0) {
            char filepath[MAX_PATH];
            snprintf(filepath, MAX_PATH, "%s/%s", session.current_dir, arg);
            struct stat st;
            if (stat(filepath, &st) == 0) {
                struct tm *tm = gmtime(&st.st_mtime);
                char response[64];
                snprintf(response, sizeof(response), "213 %04d%02d%02d%02d%02d%02d",
                        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                        tm->tm_hour, tm->tm_min, tm->tm_sec);
                send_response(client_sock, response);
            } else {
                send_error_response(client_sock, 550, "File not found");
            }
        } else if (strcmp(cmd, "FEAT") == 0) {
            send_response(client_sock, "211-Features:");
            send_response(client_sock, " SIZE");
            send_response(client_sock, " MDTM");
            send_response(client_sock, " REST STREAM");
            send_response(client_sock, " PASV");
            send_response(client_sock, " UTF8");
            send_response(client_sock, "211 End");
        } else if (strcmp(cmd, "OPTS") == 0) {
            char subcmd[16] = {0};
            sscanf(arg, "%15s", subcmd);
            for (int i = 0; subcmd[i]; i++) {
                if (subcmd[i] >= 'a' && subcmd[i] <= 'z') subcmd[i] -= 32;
            }
            if (strcmp(subcmd, "UTF8") == 0) {
                send_response(client_sock, "200 UTF8 enabled");
            } else {
                send_response(client_sock, "501 Option not supported");
            }
        } else if (strcmp(cmd, "NOOP") == 0) {
            send_response(client_sock, "200 OK");
        } else {
            send_response(client_sock, "502 Command not implemented");
        }
    }
    
    if (session.data_sock > 0) {
        close(session.data_sock);
    }
    close(client_sock);
    
    return NULL;
}

int main() {
    int server_sock;
    struct sockaddr_in server_addr;
    
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Prevent SIGPIPE on server socket
    int no_sigpipe = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
    
    // 4MB buffers for control connection
    int buf_size = BUFFER_SIZE;
    setsockopt(server_sock, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    setsockopt(server_sock, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(FTP_PORT);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_sock);
        return 1;
    }
    
    if (listen(server_sock, 5) < 0) {
        close(server_sock);
        return 1;
    }
    
    // Get actual IP address from network interfaces
    char ip_str[INET_ADDRSTRLEN] = "0.0.0.0";
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) continue;
            if (ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                inet_ntop(AF_INET, &addr->sin_addr, ip_str, INET_ADDRSTRLEN);
                if (strcmp(ip_str, "127.0.0.1") != 0) {
                    break;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "FTP Server: %s:%d - By Manos", ip_str, FTP_PORT);
    send_notification(msg);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        if (client_sock < 0) {
            continue;
        }
        
        client_info_t* client_info = malloc(sizeof(client_info_t));
        if (!client_info) {
            close(client_sock);
            continue;
        }
        
        client_info->client_sock = client_sock;
        client_info->client_addr = client_addr;
        
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        
        if (pthread_create(&thread, &attr, client_thread, client_info) != 0) {
            free(client_info);
            close(client_sock);
        }
        
        pthread_attr_destroy(&attr);
    }
    
    close(server_sock);
    return 0;
}
