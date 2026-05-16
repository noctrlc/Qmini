#ifndef SIGNALING_H
#define SIGNALING_H

#include <windows.h>
#include <winsock2.h>
#include <stdint.h>

/* Error codes returned by signaling_connect (stored in sig->last_error) */
typedef enum {
    SIG_OK = 0,
    SIG_ERR_SOCKET = -1,
    SIG_ERR_CONNECT = -2,
    SIG_ERR_TIMEOUT = -3,
    SIG_ERR_NO_RESPONSE = -4,
    SIG_ERR_REGISTER = -5,
} sig_result_t;

typedef struct {
    SOCKET  sock;
    HANDLE  thread;
    int     running;
    char    server_host[64];
    int     server_port;
    char    room[16];
    char    nickname[32];
    char    local_id[32];
    int     local_port;
    int     last_error;       /* sig_result_t on failure */
    CRITICAL_SECTION send_lock;
    void    (*peer_join_cb)(const char *peer_id, const char *nickname, struct sockaddr_in *addr, void *user);
    void    (*peer_leave_cb)(const char *peer_id, void *user);
    void    (*ice_cb)(const char *from_id, const char *sdp, void *user);
    void    (*relay_cb)(const char *from_id, const uint8_t *data, int len, void *user);
    void    *user_data;
} signaling_t;

int  signaling_connect(signaling_t *sig, const char *host, int port, const char *room,
                       const char *nickname, int local_port, SOCKET udp_sock);
void signaling_disconnect(signaling_t *sig);
int  signaling_is_connected(signaling_t *sig);
int  signaling_send_ice(signaling_t *sig, const char *target_id, const char *sdp);
int  signaling_send_relay(signaling_t *sig, const char *target_id, const uint8_t *data, int len);

#endif
