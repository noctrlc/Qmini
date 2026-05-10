#include "audio_capture.h"
#include <stdio.h>
#include <stdlib.h>
#include <mmsystem.h>
#include <mmreg.h>

#pragma comment(lib, "winmm.lib")

#define CAPTURE_BUF_FRAMES 960   /* 60ms @ 16kHz */
#define CAPTURE_BUF_COUNT  4

static char g_cap_dev_name[128] = "Default";
static HWAVEIN g_wavein = NULL;
static WAVEHDR g_wavehdr[CAPTURE_BUF_COUNT];
static short g_wavebuf[CAPTURE_BUF_COUNT][CAPTURE_BUF_FRAMES];

const char* audio_capture_get_device_name(void) {
    return g_cap_dev_name;
}

static void CALLBACK wavein_cb(HWAVEIN hwi, UINT msg, DWORD_PTR inst, DWORD_PTR param1, DWORD_PTR param2) {
    (void)hwi; (void)param2;
    if (msg != WIM_DATA) return;
    audio_capture_t *ac = (audio_capture_t*)inst;
    if (!ac || !ac->running || !ac->callback) return;

    WAVEHDR *hdr = (WAVEHDR*)param1;

    /* Callback with the captured samples */
    int frames = hdr->dwBytesRecorded / sizeof(short);
    ac->callback((const short*)hdr->lpData, frames, ac->user_data);

    /* Re-queue the buffer */
    waveInAddBuffer(g_wavein, hdr, sizeof(WAVEHDR));
}

int audio_capture_start(audio_capture_t *ac, audio_capture_cb cb, void *user) {
    ac->user_data = user;
    ac->callback = cb;
    ac->running = 1;

    WAVEFORMATEX wfx;
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = 1;
    wfx.nSamplesPerSec  = 16000;
    wfx.nAvgBytesPerSec = 32000;
    wfx.nBlockAlign     = 2;
    wfx.wBitsPerSample  = 16;
    wfx.cbSize          = 0;

    MMRESULT mmr = waveInOpen(&g_wavein, WAVE_MAPPER, &wfx,
                              (DWORD_PTR)wavein_cb, (DWORD_PTR)ac,
                              CALLBACK_FUNCTION);
    if (mmr != MMSYSERR_NOERROR) {
        ac->running = 0;
        return 0;
    }

    /* Prepare and queue buffers */
    for (int i = 0; i < CAPTURE_BUF_COUNT; i++) {
        g_wavehdr[i].lpData         = (LPSTR)g_wavebuf[i];
        g_wavehdr[i].dwBufferLength = CAPTURE_BUF_FRAMES * sizeof(short);
        g_wavehdr[i].dwFlags        = 0;
        g_wavehdr[i].dwLoops        = 0;
        waveInPrepareHeader(g_wavein, &g_wavehdr[i], sizeof(WAVEHDR));
        waveInAddBuffer(g_wavein, &g_wavehdr[i], sizeof(WAVEHDR));
    }

    waveInStart(g_wavein);
    return 1;
}

void audio_capture_stop(audio_capture_t *ac) {
    ac->running = 0;
    if (g_wavein) {
        waveInReset(g_wavein);
        for (int i = 0; i < CAPTURE_BUF_COUNT; i++) {
            waveInUnprepareHeader(g_wavein, &g_wavehdr[i], sizeof(WAVEHDR));
        }
        waveInClose(g_wavein);
        g_wavein = NULL;
    }
}
