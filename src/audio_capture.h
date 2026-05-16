#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <windows.h>

typedef void (*audio_capture_cb)(const short *samples, int count, void *user);

typedef struct {
    int     running;
    void    *user_data;
    audio_capture_cb callback;
    /* Audio processing pipeline (opaque pointers, set by main) */
    void    *aec;
    void    *agc;
    void    *ns;
    short   aec_ref_buf[480];  /* far-end reference for AEC (matches CAPTURE_BUF_FRAMES) */
} audio_capture_t;

int  audio_capture_start(audio_capture_t *ac, audio_capture_cb cb, void *user);
void audio_capture_stop(audio_capture_t *ac);
const char* audio_capture_get_device_name(void);

/* Set audio processing pipeline (called once from main) */
void audio_capture_set_pipeline(audio_capture_t *ac, void *aec, void *agc, void *ns);

/* Provide far-end reference signal for echo cancellation (called from playback) */
void audio_capture_set_far_ref(audio_capture_t *ac, const short *samples, int frames);

#endif
