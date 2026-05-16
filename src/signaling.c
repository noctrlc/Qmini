#include "signaling.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

/* --- Base64 encode/decode --- */
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_encode(const uint8_t *in, int in_len, char *out, int out_size) {
    int o = 0;
    for (int i = 0; i < in_len; i += 3) {
        if (o + 4 >= out_size) break;
        uint32_t n = (uint32_t)in[i] << 16;
        if (i + 1 < in_len) n |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < in_len) n |= (uint32_t)in[i + 2];
        out[o++] = b64_table[(n >> 18) & 0x3F];
        out[o++] = b64_table[(n >> 12) & 0x3F];
        out[o++] = (i + 1 < in_len) ? b64_table[(n >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < in_len) ? b64_table[n & 0x3F] : '=';
    }
    out[o] = 0;
    return o;
}

static int base64_decode(const char *in, uint8_t *out, int out_size) {
    static uint8_t dtable[256];
    static int dtable_init = 0;
    if (!dtable_init) {
        memset(dtable, 0xFF, 256);
        for (int i = 0; i < 64; i++) dtable[(uint8_t)b64_table[i]] = (uint8_t)i;
        dtable_init = 1;
    }
    int o = 0, bits = 0, bit_count = 0;
    for (int i = 0; in[i]; i++) {
        uint8_t val = dtable[(uint8_t)in[i]];
        if (val == 0xFF) continue;
        bits = (bits << 6) | val;
        bit_count += 6;
        if (bit_count >= 8) {
            bit_count -= 8;
            if (o < out_size) out[o++] = (uint8_t)(bits >> bit_count);
        }
    }
    return o;
}

static int recv_line(SOCKET s, char *buf, int bufsz) {
    int i = 0;
    while (i < bufsz - 1) {
        char c;
        int n = recv(s, &c, 1, 0);
        if (n <= 0) {
            LOG_INFO("recv_line: connection closed or error");
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

    LOG_INFO("recv thread started");

    while (sig->running) {
        int n = recv_line(sig->sock, buf, sizeof(buf));
        if (n <= 0) {
            LOG_INFO("recv thread: connection lost, exiting");
            break;
        }

        __try {
        if (strncmp(buf, "PEER_JOIN ", 10) == 0) {
            char pid[32], nick[32], ip[64];
            int port;
            if (sscanf(buf + 10, "%31s %31s %63s %d", pid, nick, ip, &port) >= 4) {
                LOG_INFO("peer joined: %s (%s)", nick, pid);
                if (sig->peer_join_cb) {
                    struct sockaddr_in addr;
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = inet_addr(ip);
                    addr.sin_port = htons((short)port);
                    sig->peer_join_cb(pid, nick, &addr, sig->user_data);
                }
            }
        } else if (strncmp(buf, "PEER_LEAVE ", 11) == 0) {
            LOG_INFO("peer left: %s", buf + 11);
            if (sig->peer_leave_cb)
                sig->peer_leave_cb(buf + 11, sig->user_data);
        } else if (strncmp(buf, "ICE ", 4) == 0) {
            char from_id[32] = {0};
            const char *payload = buf + 4;
            char *space = strchr(payload, ' ');
            if (space) {
                *space = 0;
                strncpy(from_id, payload, sizeof(from_id) - 1);
                if (sig->ice_cb)
                    sig->ice_cb(from_id, space + 1, sig->user_data);
            }
        } else if (strncmp(buf, "RELAY ", 6) == 0) {
            char from_id[32] = {0};
            const char *payload = buf + 6;
            char *space = strchr(payload, ' ');
            if (space) {
                *space = 0;
                strncpy(from_id, payload, sizeof(from_id) - 1);
                uint8_t decoded[400];
                int dec_len = base64_decode(space + 1, decoded, sizeof(decoded));
                if (dec_len > 0 && sig->relay_cb)
                    sig->relay_cb(from_id, decoded, dec_len, sig->user_data);
            }
        }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            LOG_ERROR("CRASH in recv thread handler! code=0x%08X msg=%.60s",
                      GetExceptionCode(), buf);
        }
    }
    LOG_INFO("recv thread exited");
    return 0;
}

int signaling_connect(signaling_t *sig, const char *host, int port, const char *room,
                      const char *nickname, int local_port, SOCKET udp_sock) {
    LOG_INFO("connecting to %s:%d, room=%s, nick=%s, port=%d",
              host, port, room, nickname, local_port);

    /* Save callbacks and lock before clearing - they are set before connect */
    {
        void (*saved_join)(const char*, const char*, struct sockaddr_in*, void*) = sig->peer_join_cb;
        void (*saved_leave)(const char*, void*) = sig->peer_leave_cb;
        void (*saved_ice)(const char*, const char*, void*) = sig->ice_cb;
        void (*saved_relay)(const char*, const uint8_t*, int, void*) = sig->relay_cb;
        void *saved_user = sig->user_data;
        CRITICAL_SECTION saved_lock = sig->send_lock;
        memset(sig, 0, sizeof(*sig));
        sig->peer_join_cb = saved_join;
        sig->peer_leave_cb = saved_leave;
        sig->ice_cb = saved_ice;
        sig->relay_cb = saved_relay;
        sig->user_data = saved_user;
        sig->send_lock = saved_lock;
    }
    sig->running = 1;
    strncpy(sig->server_host, host, sizeof(sig->server_host) - 1);
    sig->server_port = port;
    strncpy(sig->room, room, sizeof(sig->room) - 1);
    strncpy(sig->nickname, nickname, sizeof(sig->nickname) - 1);
    sig->local_port = local_port;

    sig->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sig->sock == INVALID_SOCKET) {
        LOG_ERROR("socket creation failed");
        sig->last_error = SIG_ERR_SOCKET;
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
                LOG_ERROR("connect failed: %d", WSAGetLastError());
                closesocket(sig->sock);
                sig->sock = INVALID_SOCKET;
                sig->last_error = SIG_ERR_CONNECT;
                return 0;
            }
            fd_set wfds;
            struct timeval tv = {2, 0};
            FD_ZERO(&wfds);
            FD_SET(sig->sock, &wfds);
            ret = select(0, NULL, &wfds, NULL, &tv);
            if (ret <= 0) {
                LOG_ERROR("connect timed out (5s)");
                closesocket(sig->sock);
                sig->sock = INVALID_SOCKET;
                sig->last_error = SIG_ERR_TIMEOUT;
                return 0;
            }
        }
        mode = 0;
        ioctlsocket(sig->sock, FIONBIO, &mode);
    }

    LOG_INFO("connected, sending REGISTER");

    char cmd[256];
    _snprintf(cmd, sizeof(cmd), "REGISTER %s %s %d 0 0\n", room, nickname, local_port);
    send(sig->sock, cmd, (int)strlen(cmd), 0);

    char resp[64];
    if (!recv_line(sig->sock, resp, sizeof(resp))) {
        LOG_ERROR("no response from server");
        closesocket(sig->sock);
        sig->sock = INVALID_SOCKET;
        sig->last_error = SIG_ERR_NO_RESPONSE;
        return 0;
    }

    if (strncmp(resp, "OK ", 3) != 0) {
        LOG_ERROR("register failed: %s", resp);
        closesocket(sig->sock);
        sig->sock = INVALID_SOCKET;
        sig->last_error = SIG_ERR_REGISTER;
        return 0;
    }

    strncpy(sig->local_id, resp + 3, sizeof(sig->local_id) - 1);
    sig->local_id[sizeof(sig->local_id) - 1] = 0;

    LOG_INFO("registered with id=%s", sig->local_id);

    /* Send HELLO from the P2P UDP socket so NAT creates a mapping for this port.
       The server observes the NAT-mapped address via recvfrom(). */
    if (udp_sock != INVALID_SOCKET) {
        struct sockaddr_in srv;
        srv.sin_family = AF_INET;
        srv.sin_addr.s_addr = inet_addr(host);
        srv.sin_port = htons((short)port);
        char hello[64];
        int hlen = _snprintf(hello, sizeof(hello), "HELLO %s", sig->local_id);
        /* Send 3 times to handle packet loss */
        for (int i = 0; i < 3; i++) {
            sendto(udp_sock, hello, hlen, 0, (struct sockaddr*)&srv, sizeof(srv));
            Sleep(50);
        }
        LOG_INFO("sent UDP HELLO from P2P socket for NAT discovery");
    }

    sig->thread = CreateThread(NULL, 0, signaling_recv_thread, sig, 0, NULL);
    return 1;
}

