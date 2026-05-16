/* Qmini signaling server - Linux version with NAT traversal
   Compile: gcc -O2 signaling_server_linux.c -o signaling_server -lpthread
   Run: ./signaling_server [port]

   NAT traversal: server listens on same TCP+UDP port.
   After REGISTER, client sends "HELLO <id>" via UDP to the server.
   Server observes the public IP:port from recvfrom() and uses that
   for PEER_JOIN instead of the local port reported by client.
   Falls back to TCP-derived address after 3s if no HELLO arrives.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#define MAX_CLIENTS 64
#define MAX_NAME 32
#define MAX_ID 32
#define BUF_SIZE 4096

typedef struct {
    int     sock;
    char    id[MAX_ID];
    char    nickname[MAX_NAME];
    char    room[MAX_NAME];
    struct sockaddr_in addr;        /* TCP peer address */
    int     local_port;             /* UDP port from REGISTER */
    struct sockaddr_in public_addr; /* UDP-observed public address (NAT-mapped) */
    int     public_ready;           /* 1 when public_addr is valid */
    int     active;
} client_t;

static client_t g_clients[MAX_CLIENTS];
static int g_next_id = 1;
static pthread_mutex_t g_lock;
static int g_udp_sock = -1;
static FILE *g_log = NULL;
static int g_listen_port = 9088;
static volatile sig_atomic_t g_running = 1;

/* Simple rate limiter: track last connection time per IP */
#define RATE_MAX 16
static struct { in_addr_t ip; time_t last_connect; int count; } g_rate[RATE_MAX];

static int rate_check(in_addr_t ip) {
    time_t now = time(NULL);
    for (int i = 0; i < RATE_MAX; i++) {
        if (g_rate[i].ip == ip) {
            if (now - g_rate[i].last_connect < 5) {
                g_rate[i].count++;
                if (g_rate[i].count > 3) return 0; /* reject */
            } else {
                g_rate[i].last_connect = now;
                g_rate[i].count = 1;
            }
            return 1;
        }
    }
    /* New IP, add to table */
    for (int i = 0; i < RATE_MAX; i++) {
        if (g_rate[i].ip == 0) {
            g_rate[i].ip = ip;
            g_rate[i].last_connect = now;
            g_rate[i].count = 1;
            return 1;
        }
    }
    return 1; /* table full, allow */
}

#define LOG(fmt, ...) do { \
    time_t _t = time(NULL); \
    char _tb[32]; \
    strftime(_tb, sizeof(_tb), "%Y-%m-%d %H:%M:%S", localtime(&_t)); \
    if (g_log) { fprintf(g_log, "[%s] " fmt "\n", _tb, ##__VA_ARGS__); fflush(g_log); } \
    printf("[%s] " fmt "\n", _tb, ##__VA_ARGS__); \
} while(0)

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static client_t* find_client(int s) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active && g_clients[i].sock == s)
            return &g_clients[i];
    return NULL;
}

static client_t* find_client_by_id(const char *id) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i].active && strcmp(g_clients[i].id, id) == 0)
            return &g_clients[i];
    return NULL;
}

static int v_send(int s, const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    buf[n] = '\n';
    return send(s, buf, n + 1, MSG_NOSIGNAL) > 0 ? 1 : 0;
}

