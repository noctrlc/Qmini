/* Qmini signaling server - Linux version
   Compile: gcc -O2 signaling_server_linux.c -o signaling_server -lpthread
   Run: ./signaling_server [port]
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
    struct sockaddr_in addr;
    int     active;
} client_t;

static client_t g_clients[MAX_CLIENTS];
static int g_next_id = 1;
static pthread_mutex_t g_lock;
static FILE *g_log = NULL;
static int g_listen_port = 9088;
static volatile sig_atomic_t g_running = 1;

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

static void remove_client(int s) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].active && g_clients[i].sock == s) {
            LOG("Client disconnected: id=%s room=%s nick=%s",
                g_clients[i].id, g_clients[i].room, g_clients[i].nickname);
            g_clients[i].active = 0;
            pthread_mutex_unlock(&g_lock);

            broadcast_room(g_clients[i].room, s, "PEER_LEAVE %s", g_clients[i].id);
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

                pthread_mutex_lock(&g_lock);
                char id[MAX_ID];
                snprintf(id, sizeof(id), "user%d", g_next_id++);
                pthread_mutex_unlock(&g_lock);

                strncpy(c->room, room, sizeof(c->room) - 1);
                strncpy(c->nickname, nick, sizeof(c->nickname) - 1);
                strncpy(c->id, id, sizeof(c->id) - 1);

                v_send(s, "OK %s", id);
                LOG("REGISTER: room=%s nick=%s port=%d -> id=%s", room, nick, local_port, id);

                struct sockaddr_in actual;
                socklen_t actual_len = sizeof(actual);
                getpeername(s, (struct sockaddr*)&actual, &actual_len);
                const char *ip_str = inet_ntoa(actual.sin_addr);

                broadcast_room(room, s, "PEER_JOIN %s %s %s %d", id, nick, ip_str, local_port);

                pthread_mutex_lock(&g_lock);
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (g_clients[j].active && g_clients[j].sock != s &&
                        strcmp(g_clients[j].room, room) == 0) {
                        v_send(s, "PEER_JOIN %s %s %s %d",
                               g_clients[j].id, g_clients[j].nickname,
                               inet_ntoa(g_clients[j].addr.sin_addr),
                               ntohs(g_clients[j].addr.sin_port));
                    }
                }
                pthread_mutex_unlock(&g_lock);

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

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_listen_port);

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    listen(listen_sock, SOMAXCONN);

    g_log = fopen("server.log", "a");
    LOG("=== Qmini signaling server started ===");
    LOG("Listen port: %d", g_listen_port);
    LOG("PID: %d", getpid());

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int s = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (s < 0) {
            if (!g_running) break;
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
                g_clients[i].addr = client_addr;
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
    pthread_mutex_destroy(&g_lock);
    return 0;
}
