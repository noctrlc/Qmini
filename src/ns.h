#ifndef NS_H
#define NS_H

#include <stdint.h>

typedef struct ns_t ns_t;

ns_t* ns_create(int sample_rate);
void  ns_destroy(ns_t *ns);
void  ns_process(ns_t *ns, short *frame, int samples);

#endif
