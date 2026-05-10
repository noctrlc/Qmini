/* Qmini signaling server - standalone TCP server for room/peer management
   Protocol (text-based, \n terminated):
     REGISTER <room> <nickname> <local_port> <public_ip> <public_port>
     ICE <target_id> <payload>
     UNREGISTER
   Server responses:
     OK <peer_id>
     PEER_JOIN <peer_id> <nickname> <ip> <port>
     PEER_LEAVE <peer_id>
     ICE <from_id> <payload>
*/

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_CLIENTS 64
#define MAX_NAME 32
#define MAX_ID 32
#define BUF_SIZE 4096

typedef struct {
    SOCKET  sock;
    char    id[MAX_ID];
    char    nickname[MAX_NAME];
    char    room[MAX_NAME];
    struct sockaddr_in addr;   /* TCP peer address (IP correct, port is TCP ephemeral) */
    int     local_port;        /* UDP port from REGISTER, for P2P audio */
    int     active;
} client_t;

static client_t g_clients[MAX_CLIENTS];
static int g_next_id = 1;
static CRITICAL_SECTION g_lock;
static FILE *g_log = NULL;
#define LOG(fmt, ...) do { \
    if (g_log) { fprintf(g_log, fmt "\n", ##__VA_ARGS__); fflush(g_log); } \
    printf(fmt "\n", ##__VA_ARGS__); \
} while(0)

static client_t* find_client(SOCKET s) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active && g_clients[i].sock == s)
            return &g_clients[i];
    return NULL;
}

static int v_send(SOCKET s, const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    buf[n] = '\n';
    return send(s, buf, n + 1, 0) > 0 ? 1 : 0;
}

static void broadcast_room(const char *room, SOCKET exclude, const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[n] = '\n';

    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active && g_clients[i].sock != exclude &&
            strcmp(g_clients[i].room, room) == 0) {
            send(g_clients[i].sock, buf, n + 1, 0);
        }
    }
    LeaveCriticalSection(&g_lock);
}

static void remove_client(SOCKET s) {
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active && g_clients[i].sock == s) {
            LOG("Client disconnected: id=%s room=%s nick=%s", g_clients[i].id, g_clients[i].room, g_clients[i].nickname);
            g_clients[i].active = 0;
            broadcast_room(g_clients[i].room, s, "PEER_LEAVE %s", g_clients[i].id);
            closesocket(s);
            LeaveCriticalSection(&g_lock);
            return;
        }
    }
    LeaveCriticalSection(&g_lock);
}

