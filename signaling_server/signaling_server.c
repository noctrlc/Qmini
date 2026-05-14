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
    struct sockaddr_in udp_addr; /* Observed UDP address for SFU relay */
    int     udp_known;         /* 1 = udp_addr has been populated from a received packet */
    int     local_port;        /* UDP port from REGISTER, for P2P audio */
    int     active;
} client_t;

static client_t g_clients[MAX_CLIENTS];
static int g_next_id = 1;
static CRITICAL_SECTION g_lock;
static FILE *g_log = NULL;
static SOCKET g_udp_sock = INVALID_SOCKET;  /* UDP relay socket for SFU */
#define SFU_RELAY_PORT 9089
#define SFU_MAX_PACKET 1500
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
            g_clients[i].udp_known = 0;
            broadcast_room(g_clients[i].room, s, "PEER_LEAVE %s", g_clients[i].id);
            closesocket(s);
            LeaveCriticalSection(&g_lock);
            return;
        }
    }
    LeaveCriticalSection(&g_lock);
}

/* Find a client by their observed UDP address (IP + port) */
static client_t* find_client_by_udp(const struct sockaddr_in *from) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active && g_clients[i].udp_known &&
            g_clients[i].udp_addr.sin_addr.s_addr == from->sin_addr.s_addr &&
            g_clients[i].udp_addr.sin_port == from->sin_port) {
            return &g_clients[i];
        }
    }
    return NULL;
}

/* Find a client by their ID string */
static client_t* find_client_by_id(const char *id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active && strcmp(g_clients[i].id, id) == 0)
            return &g_clients[i];
    }
    return NULL;
}

/* SFU relay thread: receives UDP audio packets from clients and forwards
   to all other clients in the same room.
   Packet format: 32-byte sender_id header + audio payload */
static DWORD WINAPI sfu_relay_thread(LPVOID arg) {
    (void)arg;
    uint8_t buf[SFU_MAX_PACKET];
    struct sockaddr_in from;
    int from_len;

    LOG("SFU relay thread started on UDP port %d", SFU_RELAY_PORT);

    while (1) {
        from_len = sizeof(from);
        int n = recvfrom(g_udp_sock, (char*)buf, SFU_MAX_PACKET, 0,
                         (struct sockaddr*)&from, &from_len);
        if (n <= 0) continue;

        /* Handle HELLO message for initial UDP address mapping */
        if (n >= 6 && memcmp(buf, "HELLO ", 6) == 0) {
            char hello_id[MAX_ID] = {0};
            int copy_len = n - 6;
            if (copy_len >= MAX_ID) copy_len = MAX_ID - 1;
            memcpy(hello_id, buf + 6, copy_len);
            hello_id[copy_len] = 0;

            EnterCriticalSection(&g_lock);
            client_t *c = find_client_by_id(hello_id);
            if (c) {
                c->udp_addr = from;
                c->udp_known = 1;
                LOG("SFU: HELLO from %s, UDP mapped to %s:%d",
                    hello_id, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
            }
            LeaveCriticalSection(&g_lock);
            continue;
        }

        if (n < 32) continue;  /* Need at least 32-byte ID header */

        /* Extract sender ID from packet header */
        char sender_id[MAX_ID] = {0};
        memcpy(sender_id, buf, 32);
        sender_id[MAX_ID - 1] = 0;
        /* Trim trailing zeros */
        for (int k = (int)strlen(sender_id); k < 32 && sender_id[k] != 0; k++)
            sender_id[k] = 0;

        EnterCriticalSection(&g_lock);

        /* Try to match by UDP address first (faster), then by ID */
        client_t *sender = find_client_by_udp(&from);
        if (!sender) {
            sender = find_client_by_id(sender_id);
            if (sender) {
                /* Cache the UDP address for future packets */
                sender->udp_addr = from;
                sender->udp_known = 1;
            }
        }

        if (!sender) {
            LeaveCriticalSection(&g_lock);
            continue;
        }

        /* Forward to all other active clients in the same room */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!g_clients[i].active || &g_clients[i] == sender)
                continue;
            if (strcmp(g_clients[i].room, sender->room) != 0)
                continue;
            if (!g_clients[i].udp_known)
                continue;  /* Skip clients whose UDP address is not yet known */

            sendto(g_udp_sock, (const char*)buf, n, 0,
                   (struct sockaddr*)&g_clients[i].udp_addr,
                   sizeof(g_clients[i].udp_addr));
        }

        LeaveCriticalSection(&g_lock);
    }
    return 0;
}

/* Initialize the UDP relay socket for SFU and start the relay thread */
static int init_udp_relay(uint16_t port) {
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_sock == INVALID_SOCKET) {
        LOG("Failed to create UDP relay socket");
        return 0;
    }

    int opt = 1;
    setsockopt(g_udp_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(g_udp_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG("Failed to bind UDP relay port %d", port);
        closesocket(g_udp_sock);
        g_udp_sock = INVALID_SOCKET;
        return 0;
    }

    HANDLE h = CreateThread(NULL, 0, sfu_relay_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);

    LOG("SFU relay initialized on UDP port %d", port);
    return 1;
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

    /* Initialize SFU UDP relay */
    if (init_udp_relay(SFU_RELAY_PORT)) {
        LOG("  SFU relay: UDP port %d", SFU_RELAY_PORT);
    } else {
        LOG("  WARNING: SFU relay failed to start");
    }

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
    if (g_udp_sock != INVALID_SOCKET) closesocket(g_udp_sock);
    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