int signaling_is_connected(signaling_t *sig) {
    return sig->sock != INVALID_SOCKET && sig->running;
}

static int send_all(SOCKET s, const char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, buf + sent, len - sent, 0);
        if (n <= 0) return 0;
        sent += n;
    }
    return 1;
}

void signaling_disconnect(signaling_t *sig) {
    LOG_INFO("disconnecting");
    sig->running = 0;

    EnterCriticalSection(&sig->send_lock);
    if (sig->sock != INVALID_SOCKET) {
        /* Graceful close: send UNREGISTER then shutdown send side.
           shutdown(SD_SEND) sends FIN and waits for server ACK before RST,
           preventing data loss from abortive close. */
        const char *cmd = "UNREGISTER\n";
        send(sig->sock, cmd, (int)strlen(cmd), 0);
        shutdown(sig->sock, SD_SEND);
        /* Let the FIN+data reach server before closing */
        Sleep(200);
        closesocket(sig->sock);
        sig->sock = INVALID_SOCKET;
        LOG_INFO("socket closed (graceful)");
    }
    LeaveCriticalSection(&sig->send_lock);

    if (sig->thread) {
        LOG_INFO("waiting for recv thread");
        WaitForSingleObject(sig->thread, 3000);  /* increased from 1000ms */
        CloseHandle(sig->thread);
        sig->thread = NULL;
        LOG_INFO("recv thread cleaned up");
    }
}

int signaling_send_ice(signaling_t *sig, const char *target_id, const char *sdp) {
    char cmd[1024];
    int n = _snprintf(cmd, sizeof(cmd), "ICE %s %s\n", target_id, sdp ? sdp : "");
    EnterCriticalSection(&sig->send_lock);
    int ok = 0;
    if (sig->sock != INVALID_SOCKET && sig->running)
        ok = send_all(sig->sock, cmd, n);
    LeaveCriticalSection(&sig->send_lock);
    return ok;
}

int signaling_send_relay(signaling_t *sig, const char *target_id, const uint8_t *data, int len) {
    if (sig->sock == INVALID_SOCKET || !sig->running) return 0;
    char b64[600];
    base64_encode(data, len, b64, sizeof(b64));
    char cmd[700];
    int n = _snprintf(cmd, sizeof(cmd), "RELAY %s %s\n", target_id, b64);
    EnterCriticalSection(&sig->send_lock);
    int ok = 0;
    if (sig->sock != INVALID_SOCKET && sig->running)
        ok = send_all(sig->sock, cmd, n);
    LeaveCriticalSection(&sig->send_lock);
    return ok;
}
