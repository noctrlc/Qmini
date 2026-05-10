#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <windows.h>

typedef void (*audio_capture_cb)(const short *samples, int count, void *user);

typedef struct {
    int     running;
    HANDLE  thread;
    void    *user_data;
    audio_capture_cb callback;
} audio_capture_t;

int  audio_capture_start(audio_capture_t *ac, audio_capture_cb cb, void *user);
void audio_capture_stop(audio_capture_t *ac);
/* Get friendly name of the active capture device */
const char* audio_capture_get_device_name(void);
#endif
