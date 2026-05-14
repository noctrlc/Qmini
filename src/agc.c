#include "agc.h"
#include <stdlib.h>
#include <math.h>

struct agc_t {
    int   sample_rate;
    float target_level;     /* linear target level */
    float gain;             /* current gain */
    float attack_coeff;     /* fast attack coefficient */
    float release_coeff;    /* slow release coefficient */
};

#define AGC_GAIN_MIN   0.1f
#define AGC_GAIN_MAX   30.0f
#define AGC_SMOOTH_MS  20.0f   /* attack time constant in ms */
#define AGC_RELEASE_MS 200.0f  /* release time constant in ms */

agc_t* agc_create(int sample_rate, int target_level_dbfs)
{
    agc_t *agc = (agc_t *)calloc(1, sizeof(agc_t));
    if (!agc) return NULL;

    agc->sample_rate = sample_rate;
    /* Convert dBFS to linear: target_linear = 32767 * 10^(target_dbfs/20) */
    agc->target_level = 32767.0f * powf(10.0f, (float)target_level_dbfs / 20.0f);
    agc->gain = 1.0f;

    /* Compute smoothing coefficients from time constants */
    float frame_ms = 1000.0f;  /* will be adjusted per-call based on frame size */
    (void)frame_ms;
    agc->attack_coeff  = expf(-1.0f / (AGC_SMOOTH_MS * (float)sample_rate / 1000.0f));
    agc->release_coeff = expf(-1.0f / (AGC_RELEASE_MS * (float)sample_rate / 1000.0f));

    return agc;
}

void agc_destroy(agc_t *agc)
{
    if (agc) free(agc);
}

void agc_process(agc_t *agc, short *frame, int samples)
{
    if (!agc || !frame || samples <= 0) return;

    /* Calculate RMS level of frame */
    float sum_sq = 0.0f;
    for (int i = 0; i < samples; i++) {
        float s = (float)frame[i];
        sum_sq += s * s;
    }
    float rms = sqrtf(sum_sq / (float)samples);

    /* Avoid division by zero */
    if (rms < 1.0f) rms = 1.0f;

    /* Compute desired gain to reach target level */
    float desired_gain = agc->target_level / rms;

    /* Clamp desired gain */
    if (desired_gain < AGC_GAIN_MIN) desired_gain = AGC_GAIN_MIN;
    if (desired_gain > AGC_GAIN_MAX) desired_gain = AGC_GAIN_MAX;

    /* Smooth gain: fast attack (gain decrease), slow release (gain increase) */
    float coeff;
    if (desired_gain < agc->gain) {
        /* Signal is too loud - use fast attack to reduce gain quickly */
        coeff = agc->attack_coeff;
    } else {
        /* Signal is too quiet - use slow release to increase gain gradually */
        coeff = agc->release_coeff;
    }
    agc->gain = coeff * agc->gain + (1.0f - coeff) * desired_gain;

    /* Apply gain with clipping protection */
    for (int i = 0; i < samples; i++) {
        float out = (float)frame[i] * agc->gain;
        if (out > 32767.0f) out = 32767.0f;
        if (out < -32768.0f) out = -32768.0f;
        frame[i] = (short)out;
    }
}
