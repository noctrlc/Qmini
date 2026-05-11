#ifndef NETWORK_H
#define NETWORK_H

#include <windows.h>
#include <winsock2.h>
#include <stdint.h>

#define MAX_PEERS 10
#define MAX_PACKET 1500

typedef struct {
    SOCKET           udp_sock;
    struct sockaddr_in addr;
    char             id[32];
    int              connected;
    uint16_t         seq_send;
    DWORD            last_keepalive;  /* tick count of last keepalive sent */
} peer_t;

typedef struct {
    SOCKET           udp_sock;
    HANDLE           thread;
    int              running;
    uint16_t         local_port;      /* actual UDP port after bind */
    peer_t           peers[MAX_PEERS];
    int              npeers;
    char             local_id[32];
    uint16_t         seq_send;
    void             *user_data;
    void             (*recv_cb)(const char *peer_id, const uint8_t *data, int len, void *user);
    CRITICAL_SECTION lock;
} network_t;

int  network_init(network_t *net, uint16_t port, void (*cb)(const char*, const uint8_t*, int, void*), void *user);
void network_close(network_t *net);
int  network_add_peer(network_t *net, const char *id, const struct sockaddr_in *addr);
void network_remove_peer(network_t *net, const char *id);
int  network_send(network_t *net, const char *peer_id, const uint8_t *data, int len);
int  network_send_all(network_t *net, const uint8_t *data, int len);
int  network_set_turn_relay(network_t *net, const char *peer_id, const struct sockaddr_in *relay_addr);
/* Periodic maintenance: sends NAT keepalives. Call from timer (~every second). */
void network_tick(network_t *net);
/* Get the actual UDP port assigned to the network socket */
uint16_t network_get_port(network_t *net);

#endif
