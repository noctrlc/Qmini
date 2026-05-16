#include "audio_playback.h"
#include "audio_capture.h"
#include "logger.h"
#include <stdlib.h>
#include <stdio.h>
#include <mmsystem.h>
#include <mmreg.h>

#pragma comment(lib, "winmm.lib")

static char g_play_dev_name[128] = "Default";

#define PLAY_BUF_FRAMES 640  /* 40ms @ 16kHz */
#define PLAY_BUF_COUNT  4  /* extra buffer for safety */

static HWAVEOUT       g_waveout = NULL;
static WAVEHDR        g_wavehdr[PLAY_BUF_COUNT];
static short          g_wavebuf[PLAY_BUF_COUNT][PLAY_BUF_FRAMES];
static LONG   g_buf_ready[PLAY_BUF_COUNT]; /* 1=in queue, 0=available */
static audio_playback_t *g_play_ap = NULL;
static HANDLE         g_play_thread = NULL;
static volatile int   g_play_thread_run = 0;
static volatile int   g_stopping = 0;

const char* audio_playback_get_device_name(void) {
    return g_play_dev_name;
}

static DWORD WINAPI play_thread(LPVOID arg) {
    audio_playback_t *ap = (audio_playback_t*)arg;
    while (g_play_thread_run) {
        __try {
        if (g_stopping) { Sleep(10); continue; }
        int wrote = 0;
        for (int i = 0; i < PLAY_BUF_COUNT; i++) {
            if (!g_stopping && !g_buf_ready[i] && g_waveout) {
                /* Fill this buffer from ring buffer */
                short *buf = g_wavebuf[i];
                int any_data = 0;
                for (int j = 0; j < PLAY_BUF_FRAMES; j++) {
                    LONG head = ap->rb_head;
                    LONG tail = ap->rb_tail;
                    if (head != tail) {
                        buf[j] = ap->rb_data[tail & RB_MASK];
                        MemoryBarrier();
                        ap->rb_tail = tail + 1;
                        any_data = 1;
                    } else {
                        buf[j] = 0;
                    }
                }
                InterlockedExchange(&g_buf_ready[i], 1);
                waveOutPrepareHeader(g_waveout, &g_wavehdr[i], sizeof(WAVEHDR));
                waveOutWrite(g_waveout, &g_wavehdr[i], sizeof(WAVEHDR));
                wrote = 1;
            }
        }
        if (!wrote) {
            Sleep(5);
        }
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            LOG_ERROR("CRASH in play_thread! code=0x%08X", GetExceptionCode());
            Sleep(10);
        }
    }
    return 0;
}

static void CALLBACK waveout_cb(HWAVEOUT hwo, UINT msg, DWORD_PTR inst, DWORD_PTR param1, DWORD_PTR param2) {
    (void)hwo; (void)inst; (void)param2;
    if (msg != WOM_DONE) return;
    __try {
    if (g_stopping) return;
    /* Mark this buffer as available */
    for (int i = 0; i < PLAY_BUF_COUNT; i++) {
        if (&g_wavehdr[i] == (WAVEHDR*)param1) {
            InterlockedExchange(&g_buf_ready[i], 0);
            break;
        }
    }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        /* Silently ignore — can't log from driver callback safely */
    }
}

void audio_playback_set_capture(audio_playback_t *ap, void *capture) {
    ap->capture = capture;
}

void audio_playback_submit(audio_playback_t *ap, const short *samples, int frames) {
    if (ap->capture)
        audio_capture_set_far_ref((audio_capture_t*)ap->capture, samples, frames);
    for (int i = 0; i < frames; i++) {
        LONG head = ap->rb_head;
        LONG tail = ap->rb_tail;
        if (head - tail >= RB_MASK + 1) break;
        ap->rb_data[head & RB_MASK] = samples[i];
        MemoryBarrier();
        ap->rb_head = head + 1;
    }
}

int audio_playback_start(audio_playback_t *ap) {
    ap->running = 1;
    ap->mix_buf = (short*)HeapAlloc(GetProcessHeap(), 0, PLAY_BUF_FRAMES * sizeof(short));
    if (!ap->mix_buf) return 0;
    ap->mix_buf_frames = PLAY_BUF_FRAMES;
    ap->rb_head = 0;
    ap->rb_tail = 0;
    g_play_ap = ap;

    WAVEFORMATEX wfx;
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = 1;
    wfx.nSamplesPerSec  = 16000;
    wfx.nAvgBytesPerSec = 32000;
    wfx.nBlockAlign     = 2;
    wfx.wBitsPerSample  = 16;
    wfx.cbSize          = 0;

    MMRESULT mmr = waveOutOpen(&g_waveout, WAVE_MAPPER, &wfx,
                               (DWORD_PTR)waveout_cb, 0,
                               CALLBACK_FUNCTION);
    if (mmr != MMSYSERR_NOERROR) {
        HeapFree(GetProcessHeap(), 0, ap->mix_buf);
        ap->mix_buf = NULL;
        ap->running = 0;
        return 0;
    }

    /* Prepare all buffers */
    for (int i = 0; i < PLAY_BUF_COUNT; i++) {
        memset(g_wavebuf[i], 0, sizeof(g_wavebuf[i]));
        g_wavehdr[i].lpData         = (LPSTR)g_wavebuf[i];
        g_wavehdr[i].dwBufferLength = PLAY_BUF_FRAMES * sizeof(short);
        g_wavehdr[i].dwFlags        = 0;
        g_wavehdr[i].dwLoops        = 0;
        waveOutPrepareHeader(g_waveout, &g_wavehdr[i], sizeof(WAVEHDR));
        g_buf_ready[i] = 0; /* initially available */
    }

    /* Start playback thread - it will fill and write all buffers */
    g_play_thread_run = 1;
    g_play_thread = CreateThread(NULL, 0, play_thread, ap, 0, NULL);

    return 1;
}

void audio_playback_stop(audio_playback_t *ap) {
    g_stopping = 1;           /* signal play_thread to stop using waveOut */
    g_play_thread_run = 0;
    if (g_play_thread) {
        WaitForSingleObject(g_play_thread, 3000);
        CloseHandle(g_play_thread);
        g_play_thread = NULL;
    }
    ap->running = 0;
    g_play_ap = NULL;
    if (g_waveout) {
        waveOutReset(g_waveout);
        for (int i = 0; i < PLAY_BUF_COUNT; i++) {
            waveOutUnprepareHeader(g_waveout, &g_wavehdr[i], sizeof(WAVEHDR));
        }
        waveOutClose(g_waveout);
        g_waveout = NULL;
    }
    g_stopping = 0;
    if (ap->mix_buf) {
        HeapFree(GetProcessHeap(), 0, ap->mix_buf);
        ap->mix_buf = NULL;
    }
}
