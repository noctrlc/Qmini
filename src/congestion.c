#include "congestion.h"

#define LOSS_CHECK_MS   1000
#define CHANGE_COOLDOWN 2000
#define EMA_ALPHA       0.3f   /* smoothing factor for loss rate */

void peer_cc_init(peer_cc_t *cc) {
    cc->packets_sent        = 0;
    cc->packets_acked       = 0;
    cc->last_loss_check     = GetTickCount();
    cc->current_bitrate     = CC_BITRATE_MAX;
    cc->last_bitrate_change = GetTickCount();
    cc->smoothed_loss       = 0.0f;
    cc->up_count            = 0;
    cc->down_count          = 0;
}

void peer_cc_update_sent(peer_cc_t *cc) {
    cc->packets_sent++;
}

void peer_cc_update_acked(peer_cc_t *cc, uint16_t seq) {
    (void)seq;
    cc->packets_acked++;
}

int peer_cc_get_bitrate(peer_cc_t *cc) {
    DWORD now = GetTickCount();

    if (now - cc->last_loss_check < LOSS_CHECK_MS)
        return cc->current_bitrate;

    if (cc->packets_sent < 10) {  /* need enough samples */
        cc->last_loss_check = now;
        return cc->current_bitrate;
    }

    /* Compute and smooth loss rate */
    float current_loss = 1.0f - (float)cc->packets_acked / (float)cc->packets_sent;
    if (current_loss < 0.0f) current_loss = 0.0f;
    if (current_loss > 1.0f) current_loss = 1.0f;
    cc->smoothed_loss = EMA_ALPHA * current_loss + (1.0f - EMA_ALPHA) * cc->smoothed_loss;

    /* Determine desired bitrate tier */
    int desired;
    if (cc->smoothed_loss < 0.02f)       desired = CC_BITRATE_MAX;    /* <2% loss */
    else if (cc->smoothed_loss < 0.05f)  desired = 48000;              /* <5% */
    else if (cc->smoothed_loss < 0.10f)  desired = 32000;              /* <10% */
    else                                 desired = CC_BITRATE_MIN;     /* >=10% */

    /* Hysteresis: require 2 consecutive consistent checks */
    if (desired > cc->current_bitrate) {
        cc->up_count++;
        cc->down_count = 0;
        if (cc->up_count >= 2) {
            /* Slow uptick */
            int next = cc->current_bitrate + CC_STEP_UP;
            if (next > desired) next = desired;
            if (next > CC_BITRATE_MAX) next = CC_BITRATE_MAX;
            cc->current_bitrate = next;
            cc->last_bitrate_change = now;
            cc->up_count = 0;
        }
    } else if (desired < cc->current_bitrate) {
        cc->down_count++;
        cc->up_count = 0;
        if (cc->down_count >= 2) {
            /* Fast downtick */
            int next = cc->current_bitrate - CC_STEP_DOWN;
            if (next < desired) next = desired;
            if (next < CC_BITRATE_MIN) next = CC_BITRATE_MIN;
            cc->current_bitrate = next;
            cc->last_bitrate_change = now;
            cc->down_count = 0;
        }
    } else {
        cc->up_count = 0;
        cc->down_count = 0;
    }

    /* Reset counters for next interval */
    cc->packets_sent = 0;
    cc->packets_acked = 0;
    cc->last_loss_check = now;

    return cc->current_bitrate;
}
