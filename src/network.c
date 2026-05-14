#include "network.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

static peer_t* find_peer(network_t *net, const char *id) {
    for (int i = 0; i < net->npeers; i++)
        if (strcmp(net->peers[i].id, id) == 0)
            return &net->peers[i];
    return NULL;
}

static void net_log(const char *msg) {
    FILE *f = fopen("D:\\QminiDoctor\\sig_log.txt", "a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d] net_recv: %s\n", st.wHour, st.wMinute, st.wSecond, msg);
        fclose(f);
    }
}

static DWORD WINAPI net_recv_thread(LPVOID arg) {
    network_t *net = (network_t*)arg;
    uint8_t buf[MAX_PACKET];
    struct sockaddr_in from;
    int from_len = sizeof(from);

    net_log("net_recv_thread started");

    while (net->running) {
        __try {
        fd_set read_set;
        struct timeval tv = {0, 50000};
        FD_ZERO(&read_set);
        FD_SET(net->udp_sock, &read_set);

        int ret = select(0, &read_set, NULL, NULL, &tv);
        if (ret <= 0) continue;

        from_len = sizeof(from);
        int n = recvfrom(net->udp_sock, (char*)buf, MAX_PACKET, 0, (struct sockaddr*)&from, &from_len);
        if (n <= 0) continue;

        if (n < 32) continue;
        char peer_id[33] = {0};
        memcpy(peer_id, buf, 32);
        peer_id[32] = 0;

        /* 32-byte header-only packet = keepalive/ping.
           Update peer address (handles NAT rebinding) and skip. */
        if (n == 32) {
            for (int i = 0; i < net->npeers; i++) {
                if (strcmp(net->peers[i].id, peer_id) == 0) {
                    net->peers[i].addr = from;
                    if (net->keepalive_cb)
                        net->keepalive_cb(peer_id, net->user_data);
                    break;
                }
            }
            continue;
        }

        /* Data packet */
        uint8_t *payload = buf + 32;
        int payload_len = n - 32;

        if (net->crypto && crypto_is_ready(net->crypto)) {
            /* Find peer to get expected seq_recv */
            peer_t *sender = NULL;
            for (int i = 0; i < net->npeers; i++) {
                if (strcmp(net->peers[i].id, peer_id) == 0) {
                    sender = &net->peers[i];
                    break;
                }
            }
            if (!sender) continue;

            /* Decrypt the data */
            uint8_t decrypted[MAX_PACKET];
            int dec_len = crypto_decrypt(net->crypto, sender->seq_recv,
                                         payload, decrypted, payload_len);
            if (dec_len == 0) {
                /* Decryption failed, skip packet */
                continue;
            }
            sender->seq_recv++;
            if (net->recv_cb)
                net->recv_cb(peer_id, decrypted, dec_len, net->user_data);
        } else {
            /* No encryption */
            if (net->recv_cb)
                net->recv_cb(peer_id, payload, payload_len, net->user_data);
        }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            char log_buf[128];
            _snprintf(log_buf, sizeof(log_buf), "CRASH in net_recv_thread! code=0x%08X", GetExceptionCode());
            net_log(log_buf);
        }
    }
    net_log("net_recv_thread exited");
    return 0;
}

int network_init(network_t *net, uint16_t port, void (*cb)(const char*, const uint8_t*, int, void*), void *user) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;

    memset(net, 0, sizeof(*net));
    net->running = 1;
    net->recv_cb = cb;
    net->user_data = user;

    net->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (net->udp_sock == INVALID_SOCKET) { WSACleanup(); return 0; }

    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(port);
    if (bind(net->udp_sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        closesocket(net->udp_sock);
        WSACleanup();
        return 0;
    }

    /* Get actual port assigned by OS */
    struct sockaddr_in actual;
    int actual_len = sizeof(actual);
    if (getsockname(net->udp_sock, (struct sockaddr*)&actual, &actual_len) == 0) {
        net->local_port = ntohs(actual.sin_port);
    } else {
        net->local_port = port;
    }

    u_long nonblock = 1;
    ioctlsocket(net->udp_sock, FIONBIO, &nonblock);

    _snprintf(net->local_id, sizeof(net->local_id), "qmini_%d", net->local_port);

    net->thread = CreateThread(NULL, 0, net_recv_thread, net, 0, NULL);
    return 1;
}

