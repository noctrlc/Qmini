#ifndef CONGESTION_H
#define CONGESTION_H

#include <stdint.h>

typedef struct {
    uint32_t packets_sent;
    uint32_t packets_acked;
    uint32_t last_loss_check;
    int      current_bitrate;
    uint32_t last_bitrate_change;
} congestion_ctrl_t;

void congestion_init(congestion_ctrl_t *cc);
void congestion_update_sent(congestion_ctrl_t *cc);
void congestion_update_acked(congestion_ctrl_t *cc, uint16_t seq);
int  congestion_get_bitrate(congestion_ctrl_t *cc);

#endif