static void broadcast_room(const char *room, int exclude, const char *fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    buf[n] = '\n';

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active && g_clients[i].sock != exclude &&
            strcmp(g_clients[i].room, room) == 0) {
            send(g_clients[i].sock, buf, n + 1, MSG_NOSIGNAL);
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* Called after public address is known: broadcast PEER_JOIN and sync existing peers */
static void announce_peer(client_t *c) {
    const char *ip_str = inet_ntoa(c->public_addr.sin_addr);
    int port = ntohs(c->public_addr.sin_port);

    LOG("ANNOUNCE: id=%s nick=%s public=%s:%d room=%s",
        c->id, c->nickname, ip_str, port, c->room);

    /* Notify existing members about new peer.
       For same-NAT peers, use LAN address instead of public address. */
    pthread_mutex_lock(&g_lock);
    for (int j = 0; j < MAX_CLIENTS; j++) {
        if (g_clients[j].active && g_clients[j].sock != c->sock &&
            strcmp(g_clients[j].room, c->room) == 0) {
            const char *notify_ip;
            int notify_port;
            if (g_clients[j].public_ready &&
                g_clients[j].public_addr.sin_addr.s_addr == c->public_addr.sin_addr.s_addr) {
                notify_ip = inet_ntoa(c->addr.sin_addr);
                notify_port = c->local_port;
                LOG("SAME-NAT: telling %s about %s via LAN %s:%d",
                    g_clients[j].id, c->id, notify_ip, notify_port);
            } else {
                notify_ip = ip_str;
                notify_port = port;
            }
            char msg[256];
            int n = snprintf(msg, sizeof(msg), "PEER_JOIN %s %s %s %d\n",
                             c->id, c->nickname, notify_ip, notify_port);
            send(g_clients[j].sock, msg, n, MSG_NOSIGNAL);
        }
    }
    pthread_mutex_unlock(&g_lock);

    /* Tell new peer about existing members */
    pthread_mutex_lock(&g_lock);
    for (int j = 0; j < MAX_CLIENTS; j++) {
        if (g_clients[j].active && g_clients[j].sock != c->sock &&
            strcmp(g_clients[j].room, c->room) == 0) {
            const char *peer_ip;
            int peer_port;
            /* If both clients share the same public IP (same NAT),
               use TCP-derived LAN address to avoid NAT hairpin issues */
            if (g_clients[j].public_ready &&
                g_clients[j].public_addr.sin_addr.s_addr == c->public_addr.sin_addr.s_addr) {
                peer_ip = inet_ntoa(g_clients[j].addr.sin_addr);
                peer_port = g_clients[j].local_port;
                LOG("SAME-NAT: using LAN addr %s:%d for %s", peer_ip, peer_port, g_clients[j].id);
            } else if (g_clients[j].public_ready) {
                peer_ip = inet_ntoa(g_clients[j].public_addr.sin_addr);
                peer_port = ntohs(g_clients[j].public_addr.sin_port);
            } else {
                peer_ip = inet_ntoa(g_clients[j].addr.sin_addr);
                peer_port = g_clients[j].local_port;
            }
            v_send(c->sock, "PEER_JOIN %s %s %s %d",
                   g_clients[j].id, g_clients[j].nickname, peer_ip, peer_port);
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* Fallback: use TCP-derived address if UDP HELLO never arrives */
static void announce_peer_fallback(client_t *c) {
    if (c->public_ready) return;
    c->public_addr = c->addr;
    c->public_addr.sin_port = htons((short)c->local_port);
    c->public_ready = 1;
    announce_peer(c);
}

/* UDP listener thread for NAT address discovery */
static void* udp_thread(void* arg) {
    (void)arg;
    char buf[256];
    struct sockaddr_in from;
    socklen_t from_len;

    while (g_running) {
        from_len = sizeof(from);
        int n = recvfrom(g_udp_sock, buf, sizeof(buf) - 1, 0,
                         (struct sockaddr*)&from, &from_len);
        if (n <= 0) continue;
        buf[n] = 0;

        /* Expect "HELLO <id>" */
        if (n < 7 || strncmp(buf, "HELLO ", 6) != 0) continue;

        char *id = buf + 6;
        char *nl = strchr(id, '\n');
        if (nl) *nl = 0;
        nl = strchr(id, '\r');
        if (nl) *nl = 0;

        pthread_mutex_lock(&g_lock);
        client_t *c = find_client_by_id(id);
        if (c && c->active && !c->public_ready) {
            c->public_addr = from;
            c->public_ready = 1;
            LOG("UDP HELLO: id=%s public=%s:%d",
                id, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
            pthread_mutex_unlock(&g_lock);
            announce_peer(c);
        } else {
            pthread_mutex_unlock(&g_lock);
        }
    }
    return NULL;
}

static void remove_client(int s) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active && g_clients[i].sock == s) {
            char room[MAX_NAME], id[MAX_ID];
            strncpy(room, g_clients[i].room, sizeof(room));
            strncpy(id, g_clients[i].id, sizeof(id));
            LOG("Client disconnected: id=%s room=%s nick=%s",
                g_clients[i].id, g_clients[i].room, g_clients[i].nickname);
            g_clients[i].active = 0;
            pthread_mutex_unlock(&g_lock);

            broadcast_room(room, s, "PEER_LEAVE %s", id);
            close(s);
            return;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

static void* client_thread(void* arg) {
    int s = (int)(intptr_t)arg;
    char buf[BUF_SIZE];
    int pos = 0;

    int ka = 1, ka_idle = 60, ka_intvl = 10, ka_cnt = 3;
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof(ka));
    setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &ka_idle, sizeof(ka_idle));
    setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &ka_intvl, sizeof(ka_intvl));
    setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, &ka_cnt, sizeof(ka_cnt));

    /* Timeout: disconnect if client doesn't send REGISTER within 10s */
    struct timeval tv = {10, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

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
                char room[MAX_NAME] = {0}, nick[MAX_NAME] = {0};
                int local_port = 0;
                sscanf(line + 9, "%31s %31s %d", room, nick, &local_port);

                client_t *c = find_client(s);
                if (!c) continue;

                /* Remove stale duplicate entries (reconnect scenario) */
                pthread_mutex_lock(&g_lock);
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (g_clients[j].active && g_clients[j].sock != s &&
                        strcmp(g_clients[j].nickname, nick) == 0 &&
                        strcmp(g_clients[j].room, room) == 0) {
                        LOG("Removing stale duplicate: id=%s nick=%s room=%s",
                            g_clients[j].id, nick, room);
                        broadcast_room(room, g_clients[j].sock, "PEER_LEAVE %s", g_clients[j].id);
                        close(g_clients[j].sock);
                        g_clients[j].active = 0;
                        g_clients[j].public_ready = 0;
                    }
                }
                pthread_mutex_unlock(&g_lock);

                pthread_mutex_lock(&g_lock);
                char id[MAX_ID];
                snprintf(id, sizeof(id), "user%d", g_next_id++);
                pthread_mutex_unlock(&g_lock);

                strncpy(c->room, room, sizeof(c->room) - 1);
                strncpy(c->nickname, nick, sizeof(c->nickname) - 1);
                strncpy(c->id, id, sizeof(c->id) - 1);
                c->local_port = local_port;
                c->public_ready = 0;

                v_send(s, "OK %s", id);
                LOG("REGISTER: room=%s nick=%s port=%d -> id=%s (waiting for UDP HELLO)",
                    room, nick, local_port, id);

                /* Remove recv timeout — keep connection alive after registration.
                   Use a 1-hour timeout as "effectively infinite" since {0,0} may not
                   work reliably on all Linux kernels. */
                struct timeval no_tv = {3600, 0};
                setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &no_tv, sizeof(no_tv));

                /* Wait for UDP HELLO to learn public address, with 3-second fallback.
                   The udp_thread will call announce_peer() when HELLO arrives. */
                for (int wait_ms = 0; wait_ms < 3000; wait_ms += 50) {
                    usleep(50000);
                    if (c->public_ready) break;
                    if (!c->active) break;
                }
                if (c->active && !c->public_ready) {
                    LOG("UDP HELLO timeout for %s, using TCP-derived address", id);
                    announce_peer_fallback(c);
                }

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

                pthread_mutex_lock(&g_lock);
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (g_clients[j].active && g_clients[j].sock != s &&
                        strcmp(g_clients[j].id, target) == 0) {
                        v_send(g_clients[j].sock, "ICE %s %s", c->id, payload);
                        break;
                    }
                }
                pthread_mutex_unlock(&g_lock);

            } else if (strncmp(line, "RELAY ", 6) == 0) {
                client_t *c = find_client(s);
                if (!c) continue;

                char target[MAX_ID];
                const char *payload = line + 6;
                char *space = strchr(payload, ' ');
                if (space) {
                    *space = 0;
                    strncpy(target, payload, sizeof(target) - 1);
                    payload = space + 1;
                } else continue;

                pthread_mutex_lock(&g_lock);
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (g_clients[j].active && g_clients[j].sock != s &&
                        strcmp(g_clients[j].id, target) == 0) {
                        v_send(g_clients[j].sock, "RELAY %s %s", c->id, payload);
                        break;
                    }
                }
                pthread_mutex_unlock(&g_lock);

            } else if (strcmp(line, "UNREGISTER") == 0) {
                remove_client(s);
                return NULL;
            }

            line = nl + 1;
        }

        int remaining = (int)(buf + pos - line);
        if (remaining > 0 && line != buf) memmove(buf, line, remaining);
        pos = remaining;
        buf[pos] = 0;
    }

    remove_client(s);
    return NULL;
}

