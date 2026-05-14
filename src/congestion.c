#include "congestion.h"
#include <windows.h>

#define BITRATE_HIGH    64000
#define BITRATE_MED     48000
#define BITRATE_LOW     32000
#define BITRATE_MIN     16000

#define LOSS_CHECK_MS   1000   /* check loss every 1 second */
#define CHANGE_COOLDOWN 2000   /* minimum 2 seconds between changes */

void congestion_init(congestion_ctrl_t *cc) {
    cc->packets_sent = 0;
    cc->packets_acked = 0;
    cc->last_loss_check = GetTickCount();
    cc->current_bitrate = BITRATE_HIGH;
    cc->last_bitrate_change = GetTickCount();
}

void congestion_update_sent(congestion_ctrl_t *cc) {
    cc->packets_sent++;
}

void congestion_update_acked(congestion_ctrl_t *cc, uint16_t seq) {
    (void)seq;
    cc->packets_acked++;
}

int congestion_get_bitrate(congestion_ctrl_t *cc) {
    DWORD now = GetTickCount();

    /* Only check loss rate every LOSS_CHECK_MS */
    if (now - cc->last_loss_check < LOSS_CHECK_MS)
        return cc->current_bitrate;

    /* Need enough packets for a meaningful sample */
    if (cc->packets_sent < 5) {
        cc->last_loss_check = now;
        return cc->current_bitrate;
    }

    /* Enforce cooldown between bitrate changes */
    if (now - cc->last_bitrate_change < CHANGE_COOLDOWN) {
        cc->last_loss_check = now;
        return cc->current_bitrate;
    }

    /* Compute loss percentage (integer math, loss% = 100 - ack*100/sent) */
    int loss_pct = 100 - (int)((uint64_t)cc->packets_acked * 100 / cc->packets_sent);

    int new_bitrate;
    if (loss_pct < 2)
        new_bitrate = BITRATE_HIGH;
    else if (loss_pct < 5)
        new_bitrate = BITRATE_MED;
    else if (loss_pct < 10)
        new_bitrate = BITRATE_LOW;
    else
        new_bitrate = BITRATE_MIN;

    if (new_bitrate != cc->current_bitrate) {
        cc->current_bitrate = new_bitrate;
        cc->last_bitrate_change = now;
    }

    /* Reset counters for next interval */
    cc->packets_sent = 0;
    cc->packets_acked = 0;
    cc->last_loss_check = now;

    return cc->current_bitrate;
}
