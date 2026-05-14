#include "aec.h"
#include <stdlib.h>
#include <string.h>

struct aec_t {
    float w[AEC_TAIL_LENGTH];       /* adaptive filter weights */
    float far_buf[AEC_TAIL_LENGTH]; /* far-end reference buffer (circular) */
    int buf_pos;                    /* current write position in far_buf */
    float power;                    /* smoothed far-end power estimate */
};

#define AEC_EPSILON  1e-6f
#define AEC_STEP     0.5f
#define AEC_SMOOTH   0.9f

aec_t* aec_create(void)
{
    aec_t *aec = (aec_t *)calloc(1, sizeof(aec_t));
    if (!aec) return NULL;
    aec->buf_pos = 0;
    aec->power = 0.0f;
    return aec;
}

void aec_destroy(aec_t *aec)
{
    if (aec) free(aec);
}

void aec_reset(aec_t *aec)
{
    if (!aec) return;
    memset(aec->w, 0, sizeof(aec->w));
    memset(aec->far_buf, 0, sizeof(aec->far_buf));
    aec->buf_pos = 0;
    aec->power = 0.0f;
}

void aec_process(aec_t *aec, const short *near_in, const short *far_ref, short *near_out)
{
    if (!aec || !near_in || !far_ref || !near_out) return;

    for (int i = 0; i < AEC_FRAME_SIZE; i++) {
        /* Write new far-end sample into circular buffer */
        aec->far_buf[aec->buf_pos] = (float)far_ref[i];

        /* Compute filter output (estimated echo) */
        float y_hat = 0.0f;
        int idx = aec->buf_pos;
        for (int tap = 0; tap < AEC_TAIL_LENGTH; tap++) {
            y_hat += aec->w[tap] * aec->far_buf[idx];
            idx--;
            if (idx < 0) idx = AEC_TAIL_LENGTH - 1;
        }

        /* Error = near_in - estimated_echo */
        float err = (float)near_in[i] - y_hat;

        /* Update smoothed power estimate */
        float sample_power = 0.0f;
        idx = aec->buf_pos;
        for (int tap = 0; tap < AEC_TAIL_LENGTH; tap++) {
            float s = aec->far_buf[idx];
            sample_power += s * s;
            idx--;
            if (idx < 0) idx = AEC_TAIL_LENGTH - 1;
        }
        aec->power = AEC_SMOOTH * aec->power + (1.0f - AEC_SMOOTH) * sample_power;

        /* NLMS weight update: w += step * err * far / (power + eps) */
        float step = AEC_STEP / (aec->power + AEC_EPSILON);
        idx = aec->buf_pos;
        for (int tap = 0; tap < AEC_TAIL_LENGTH; tap++) {
            aec->w[tap] += step * err * aec->far_buf[idx];
            idx--;
            if (idx < 0) idx = AEC_TAIL_LENGTH - 1;
        }

        /* Output the error signal (echo-cancelled) */
        if (err > 32767.0f) err = 32767.0f;
        if (err < -32768.0f) err = -32768.0f;
        near_out[i] = (short)err;

        /* Advance circular buffer position */
        aec->buf_pos++;
        if (aec->buf_pos >= AEC_TAIL_LENGTH) aec->buf_pos = 0;
    }
}
