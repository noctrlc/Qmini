#ifndef AEC_H
#define AEC_H

#include <stdint.h>

#define AEC_SAMPLE_RATE    16000
#define AEC_FRAME_SIZE     160    /* 10ms @ 16kHz */
#define AEC_TAIL_LENGTH    1024   /* 64ms tail */

typedef struct aec_t aec_t;

aec_t* aec_create(void);
void aec_destroy(aec_t *aec);
void aec_process(aec_t *aec, const short *near_in, const short *far_ref, short *near_out);
void aec_reset(aec_t *aec);

#endif
