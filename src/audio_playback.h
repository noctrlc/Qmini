#ifndef AUDIO_PLAYBACK_H
#define AUDIO_PLAYBACK_H

#include <windows.h>

typedef struct {
    short  *buffer;
    int     capacity;
    int     frames;
    int     read_pos;
} speaker_t;

typedef struct {
    int     running;
    HANDLE  thread;
    short   *mix_buf;
    int     mix_buf_frames;
    /* ring buffer: main thread pushes mixed audio, playback thread consumes */
    volatile LONG  rb_head;
    volatile LONG  rb_tail;
    short   rb_data[16384];      /* 1024ms @ 16kHz, power of 2 */
    void    *capture;            /* audio_capture_t* for far-ref callback */
} audio_playback_t;

#define RB_MASK 16383

int  audio_playback_start(audio_playback_t *ap);
void audio_playback_stop(audio_playback_t *ap);
void audio_playback_mix(audio_playback_t *ap, speaker_t speakers[], int nspeakers, short *out, int frames);
void audio_playback_submit(audio_playback_t *ap, const short *samples, int frames);
void audio_playback_set_capture(audio_playback_t *ap, void *capture);
const char* audio_playback_get_device_name(void);

#endif
