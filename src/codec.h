#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>

typedef struct codec_enc codec_enc_t;
typedef struct codec_dec codec_dec_t;

codec_enc_t* codec_enc_create(int sample_rate, int channels);
void         codec_enc_destroy(codec_enc_t *e);
int          codec_enc_encode(codec_enc_t *e, const short *pcm, int frame_size, uint8_t *out, int max_out);
void         codec_enc_set_bitrate(codec_enc_t *e, int bitrate);

codec_dec_t* codec_dec_create(int sample_rate, int channels);
void         codec_dec_destroy(codec_dec_t *d);
int          codec_dec_decode(codec_dec_t *d, const uint8_t *data, int len, short *pcm, int frame_size, int fec);

#endif
