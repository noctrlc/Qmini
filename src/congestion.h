#ifndef CONGESTION_H
#define CONGESTION_H

#include <stdint.h>
#include <windows.h>

#define CC_BITRATE_MAX   64000
#define CC_BITRATE_MIN   16000
#define CC_STEP_UP       8000
#define CC_STEP_DOWN    16000

/* Per-peer congestion tracking (embedded in peer_state_t) */
typedef struct {
    uint32_t packets_sent;
    uint32_t packets_acked;
    uint32_t last_loss_check;
    int      current_bitrate;
    uint32_t last_bitrate_change;
    float    smoothed_loss;
    int      up_count;
    int      down_count;
} peer_cc_t;

void peer_cc_init(peer_cc_t *cc);
void peer_cc_update_sent(peer_cc_t *cc);
void peer_cc_update_acked(peer_cc_t *cc, uint16_t seq);
int  peer_cc_get_bitrate(peer_cc_t *cc);

#endif
