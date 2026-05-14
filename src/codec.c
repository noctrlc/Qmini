#include "codec.h"
#include <opus.h>
#include <stdlib.h>

struct codec_enc {
    OpusEncoder *enc;
};

codec_enc_t* codec_enc_create(int sample_rate, int channels) {
    codec_enc_t *e = (codec_enc_t*)calloc(1, sizeof(codec_enc_t));
    if (!e) return NULL;
    int err;
    e->enc = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) { free(e); return NULL; }
    opus_encoder_ctl(e->enc, OPUS_SET_BITRATE(32000));
    opus_encoder_ctl(e->enc, OPUS_SET_COMPLEXITY(8));
    opus_encoder_ctl(e->enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(e->enc, OPUS_SET_DTX(1));
    opus_encoder_ctl(e->enc, OPUS_SET_PACKET_LOSS_PERC(15));
    opus_encoder_ctl(e->enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(e->enc, OPUS_SET_VBR(1));
    opus_encoder_ctl(e->enc, OPUS_SET_VBR_CONSTRAINT(0));
    return e;
}

void codec_enc_destroy(codec_enc_t *e) {
    if (e) {
        if (e->enc) opus_encoder_destroy(e->enc);
        free(e);
    }
}

int codec_enc_encode(codec_enc_t *e, const short *pcm, int frame_size, uint8_t *out, int max_out) {
    return opus_encode(e->enc, pcm, frame_size, out, max_out);
}

void codec_enc_set_bitrate(codec_enc_t *e, int bitrate) {
    opus_encoder_ctl(e->enc, OPUS_SET_BITRATE(bitrate));
}

struct codec_dec {
    OpusDecoder *dec;
};

codec_dec_t* codec_dec_create(int sample_rate, int channels) {
    codec_dec_t *d = (codec_dec_t*)calloc(1, sizeof(codec_dec_t));
    if (!d) return NULL;
    int err;
    d->dec = opus_decoder_create(sample_rate, channels, &err);
    if (err != OPUS_OK) { free(d); return NULL; }
    return d;
}

void codec_dec_destroy(codec_dec_t *d) {
    if (d) {
        if (d->dec) opus_decoder_destroy(d->dec);
        free(d);
    }
}

int codec_dec_decode(codec_dec_t *d, const uint8_t *data, int len, short *pcm, int frame_size, int fec) {
    if (data == NULL || len <= 0) {
        return opus_decode(d->dec, NULL, 0, pcm, frame_size, fec ? 1 : 0);
    }
    return opus_decode(d->dec, data, len, pcm, frame_size, fec ? 1 : 0);
}