int main(int argc, char *argv[]) {
    int listen_sock;
    struct sockaddr_in addr;

    if (argc > 1) g_listen_port = atoi(argv[1]);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_lock, &attr);
    pthread_mutexattr_destroy(&attr);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* TCP socket */
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_listen_port);

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind TCP");
        return 1;
    }

    /* UDP socket for NAT address discovery (same port) */
    g_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_udp_sock < 0) { perror("socket UDP"); close(listen_sock); return 1; }
    setsockopt(g_udp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(g_udp_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind UDP");
        close(g_udp_sock);
        close(listen_sock);
        return 1;
    }

    listen(listen_sock, SOMAXCONN);

    g_log = fopen("server.log", "a");
    LOG("=== Qmini signaling server started (NAT-aware) ===");
    LOG("Listen port: %d (TCP + UDP)", g_listen_port);
    LOG("PID: %d", getpid());

    /* Start UDP listener thread for NAT address discovery */
    pthread_t udp_tid;
    pthread_create(&udp_tid, NULL, udp_thread, NULL);
    pthread_detach(udp_tid);

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int s = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (s < 0) {
            if (!g_running) break;
            continue;
        }

        /* Rate limit: reject IPs connecting too fast (likely scanner) */
        if (!rate_check(client_addr.sin_addr.s_addr)) {
            LOG("Rate limited: %s", inet_ntoa(client_addr.sin_addr));
            close(s);
            continue;
        }

        LOG("New connection from %s:%d",
            inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        int nd = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));

        pthread_mutex_lock(&g_lock);
        int assigned = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!g_clients[i].active) {
                g_clients[i].sock = s;
                g_clients[i].active = 1;
                g_clients[i].id[0] = 0;
                g_clients[i].room[0] = 0;
                g_clients[i].nickname[0] = 0;
                g_clients[i].addr = client_addr;
                g_clients[i].public_ready = 0;
                assigned = 1;
                pthread_mutex_unlock(&g_lock);

                pthread_t tid;
                pthread_create(&tid, NULL, client_thread, (void*)(intptr_t)s);
                pthread_detach(tid);
                break;
            }
        }
        if (!assigned) {
            pthread_mutex_unlock(&g_lock);
            LOG("Max clients reached, rejecting");
            close(s);
        }
    }

    LOG("Shutting down...");
    if (g_log) fclose(g_log);
    close(listen_sock);
    close(g_udp_sock);
    pthread_mutex_destroy(&g_lock);
    return 0;
}
