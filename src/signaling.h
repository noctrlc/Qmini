#ifndef SIGNALING_H
#define SIGNALING_H

#include <windows.h>
#include <winsock2.h>

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
    void    (*peer_join_cb)(const char *peer_id, const char *nickname, struct sockaddr_in *addr, void *user);
    void    (*peer_leave_cb)(const char *peer_id, void *user);
    void    (*ice_cb)(const char *from_id, const char *sdp, void *user);
    void    *user_data;
} signaling_t;

int  signaling_connect(signaling_t *sig, const char *host, int port, const char *room,
                       const char *nickname, int local_port);
void signaling_disconnect(signaling_t *sig);
int  signaling_is_connected(signaling_t *sig);
int  signaling_send_ice(signaling_t *sig, const char *target_id, const char *sdp);

#endif
