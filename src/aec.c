#include "aec.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct aec_t {
    float w[AEC_TAIL_LENGTH];
    float far_buf[AEC_TAIL_LENGTH];
    int   buf_pos;
    float power;
    /* Enhanced features */
    int   delay_samples;
    int   delay_estimated;          /* 1 once delay search completed */
    int   dtd_active;               /* double-talk flag (applied next frame) */
    int   dtd_hold;                 /* frames remaining before re-enabling adapt */
    float near_energy;              /* smoothed near-end energy */
    float echo_energy;              /* smoothed echo estimate energy */
    float erle_smoothed;            /* smoothed ERLE in dB */
    float filter_norm_smoothed;     /* smoothed L2 norm */
    int   frame_count;              /* frames processed for delay estimation */
};

#define AEC_EPSILON   1e-6f
#define AEC_STEP      0.5f
#define AEC_SMOOTH    0.9f
#define DTD_RATIO     2.0f   /* declare DTD when near > echo * ratio */
#define DTD_HOLD      3      /* freeze adaptation for N frames after DTD */
#define ERLE_ALPHA    0.95f  /* EMA for ERLE */

/* --- Helpers --- */

static float compute_energy(const short *samples, int count) {
    float e = 0.0f;
    for (int i = 0; i < count; i++)
        e += (float)samples[i] * (float)samples[i];
    return e;
}

static float compute_filter_norm(const float *w, int taps) {
    float n = 0.0f;
    for (int i = 0; i < taps; i++)
        n += w[i] * w[i];
    return sqrtf(n);
}

/* --- Delay estimation via cross-correlation --- */
static int estimate_delay(aec_t *aec, const short *near_in, const short *far_ref,
                           int search_max_samples) {
    /* Use ~10ms of far_ref to cross-correlate against near_in.
       Downsample by 4 to save CPU: every 4th sample. */
    int ds = 4;
    int far_len = AEC_FRAME_SIZE;
    int near_len = AEC_FRAME_SIZE;
    int search = search_max_samples / ds;
    if (search > near_len / ds) search = near_len / ds;

    float best_corr = -1e30f;
    int best_lag = 0;

    for (int lag = 0; lag < search; lag++) {
        float corr = 0.0f;
        int count = 0;
        for (int i = lag * ds; i < far_len / ds && count < (near_len / ds - lag); i++) {
            corr += (float)far_ref[i * ds] * (float)near_in[(i - lag) * ds];
            count++;
        }
        if (corr > best_corr) {
            best_corr = corr;
            best_lag = lag * ds;
        }
    }
    return best_lag;
}

/* --- Public API --- */

aec_t* aec_create(void) {
    return (aec_t*)calloc(1, sizeof(aec_t));
}

void aec_destroy(aec_t *aec) {
    free(aec);
}

void aec_reset(aec_t *aec) {
    if (!aec) return;
    memset(aec->w, 0, sizeof(aec->w));
    memset(aec->far_buf, 0, sizeof(aec->far_buf));
    aec->buf_pos = 0;
    aec->power = 0.0f;
    aec->delay_samples = 0;
    aec->delay_estimated = 0;
    aec->dtd_active = 0;
    aec->dtd_hold = 0;
    aec->near_energy = 0.0f;
    aec->echo_energy = 0.0f;
    aec->erle_smoothed = 0.0f;
    aec->filter_norm_smoothed = 0.0f;
    aec->frame_count = 0;
}

void aec_get_metrics(aec_t *aec, aec_metrics_t *m) {
    if (!aec || !m) return;
    m->erle_db = aec->erle_smoothed;
    m->filter_norm = aec->filter_norm_smoothed;
    m->double_talk = aec->dtd_active;
    m->delay_samples = aec->delay_samples;
    m->delay_confirmed = aec->delay_estimated;
}

