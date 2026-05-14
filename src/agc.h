#ifndef AGC_H
#define AGC_H

#include <stdint.h>

typedef struct agc_t agc_t;

agc_t* agc_create(int sample_rate, int target_level_dbfs);
void   agc_destroy(agc_t *agc);
void   agc_process(agc_t *agc, short *frame, int samples);

#endif
