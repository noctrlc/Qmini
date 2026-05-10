#include "signaling.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

static void log_msg(const char *msg) {
    FILE *f = fopen("D:\\Qmini\\sig_log.txt", "a");
    if (f) {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        fprintf(f, "[%02d:%02d:%02d] signaling: %s\n",
                t->tm_hour, t->tm_min, t->tm_sec, msg);
        fclose(f);
    }
}

static int recv_line(SOCKET s, char *buf, int bufsz) {
    int i = 0;
    while (i < bufsz - 1) {
        char c;
        int n = recv(s, &c, 1, 0);
        if (n <= 0) {
            log_msg("recv_line: connection closed or error");
            return 0;
        }
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = 0;
    return i;
}

static DWORD WINAPI signaling_recv_thread(LPVOID arg) {
    signaling_t *sig = (signaling_t*)arg;
    char buf[1024];

    log_msg("recv thread started");

    while (sig->running) {
        int n = recv_line(sig->sock, buf, sizeof(buf));
        if (n <= 0) {
            log_msg("recv thread: connection lost, exiting");
            break;
        }

        log_msg(buf);  // Log all received messages

        if (strncmp(buf, "PEER_JOIN ", 10) == 0) {
            char pid[32], nick[32], ip[64];
            int port;
            if (sscanf(buf + 10, "%31s %31s %63s %d", pid, nick, ip, &port) >= 4) {
                char log_buf[128];
                _snprintf(log_buf, sizeof(log_buf), "peer joined: %s (%s)", nick, pid);
                log_msg(log_buf);
                if (sig->peer_join_cb) {
                    struct sockaddr_in addr;
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = inet_addr(ip);
                    addr.sin_port = htons((short)port);
                    sig->peer_join_cb(pid, nick, &addr, sig->user_data);
                }
            }
        } else if (strncmp(buf, "PEER_LEAVE ", 11) == 0) {
            char log_buf[128];
            _snprintf(log_buf, sizeof(log_buf), "peer left: %s", buf + 11);
            log_msg(log_buf);
            if (sig->peer_leave_cb)
                sig->peer_leave_cb(buf + 11, sig->user_data);
        } else if (strncmp(buf, "ICE ", 4) == 0) {
            log_msg("ICE message received");
            char from_id[32] = {0};
            const char *payload = buf + 4;
            char *space = strchr(payload, ' ');
            if (space) {
                *space = 0;
                strncpy(from_id, payload, sizeof(from_id) - 1);
                if (sig->ice_cb)
                    sig->ice_cb(from_id, space + 1, sig->user_data);
            }
        }
    }
    log_msg("recv thread exited");
    return 0;
}

int signaling_connect(signaling_t *sig, const char *host, int port, const char *room,
                      const char *nickname, int local_port) {
    char log_buf[256];

    _snprintf(log_buf, sizeof(log_buf), "connecting to %s:%d, room=%s, nick=%s, port=%d",
              host, port, room, nickname, local_port);
    log_msg(log_buf);

    /* Save callbacks before clearing - they are set before connect */
    {
        void (*saved_join)(const char*, const char*, struct sockaddr_in*, void*) = sig->peer_join_cb;
        void (*saved_leave)(const char*, void*) = sig->peer_leave_cb;
        void (*saved_ice)(const char*, const char*, void*) = sig->ice_cb;
        void *saved_user = sig->user_data;
        memset(sig, 0, sizeof(*sig));
        sig->peer_join_cb = saved_join;
        sig->peer_leave_cb = saved_leave;
        sig->ice_cb = saved_ice;
        sig->user_data = saved_user;
    }
    sig->running = 1;
    strncpy(sig->server_host, host, sizeof(sig->server_host) - 1);
    sig->server_port = port;
    strncpy(sig->room, room, sizeof(sig->room) - 1);
    strncpy(sig->nickname, nickname, sizeof(sig->nickname) - 1);
    sig->local_port = local_port;

    sig->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sig->sock == INVALID_SOCKET) {
        log_msg("socket creation failed");
        return 0;
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(host);
    server.sin_port = htons((short)port);

    /* non-blocking connect with 5-second timeout */
    {
        u_long mode = 1;
        ioctlsocket(sig->sock, FIONBIO, &mode);
        int ret = connect(sig->sock, (struct sockaddr*)&server, sizeof(server));
        if (ret == SOCKET_ERROR) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
                _snprintf(log_buf, sizeof(log_buf), "connect failed: %d", WSAGetLastError());
                log_msg(log_buf);
                closesocket(sig->sock);
                sig->sock = INVALID_SOCKET;
                return 0;
            }
            fd_set wfds;
            struct timeval tv = {5, 0};
            FD_ZERO(&wfds);
            FD_SET(sig->sock, &wfds);
            ret = select(0, NULL, &wfds, NULL, &tv);
            if (ret <= 0) {
                log_msg("connect timed out (5s)");
                closesocket(sig->sock);
                sig->sock = INVALID_SOCKET;
                return 0;
            }
        }
        mode = 0;
        ioctlsocket(sig->sock, FIONBIO, &mode);
    }

    log_msg("connected, sending REGISTER");

    char cmd[256];
    _snprintf(cmd, sizeof(cmd), "REGISTER %s %s %d 0 0\n", room, nickname, local_port);
    send(sig->sock, cmd, (int)strlen(cmd), 0);

    char resp[64];
    if (!recv_line(sig->sock, resp, sizeof(resp))) {
        log_msg("no response from server");
        closesocket(sig->sock);
        sig->sock = INVALID_SOCKET;
        return 0;
    }

    if (strncmp(resp, "OK ", 3) != 0) {
        _snprintf(log_buf, sizeof(log_buf), "register failed: %s", resp);
        log_msg(log_buf);
        closesocket(sig->sock);
        sig->sock = INVALID_SOCKET;
        return 0;
    }

    strncpy(sig->local_id, resp + 3, sizeof(sig->local_id) - 1);
    sig->local_id[sizeof(sig->local_id) - 1] = 0;

    _snprintf(log_buf, sizeof(log_buf), "registered with id=%s", sig->local_id);
    log_msg(log_buf);

    sig->thread = CreateThread(NULL, 0, signaling_recv_thread, sig, 0, NULL);
    return 1;
}

int signaling_is_connected(signaling_t *sig) {
    return sig->sock != INVALID_SOCKET && sig->running;
}

void signaling_disconnect(signaling_t *sig) {
    log_msg("disconnecting");
    sig->running = 0;

    /* Close socket first so recv() in thread unblocks immediately */
    if (sig->sock != INVALID_SOCKET) {
        const char *cmd = "UNREGISTER\n";
        send(sig->sock, cmd, (int)strlen(cmd), 0);
        closesocket(sig->sock);
        sig->sock = INVALID_SOCKET;
        log_msg("socket closed");
    }

    if (sig->thread) {
        log_msg("waiting for recv thread");
        WaitForSingleObject(sig->thread, 1000);
        CloseHandle(sig->thread);
        sig->thread = NULL;
        log_msg("recv thread cleaned up");
    }
}

int signaling_send_ice(signaling_t *sig, const char *target_id, const char *sdp) {
    char cmd[1024];
    _snprintf(cmd, sizeof(cmd), "ICE %s %s\n", target_id, sdp ? sdp : "");
    return send(sig->sock, cmd, (int)strlen(cmd), 0) > 0;
}
