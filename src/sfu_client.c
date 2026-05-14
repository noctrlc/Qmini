#include "sfu_client.h"
#include <string.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

static DWORD WINAPI sfu_recv_thread(LPVOID arg) {
    sfu_client_t *sfu = (sfu_client_t*)arg;
    uint8_t buf[SFU_MAX_PACKET];
    struct sockaddr_in from;
    int from_len;

    while (sfu->running) {
        fd_set read_set;
        struct timeval tv = {0, 50000};  /* 50ms timeout */
        FD_ZERO(&read_set);
        FD_SET(sfu->udp_sock, &read_set);

        int ret = select(0, &read_set, NULL, NULL, &tv);
        if (ret <= 0) continue;

        from_len = sizeof(from);
        int n = recvfrom(sfu->udp_sock, (char*)buf, SFU_MAX_PACKET, 0,
                         (struct sockaddr*)&from, &from_len);
        if (n <= 0) continue;

        /* Minimum packet: 32-byte sender ID header + some data */
        if (n < 32) continue;

        /* Extract sender peer ID from the 32-byte header */
        char peer_id[SFU_ID_LEN + 1] = {0};
        memcpy(peer_id, buf, SFU_ID_LEN);

        /* Audio payload starts after the 32-byte header */
        const uint8_t *payload = buf + SFU_ID_LEN;
        int payload_len = n - SFU_ID_LEN;

        if (payload_len > 0 && sfu->recv_cb) {
            sfu->recv_cb(peer_id, payload, payload_len, sfu->user_data);
        }
    }
    return 0;
}

int sfu_init(sfu_client_t *sfu, const char *server_ip, uint16_t server_port,
             void (*cb)(const char*, const uint8_t*, int, void*), void *user) {
    memset(sfu, 0, sizeof(*sfu));
    sfu->udp_sock = INVALID_SOCKET;
    sfu->recv_cb = cb;
    sfu->user_data = user;

    sfu->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sfu->udp_sock == INVALID_SOCKET)
        return 0;

    /* Set up server address */
    sfu->server_addr.sin_family = AF_INET;
    sfu->server_addr.sin_addr.s_addr = inet_addr(server_ip);
    sfu->server_addr.sin_port = htons(server_port);

    /* Set non-blocking for the recv thread select loop */
    u_long nonblock = 1;
    ioctlsocket(sfu->udp_sock, FIONBIO, &nonblock);

    sfu->running = 1;
    sfu->connected = 1;

    /* Start receive thread */
    sfu->recv_thread = CreateThread(NULL, 0, sfu_recv_thread, sfu, 0, NULL);
    if (!sfu->recv_thread) {
        closesocket(sfu->udp_sock);
        sfu->udp_sock = INVALID_SOCKET;
        sfu->connected = 0;
        sfu->running = 0;
        return 0;
    }

    return 1;
}

void sfu_close(sfu_client_t *sfu) {
    sfu->running = 0;
    sfu->connected = 0;

    if (sfu->recv_thread) {
        WaitForSingleObject(sfu->recv_thread, 2000);
        CloseHandle(sfu->recv_thread);
        sfu->recv_thread = NULL;
    }

    if (sfu->udp_sock != INVALID_SOCKET) {
        closesocket(sfu->udp_sock);
        sfu->udp_sock = INVALID_SOCKET;
    }
}

int sfu_send(sfu_client_t *sfu, const uint8_t *data, int len) {
    if (!sfu->connected || sfu->udp_sock == INVALID_SOCKET)
        return 0;

    /* Build packet: 32-byte local_id header + audio data */
    uint8_t buf[SFU_MAX_PACKET];
    int total = SFU_ID_LEN + len;
    if (total > SFU_MAX_PACKET) return 0;

    memset(buf, 0, SFU_ID_LEN);
    memcpy(buf, sfu->local_id, strlen(sfu->local_id));
    memcpy(buf + SFU_ID_LEN, data, len);

    int sent = sendto(sfu->udp_sock, (const char*)buf, total, 0,
                      (struct sockaddr*)&sfu->server_addr, sizeof(sfu->server_addr));
    return sent > 0;
}

void sfu_set_local_id(sfu_client_t *sfu, const char *id) {
    memset(sfu->local_id, 0, sizeof(sfu->local_id));
    strncpy(sfu->local_id, id, SFU_ID_LEN - 1);
}

void sfu_send_hello(sfu_client_t *sfu) {
    if (!sfu->connected || sfu->udp_sock == INVALID_SOCKET)
        return;
    char hello[64];
    int n = _snprintf(hello, sizeof(hello), "HELLO %s", sfu->local_id);
    sendto(sfu->udp_sock, hello, n, 0,
           (struct sockaddr*)&sfu->server_addr, sizeof(sfu->server_addr));
}
