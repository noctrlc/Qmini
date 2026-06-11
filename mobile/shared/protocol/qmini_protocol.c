/**
 * QminiDoctor Mobile - Signaling protocol implementation
 * Parses and builds text-based TCP signaling messages
 */
#include "qmini_protocol.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Skip leading spaces */
static const char *skip_space(const char *s) {
    while (*s == ' ') s++;
    return s;
}

/* Read one token (space-delimited) into dst, return pointer past it */
static const char *read_token(const char *src, char *dst, size_t dstlen) {
    size_t i = 0;
    const char *s = skip_space(src);
    while (*s && *s != ' ' && *s != '\n' && *s != '\r' && i < dstlen - 1) {
        dst[i++] = *s++;
    }
    dst[i] = '\0';
    return s;
}

/* Read rest of line (trimmed) into dst */
static const char *read_line(const char *src, char *dst, size_t dstlen) {
    size_t i = 0;
    const char *s = skip_space(src);
    while (*s && *s != '\n' && *s != '\r' && i < dstlen - 1) {
        dst[i++] = *s++;
    }
    dst[i] = '\0';
    return s;
}

int qmini_parse_message(const char *raw, qmini_msg_t *msg) {
    if (!raw || !msg) return -1;
    memset(msg, 0, sizeof(*msg));

    const char *p = raw;
    char cmd[32];
    p = read_token(p, cmd, sizeof(cmd));

    if (strcmp(cmd, "OK") == 0) {
        msg->type = MSG_OK;
        p = read_token(p, msg->peer_id, sizeof(msg->peer_id));
    }
    else if (strcmp(cmd, "PEER_JOIN") == 0) {
        msg->type = MSG_PEER_JOIN;
        p = read_token(p, msg->peer_id, sizeof(msg->peer_id));
        p = read_token(p, msg->nickname, sizeof(msg->nickname));
        p = read_token(p, msg->ip, sizeof(msg->ip));
        char port_str[16];
        p = read_token(p, port_str, sizeof(port_str));
        msg->port = (uint16_t)atoi(port_str);
    }
    else if (strcmp(cmd, "PEER_LEAVE") == 0) {
        msg->type = MSG_PEER_LEAVE;
        p = read_token(p, msg->peer_id, sizeof(msg->peer_id));
    }
    else if (strcmp(cmd, "ICE") == 0) {
        msg->type = MSG_ICE;
        p = read_token(p, msg->peer_id, sizeof(msg->peer_id));
        p = read_line(p, msg->payload, sizeof(msg->payload));
    }
    else if (strcmp(cmd, "RELAY") == 0) {
        msg->type = MSG_RELAY;
        p = read_token(p, msg->peer_id, sizeof(msg->peer_id));
        p = read_line(p, msg->payload, sizeof(msg->payload));
    }
    else {
        msg->type = MSG_UNKNOWN;
        return -1;
    }
    return 0;
}

int qmini_build_register(char *buf, size_t buflen,
                         const char *room, const char *nick, uint16_t local_port) {
    if (!buf || !room || !nick) return -1;
    return snprintf(buf, buflen, "REGISTER %s %s %u 0 0\n", room, nick, local_port);
}

int qmini_build_ice(char *buf, size_t buflen,
                    const char *target_id, const char *sdp) {
    if (!buf || !target_id || !sdp) return -1;
    return snprintf(buf, buflen, "ICE %s %s\n", target_id, sdp);
}

int qmini_build_relay(char *buf, size_t buflen,
                      const char *target_id, const char *base64_data) {
    if (!buf || !target_id || !base64_data) return -1;
    return snprintf(buf, buflen, "RELAY %s %s\n", target_id, base64_data);
}

int qmini_build_hello(char *buf, size_t buflen, const char *peer_id) {
    if (!buf || !peer_id) return -1;
    return snprintf(buf, buflen, "HELLO %s", peer_id);  /* no newline for UDP */
}