static DWORD WINAPI client_thread(LPVOID arg) {
    SOCKET s = (SOCKET)(uintptr_t)arg;
    char buf[BUF_SIZE];
    int pos = 0;

    while (1) {
        int n = recv(s, buf + pos, (int)(sizeof(buf) - pos - 1), 0);
        if (n <= 0) break;
        pos += n;
        buf[pos] = 0;

        char *line = buf;
        while (1) {
            char *nl = strchr(line, '\n');
            if (!nl) break;
            *nl = 0;

            if (strncmp(line, "REGISTER ", 9) == 0) {
                char room[MAX_NAME], nick[MAX_NAME];
                int local_port = 0, pub_ip = 0, pub_port = 0;
                sscanf(line + 9, "%31s %31s %d %d %d", room, nick, &local_port, &pub_ip, &pub_port);

                client_t *c = find_client(s);
                if (!c) continue;

                char id[MAX_ID];
                _snprintf(id, sizeof(id), "user%d", g_next_id++);
                strncpy(c->room, room, sizeof(c->room) - 1);
                strncpy(c->nickname, nick, sizeof(c->nickname) - 1);
                strncpy(c->id, id, sizeof(c->id) - 1);
                c->local_port = local_port;  /* store UDP port for P2P */

                v_send(s, "OK %s", id);
                LOG("REGISTER: room=%s nick=%s port=%d -> id=%s", room, nick, local_port, id);

                struct sockaddr_in actual;
                int actual_len = sizeof(actual);
                getpeername(s, (struct sockaddr*)&actual, &actual_len);
                const char *ip_str = inet_ntoa(actual.sin_addr);

                /* Notify existing members about new peer */
                broadcast_room(room, s, "PEER_JOIN %s %s %s %d", id, nick, ip_str, local_port);

                /* Tell new peer about existing members */
                EnterCriticalSection(&g_lock);
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (g_clients[j].active && g_clients[j].sock != s &&
                        strcmp(g_clients[j].room, room) == 0) {
                        v_send(s, "PEER_JOIN %s %s %s %d",
                               g_clients[j].id, g_clients[j].nickname,
                               inet_ntoa(g_clients[j].addr.sin_addr),
                               g_clients[j].local_port);
                    }
                }
                LeaveCriticalSection(&g_lock);

            } else if (strncmp(line, "ICE ", 4) == 0) {
                client_t *c = find_client(s);
                if (!c) continue;

                char target[MAX_ID];
                const char *payload = line + 4;
                char *space = strchr(payload, ' ');
                if (space) {
                    *space = 0;
                    strncpy(target, payload, sizeof(target) - 1);
                    payload = space + 1;
                } else continue;

                EnterCriticalSection(&g_lock);
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (g_clients[j].active && g_clients[j].sock != s &&
                        strcmp(g_clients[j].id, target) == 0) {
                        v_send(g_clients[j].sock, "ICE %s %s", c->id, payload);
                        break;
                    }
                }
                LeaveCriticalSection(&g_lock);

            } else if (strcmp(line, "UNREGISTER") == 0) {
                remove_client(s);
                return 0;
            }

            line = nl + 1;
        }

        int remaining = (int)(buf + pos - line);
        if (remaining > 0 && line != buf) memmove(buf, line, remaining);
        pos = remaining;
        buf[pos] = 0;
    }

    remove_client(s);
    return 0;
}

int main() {
    WSADATA wsa;
    SOCKET listen_sock;
    struct sockaddr_in addr;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    InitializeCriticalSection(&g_lock);

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        printf("Failed to create socket\n");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(9088);

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Failed to bind port 9088 (try running as admin?)\n");
        closesocket(listen_sock);
        WSACleanup();
        return 1;
    }

    listen(listen_sock, SOMAXCONN);

    /* Open log file */
    g_log = fopen("D:\\Qmini\\signaling_server\\server.log", "w");

    LOG("Qmini signaling server started on:");
    LOG("  Listen port: 9088");
    {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            LOG("  Hostname: %s", hostname);
            struct addrinfo hints = {0}, *res;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            if (getaddrinfo(hostname, NULL, &hints, &res) == 0) {
                for (struct addrinfo *p = res; p; p = p->ai_next) {
                    struct sockaddr_in *sin = (struct sockaddr_in*)p->ai_addr;
                    LOG("  IP: %s", inet_ntoa(sin->sin_addr));
                }
                freeaddrinfo(res);
            }
        }
    }
    LOG("  Local: 127.0.0.1:9088");
    LOG("Press Ctrl+C to exit");

    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET s = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (s == INVALID_SOCKET) continue;

        LOG("New connection from %s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        EnterCriticalSection(&g_lock);
        int assigned = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!g_clients[i].active) {
                g_clients[i].sock = s;
                g_clients[i].active = 1;
                g_clients[i].id[0] = 0;
                g_clients[i].addr = client_addr;  /* store address for PEER_JOIN */
                assigned = 1;
                LeaveCriticalSection(&g_lock);
                HANDLE h = CreateThread(NULL, 0, client_thread, (void*)(uintptr_t)s, 0, NULL);
                CloseHandle(h);
                break;
            }
        }
        if (!assigned) {
            LeaveCriticalSection(&g_lock);
            printf("Max clients reached, rejecting\n");
            closesocket(s);
        }
    }

    DeleteCriticalSection(&g_lock);
    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
