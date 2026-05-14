#ifndef SFU_CLIENT_H
#define SFU_CLIENT_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>

#define SFU_ID_LEN 32
#define SFU_MAX_PACKET 1500

typedef struct {
    SOCKET              udp_sock;
    struct sockaddr_in  server_addr;
    int                 connected;
    int                 running;
    char                local_id[SFU_ID_LEN];
    HANDLE              recv_thread;
    void               *user_data;
    void (*recv_cb)(const char *peer_id, const uint8_t *data, int len, void *user);
} sfu_client_t;

/* Initialize SFU client: creates UDP socket, connects to server relay port.
   recv_cb is called (on recv thread) for each incoming audio packet.
   Returns 1 on success, 0 on failure. */
int  sfu_init(sfu_client_t *sfu, const char *server_ip, uint16_t server_port,
              void (*cb)(const char*, const uint8_t*, int, void*), void *user);

/* Shut down SFU client: stops recv thread, closes socket. */
void sfu_close(sfu_client_t *sfu);

/* Send audio data to the server for relay.
   Prepends 32-byte local_id header automatically.
   Returns 1 on success, 0 on failure. */
int  sfu_send(sfu_client_t *sfu, const uint8_t *data, int len);

/* Set the local peer ID (must match signaling-assigned ID). */
void sfu_set_local_id(sfu_client_t *sfu, const char *id);

/* Send HELLO to server for initial UDP address mapping (NAT traversal).
   Call after sfu_set_local_id(). Sends 3 times for reliability. */
void sfu_send_hello(sfu_client_t *sfu);

#endif
