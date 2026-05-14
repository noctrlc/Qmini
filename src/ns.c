#include "ns.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NS_FFT_SIZE     256
#define NS_HOP_SIZE     (NS_FFT_SIZE / 2)  /* 50% overlap */
#define NS_NUM_BINS     (NS_FFT_SIZE / 2 + 1)

struct ns_t {
    int   sample_rate;
    float window[NS_FFT_SIZE];           /* Hann window */
    float prev_frame[NS_HOP_SIZE];       /* previous hop for overlap-add */
    float noise_est[NS_NUM_BINS];        /* noise power estimate per bin */
    int   noise_frames;                  /* number of frames used for noise estimate */
    int   initialized;                   /* 1 after initial noise estimate is done */
};

/* --- Simple FFT (Cooley-Tukey radix-2, in-place) --- */

static void fft(float *re, float *im, int n)
{
    /* Bit-reversal permutation */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float tmp;
            tmp = re[i]; re[i] = re[j]; re[j] = tmp;
            tmp = im[i]; im[i] = im[j]; im[j] = tmp;
        }
    }

    /* FFT butterfly */
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * (float)M_PI / (float)len;
        float w_re = cosf(ang);
        float w_im = sinf(ang);
        for (int i = 0; i < n; i += len) {
            float cur_re = 1.0f, cur_im = 0.0f;
            for (int j = 0; j < len / 2; j++) {
                float t_re = cur_re * re[i + j + len/2] - cur_im * im[i + j + len/2];
                float t_im = cur_re * im[i + j + len/2] + cur_im * re[i + j + len/2];
                float u_re = re[i + j];
                float u_im = im[i + j];
                re[i + j] = u_re + t_re;
                im[i + j] = u_im + t_im;
                re[i + j + len/2] = u_re - t_re;
                im[i + j + len/2] = u_im - t_im;
                float new_re = cur_re * w_re - cur_im * w_im;
                float new_im = cur_re * w_im + cur_im * w_re;
                cur_re = new_re;
                cur_im = new_im;
            }
        }
    }
}

static void ifft(float *re, float *im, int n)
{
    /* Conjugate, FFT, conjugate, scale */
    for (int i = 0; i < n; i++)
        im[i] = -im[i];
    fft(re, im, n);
    for (int i = 0; i < n; i++) {
        re[i] /= (float)n;
        im[i] = -im[i] / (float)n;
    }
}

ns_t* ns_create(int sample_rate)
{
    ns_t *ns = (ns_t *)calloc(1, sizeof(ns_t));
    if (!ns) return NULL;

    ns->sample_rate = sample_rate;
    ns->noise_frames = 0;
    ns->initialized = 0;

    /* Generate Hann window */
    for (int i = 0; i < NS_FFT_SIZE; i++) {
        ns->window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(NS_FFT_SIZE - 1)));
    }

    return ns;
}

void ns_destroy(ns_t *ns)
{
    if (ns) free(ns);
}

void ns_process(ns_t *ns, short *frame, int samples)
{
    if (!ns || !frame || samples < NS_HOP_SIZE) return;

    /* Process in NS_HOP_SIZE chunks with overlap-add */
    int pos = 0;
    while (pos + NS_HOP_SIZE <= samples) {
        float re[NS_FFT_SIZE];
        float im[NS_FFT_SIZE];

        /* Build FFT frame: previous hop (windowed) + current hop (windowed) */
        for (int i = 0; i < NS_HOP_SIZE; i++) {
            re[i] = ns->prev_frame[i] * ns->window[i];
            im[i] = 0.0f;
        }
        for (int i = 0; i < NS_HOP_SIZE; i++) {
            re[NS_HOP_SIZE + i] = (float)frame[pos + i] * ns->window[NS_HOP_SIZE + i];
            im[NS_HOP_SIZE + i] = 0.0f;
        }

        /* Save current hop for next overlap */
        for (int i = 0; i < NS_HOP_SIZE; i++) {
            ns->prev_frame[i] = (float)frame[pos + i];
        }

        /* Forward FFT */
        fft(re, im, NS_FFT_SIZE);

        /* Compute power spectrum */
        float mag[NS_NUM_BINS];
        for (int k = 0; k < NS_NUM_BINS; k++) {
            mag[k] = re[k] * re[k] + im[k] * im[k];
        }

        /* Noise estimation: average first ~160ms of frames */
        if (!ns->initialized) {
            if (ns->noise_frames == 0) {
                for (int k = 0; k < NS_NUM_BINS; k++)
                    ns->noise_est[k] = mag[k];
            } else {
                float alpha = 0.9f;
                for (int k = 0; k < NS_NUM_BINS; k++)
                    ns->noise_est[k] = alpha * ns->noise_est[k] + (1.0f - alpha) * mag[k];
            }
            ns->noise_frames++;
            if (ns->noise_frames >= 10)
                ns->initialized = 1;
        }

        /* Spectral subtraction with over-subtraction factor */
        float alpha = 2.0f;       /* over-subtraction factor */
        float floor_factor = 0.02f; /* spectral floor */
        for (int k = 0; k < NS_NUM_BINS; k++) {
            float clean_power = mag[k] - alpha * ns->noise_est[k];
            float floor_val = floor_factor * mag[k];
            if (clean_power < floor_val)
                clean_power = floor_val;

            /* Gain = sqrt(clean_power / original_power) */
            float gain = 1.0f;
            if (mag[k] > 1e-10f) {
                gain = sqrtf(clean_power / mag[k]);
            }

            re[k] *= gain;
            im[k] *= gain;
            /* Mirror for negative frequencies */
            if (k > 0 && k < NS_FFT_SIZE / 2) {
                re[NS_FFT_SIZE - k] *= gain;
                im[NS_FFT_SIZE - k] *= gain;
            }
        }

        /* Adaptive noise update during low-SNR frames */
        if (ns->initialized) {
            float sig_power = 0.0f, noise_power = 0.0f;
            for (int k = 0; k < NS_NUM_BINS; k++) {
                sig_power += mag[k];
                noise_power += ns->noise_est[k];
            }
            if (sig_power < 2.0f * noise_power) {
                float beta = 0.98f;
                for (int k = 0; k < NS_NUM_BINS; k++)
                    ns->noise_est[k] = beta * ns->noise_est[k] + (1.0f - beta) * mag[k];
            }
        }

        /* Inverse FFT */
        ifft(re, im, NS_FFT_SIZE);

        /* Overlap-add: first half goes to output, second half becomes new overlap */
        for (int i = 0; i < NS_HOP_SIZE; i++) {
            float out = re[i];
            if (out > 32767.0f) out = 32767.0f;
            if (out < -32768.0f) out = -32768.0f;
            frame[pos + i] = (short)out;
        }

        pos += NS_HOP_SIZE;
    }
}
