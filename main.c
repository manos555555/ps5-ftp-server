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
#define BUFFER_SIZE (2 * 1024 * 1024)
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
    
    int sndbuf = BUFFER_SIZE;
    int rcvbuf = BUFFER_SIZE;
    setsockopt(session->data_sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    setsockopt(session->data_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
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
    unsigned short port = ntohs(addr.sin_port);
    
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
            
            if (stat(full_path, &st) == 0) {
                snprintf(buffer, sizeof(buffer),
                         "%crwxrwxrwx 1 root root %10ld Jan  1 00:00 %s\r\n",
                         S_ISDIR(st.st_mode) ? 'd' : '-',
                         (long)st.st_size,
                         entry->d_name);
                send(client_sock, buffer, strlen(buffer), 0);
            }
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
        send_response(session->control_sock, "550 File not found");
        return;
    }
    
    if (session->restart_offset > 0) {
        lseek(fd, session->restart_offset, SEEK_SET);
        session->restart_offset = 0;
    }
    
    send_response(session->control_sock, "150 Opening data connection");
    
    int client_sock = accept(session->data_sock, NULL, NULL);
    if (client_sock < 0) {
        send_response(session->control_sock, "425 Cannot open data connection");
        close(fd);
        return;
    }
    
    int sndbuf = BUFFER_SIZE * 2;
    int rcvbuf = BUFFER_SIZE * 2;
    int keepalive = 1;
    setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    int nopush = 1;
    setsockopt(client_sock, IPPROTO_TCP, TCP_NOPUSH, &nopush, sizeof(nopush));
    
    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        send_response(session->control_sock, "451 Memory allocation failed");
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
    
    int rcvbuf = BUFFER_SIZE;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
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
