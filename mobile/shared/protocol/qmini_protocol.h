/**
 * QminiDoctor Mobile - Shared signaling protocol
 * Compatible with desktop signaling server (TCP port 9088)
 */
#ifndef QMINI_PROTOCOL_H
#define QMINI_PROTOCOL_H

#include <stdint.h>

#define QMINI_SIGNALLING_PORT  9088
#define QMINI_SFU_PORT         9089
#define QMINI_MAX_PEERS        10
#define QMINI_PEER_ID_LEN      32
#define QMINI_MAX_NICK_LEN     32
#define QMINI_MAX_ROOM_LEN     32

/* Signaling message types */
typedef enum {
    MSG_REGISTER,       /* C->S: REGISTER <room> <nick> <port> 0 0 */
    MSG_OK,             /* S->C: OK <peer_id> */
    MSG_PEER_JOIN,      /* S->C: PEER_JOIN <id> <nick> <ip> <port> */
    MSG_PEER_LEAVE,     /* S->C: PEER_LEAVE <id> */
    MSG_ICE,            /* C<->S: ICE <target_id> <payload> */
    MSG_RELAY,          /* C<->S: RELAY <target_id> <base64_data> */
    MSG_UNREGISTER,     /* C->S: UNREGISTER */
    MSG_UNKNOWN
} qmini_msg_type_t;

typedef struct {
    qmini_msg_type_t type;
    char peer_id[QMINI_PEER_ID_LEN + 1];
    char nickname[QMINI_MAX_NICK_LEN + 1];
    char room[QMINI_MAX_ROOM_LEN + 1];
    char ip[64];
    uint16_t port;
    char payload[1024];  /* ICE/RELAY data */
} qmini_msg_t;

/**
 * Parse a signaling message from raw TCP data.
 * Returns 0 on success, -1 on parse error.
 */
int qmini_parse_message(const char *raw, qmini_msg_t *msg);

/**
 * Build a REGISTER message.
 * Returns length written, or -1 on error.
 */
int qmini_build_register(char *buf, size_t buflen,
                         const char *room, const char *nick, uint16_t local_port);

/**
 * Build an ICE message.
 */
int qmini_build_ice(char *buf, size_t buflen,
                    const char *target_id, const char *sdp);

/**
 * Build a RELAY message.
 */
int qmini_build_relay(char *buf, size_t buflen,
                      const char *target_id, const char *base64_data);

/**
 * Build HELLO message for UDP NAT traversal.
 */
int qmini_build_hello(char *buf, size_t buflen, const char *peer_id);

#endif /* QMINI_PROTOCOL_H */