void aec_process(aec_t *aec, const short *near_in, const short *far_ref, short *near_out) {
    if (!aec || !near_in || !far_ref || !near_out) return;

    aec->frame_count++;

    /* Delay estimation: run on first 20 frames */
    if (!aec->delay_estimated && aec->frame_count <= 20) {
        int est = estimate_delay(aec, near_in, far_ref, AEC_DELAY_MAX);
        /* Average across multiple frames for stability */
        aec->delay_samples = (aec->delay_samples * (aec->frame_count - 1) + est) / aec->frame_count;
        if (aec->frame_count == 20)
            aec->delay_estimated = 1;
    }

    /* Per-frame energy accumulation for DTD and ERLE */
    float frame_near_energy = 0.0f;
    float frame_echo_energy = 0.0f;
    float frame_out_energy = 0.0f;

    for (int i = 0; i < AEC_FRAME_SIZE; i++) {
        /* Write far-end sample */
        aec->far_buf[aec->buf_pos] = (float)far_ref[i];

        /* Compute echo estimate with delay offset */
        float y_hat = 0.0f;
        int idx = aec->buf_pos - aec->delay_samples;
        while (idx < 0) idx += AEC_TAIL_LENGTH;
        for (int tap = 0; tap < AEC_TAIL_LENGTH; tap++) {
            y_hat += aec->w[tap] * aec->far_buf[idx];
            idx--;
            if (idx < 0) idx = AEC_TAIL_LENGTH - 1;
        }

        float err = (float)near_in[i] - y_hat;

        /* Compute far-end power (smoothed) */
        float sample_power = 0.0f;
        idx = aec->buf_pos;
        for (int tap = 0; tap < AEC_TAIL_LENGTH; tap++) {
            float s = aec->far_buf[idx];
            sample_power += s * s;
            idx--;
            if (idx < 0) idx = AEC_TAIL_LENGTH - 1;
        }
        aec->power = AEC_SMOOTH * aec->power + (1.0f - AEC_SMOOTH) * sample_power;

        /* NLMS weight update (skip if DTD is active) */
        if (!aec->dtd_active) {
            float step = AEC_STEP / (aec->power + AEC_EPSILON);
            idx = aec->buf_pos - aec->delay_samples;
            while (idx < 0) idx += AEC_TAIL_LENGTH;
            for (int tap = 0; tap < AEC_TAIL_LENGTH; tap++) {
                aec->w[tap] += step * err * aec->far_buf[idx];
                idx--;
                if (idx < 0) idx = AEC_TAIL_LENGTH - 1;
            }
        } else if (aec->dtd_hold > 0) {
            aec->dtd_hold--;
            if (aec->dtd_hold == 0)
                aec->dtd_active = 0;
        }

        /* Output */
        if (err > 32767.0f) err = 32767.0f;
        if (err < -32768.0f) err = -32768.0f;
        near_out[i] = (short)err;

        /* Accumulate energies */
        frame_near_energy += (float)near_in[i] * (float)near_in[i];
        frame_echo_energy += y_hat * y_hat;
        frame_out_energy += err * err;

        /* Advance buffer */
        aec->buf_pos++;
        if (aec->buf_pos >= AEC_TAIL_LENGTH) aec->buf_pos = 0;
    }

    /* --- End of frame: DTD and ERLE --- */
    aec->near_energy = 0.9f * aec->near_energy + 0.1f * frame_near_energy;
    aec->echo_energy = 0.9f * aec->echo_energy + 0.1f * frame_echo_energy;

    /* Double-talk detection */
    if (aec->near_energy > aec->echo_energy * DTD_RATIO) {
        aec->dtd_active = 1;
        aec->dtd_hold = DTD_HOLD;
    }

    /* ERLE = 10 * log10(near_energy / out_energy) */
    if (frame_out_energy > 1.0f && frame_near_energy > 1.0f) {
        float erle = 10.0f * log10f(frame_near_energy / frame_out_energy);
        if (erle < 0.0f) erle = 0.0f;
        if (erle > 30.0f) erle = 30.0f;  /* cap unrealistic values */
        aec->erle_smoothed = ERLE_ALPHA * aec->erle_smoothed + (1.0f - ERLE_ALPHA) * erle;
    }

    /* Filter norm (convergence indicator) */
    aec->filter_norm_smoothed = 0.99f * aec->filter_norm_smoothed +
                                 0.01f * compute_filter_norm(aec->w, AEC_TAIL_LENGTH);
}