void network_close(network_t *net) {
    net->running = 0;
    if (net->thread) {
        WaitForSingleObject(net->thread, 2000);
        CloseHandle(net->thread);
        net->thread = NULL;
    }
    if (net->udp_sock != INVALID_SOCKET) {
        closesocket(net->udp_sock);
        net->udp_sock = INVALID_SOCKET;
    }
    WSACleanup();
}

int network_add_peer(network_t *net, const char *id, const struct sockaddr_in *addr) {
    if (net->npeers >= MAX_PEERS) return 0;
    peer_t *p = &net->peers[net->npeers++];
    strncpy(p->id, id, sizeof(p->id) - 1);
    p->addr = *addr;
    p->connected = 1;
    p->seq_send = 0;
    p->seq_recv = 0;
    p->last_keepalive = 0;  /* send keepalive immediately */
    return 1;
}

void network_remove_peer(network_t *net, const char *id) {
    for (int i = 0; i < net->npeers; i++) {
        if (strcmp(net->peers[i].id, id) == 0) {
            memmove(&net->peers[i], &net->peers[i+1], (net->npeers - i - 1) * sizeof(peer_t));
            net->npeers--;
            return;
        }
    }
}

int network_send(network_t *net, const char *peer_id, const uint8_t *data, int len) {
    peer_t *p = find_peer(net, peer_id);
    if (!p || !p->connected) return 0;

    uint8_t buf[MAX_PACKET];
    /* Pre-built header: 32-byte local_id, zero-padded */
    memset(buf, 0, 32);
    memcpy(buf, net->local_id, strlen(net->local_id));

    int total;
    if (net->crypto && crypto_is_ready(net->crypto)) {
        /* Encrypt the data */
        uint8_t encrypted[MAX_PACKET];
        int enc_len = crypto_encrypt(net->crypto, p->seq_send, data, encrypted, len);
        if (enc_len == 0) return 0;

        total = 32 + enc_len + CRYPTO_HMAC_SIZE;
        if (total > MAX_PACKET) return 0;
        memcpy(buf + 32, encrypted, enc_len + CRYPTO_HMAC_SIZE);
        p->seq_send++;
    } else {
        /* No encryption */
        total = 32 + len;
        if (total > MAX_PACKET) return 0;
        memcpy(buf + 32, data, len);
    }

    int sent = sendto(net->udp_sock, (const char*)buf, total, 0,
                      (struct sockaddr*)&p->addr, sizeof(p->addr));
    return sent > 0;
}

int network_send_all(network_t *net, const uint8_t *data, int len) {
    int ok = 0;
    for (int i = 0; i < net->npeers; i++)
        if (network_send(net, net->peers[i].id, data, len)) ok = 1;
    return ok;
}

int network_set_turn_relay(network_t *net, const char *peer_id, const struct sockaddr_in *relay_addr) {
    peer_t *p = find_peer(net, peer_id);
    if (!p) return 0;
    p->addr = *relay_addr;
    return 1;
}

uint16_t network_get_port(network_t *net) {
    return net->local_port;
}

void network_set_local_id(network_t *net, const char *id) {
    strncpy(net->local_id, id, sizeof(net->local_id) - 1);
    net->local_id[sizeof(net->local_id) - 1] = 0;
}

void network_set_crypto(network_t *net, crypto_ctx_t *crypto) {
    net->crypto = crypto;
}

void network_tick(network_t *net) {
    DWORD now = GetTickCount();
    uint8_t buf[32];
    memset(buf, 0, 32);
    memcpy(buf, net->local_id, strlen(net->local_id));

    for (int i = 0; i < net->npeers; i++) {
        if (net->peers[i].connected && now - net->peers[i].last_keepalive >= 3000) {
            sendto(net->udp_sock, (const char*)buf, 32, 0,
                   (struct sockaddr*)&net->peers[i].addr, sizeof(net->peers[i].addr));
            net->peers[i].last_keepalive = now;
        }
    }
}
