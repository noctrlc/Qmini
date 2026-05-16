#ifndef AEC_H
#define AEC_H

#include <stdint.h>

#define AEC_SAMPLE_RATE    16000
#define AEC_FRAME_SIZE     160    /* 10ms @ 16kHz */
#define AEC_TAIL_LENGTH    1024   /* 64ms max tail (filter taps) */
#define AEC_DELAY_MAX      3200   /* 200ms max search range */

/* Metrics for diagnostics */
typedef struct {
    float erle_db;           /* Echo Return Loss Enhancement */
    float filter_norm;       /* L2 norm of weights (convergence) */
    int   double_talk;       /* 1 = DTD active in last frame */
    int   delay_samples;     /* estimated acoustic delay */
    int   delay_confirmed;   /* 1 once delay estimation converged */
} aec_metrics_t;

typedef struct aec_t aec_t;

aec_t* aec_create(void);
void aec_destroy(aec_t *aec);
void aec_process(aec_t *aec, const short *near_in, const short *far_ref, short *near_out);
void aec_reset(aec_t *aec);
void aec_get_metrics(aec_t *aec, aec_metrics_t *m);

#endif
