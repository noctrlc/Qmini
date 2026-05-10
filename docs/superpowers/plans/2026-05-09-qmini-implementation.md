# Qmini Voice Chat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a complete, working ultra-low-memory voice chat application for 5-10 person gaming on Windows.

**Architecture:** Single-process Windows app using WASAPI for audio, Opus for codec, P2P UDP + TURN fallback for networking, system tray + global hotkeys for UI. All modules pre-allocate fixed-size buffers, no runtime alloc in hot paths.

**Tech Stack:** C + MSVC 2019 + Windows SDK 10.0 + libopus (source build)

**File Structure:**
```
D:\Qmini\
├── build.bat                  # One-click build (downloads opus, compiles everything)
├── src/
│   ├── main.c                 # Entry point, message loop, module lifecycle
│   ├── audio_capture.c/h      # WASAPI capture, 16kHz mono
│   ├── audio_playback.c/h     # WASAPI render + multi-stream mixing
│   ├── codec.c/h              # Opus encode/decode wrapper
│   ├── jitter_buffer.c/h      # Adaptive jitter buffer (pre-allocated)
│   ├── network.c/h            # UDP P2P + ICE + TURN client
│   ├── signaling.c/h          # Signaling protocol client
│   ├── tray.c/h               # System tray icon + menu
│   ├── hotkey.c/h             # Global hotkeys (RegisterHotKey)
│   ├── config.c/h             # INI-style config read/write
│   ├── ringbuf.h              # Lock-free SPSC ring buffer (header-only)
│   ├── miniaudio.h            # Lightweight header-only audio abstraction
│   └── opus/                  # libopus source (downloaded by build.bat)
├── signaling_server/
│   ├── signaling_server.c     # Standalone signaling server (TCP)
│   └── build.bat              # Build script for server
└── docs/
    └── superpowers/
        └── specs/
            └── 2026-05-08-qmini-voice-chat-design.md
```

---

### Task 1: Build Script + Project Scaffold

**Files:**
- Create: `D:\Qmini\build.bat`
- Create: `D:\Qmini\src\`

- [ ] **Step 1: Create build.bat**

The build script must:
1. Set up MSVC environment via `vcvarsall.bat`
2. Download libopus source (if not already present)
3. Compile opus sources to static lib
4. Compile all Qmini sources
5. Link into final qmini.exe

```bat
@echo off
setlocal

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
set OPUS_URL=https://archive.mozilla.org/pub/opus/opus-1.5.2.tar.gz
set OPUS_DIR=src\opus
set SDK_INC=C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0
set SDK_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0
set WINVER=/D_WIN32_WINNT=0x0600

call %VCVARS% x64

if not exist %OPUS_DIR%\config.h (
    echo == Downloading libopus...
    if not exist opus.tar.gz (
        powershell -Command "[Net.ServicePointManager]::SecurityProtocol = 'tls12'; Invoke-WebRequest -Uri '%OPUS_URL%' -OutFile 'opus.tar.gz'"
    )
    echo == Extracting opus...
    tar -xzf opus.tar.gz -C src\
    move src\opus-1.5.2 %OPUS_DIR%
    echo #define PACKAGE_VERSION \"1.5.2\" > %OPUS_DIR%\config.h
    echo #define OPUS_BUILD >> %OPUS_DIR%\config.h
    echo #define HAVE_LRINTF 1 >> %OPUS_DIR%\config.h
    echo #define HAVE_LRINT 1 >> %OPUS_DIR%\config.h
)

set OPUS_SRC=%OPUS_DIR%\celt\bands.c %OPUS_DIR%\celt\celt.c %OPUS_DIR%\celt\celt_decoder.c %OPUS_DIR%\celt\celt_encoder.c %OPUS_DIR%\celt\celt_lpc.c %OPUS_DIR%\celt\entcode.c %OPUS_DIR%\celt\entdec.c %OPUS_DIR%\celt\entenc.c %OPUS_DIR%\celt\kiss_fft.c %OPUS_DIR%\celt\laplace.c %OPUS_DIR%\celt\mathops.c %OPUS_DIR%\celt\mdct.c %OPUS_DIR%\celt\modes.c %OPUS_DIR%\celt\pitch.c %OPUS_DIR%\celt\celt_lpc.c %OPUS_DIR%\celt\quant_bands.c %OPUS_DIR%\celt\rate.c %OPUS_DIR%\celt\vq.c %OPUS_DIR%\silk\A2NLSF.c %OPUS_DIR%\silk\dec_API.c %OPUS_DIR%\silk\enc_API.c

echo == Compiling opus...
cl /nologo /O1 /MT /c /I%OPUS_DIR%\ /I%OPUS_DIR%\celt\ /I%OPUS_DIR%\silk\ /I%OPUS_DIR%\silk\float\ /Foopus.obj %OPUS_SRC% /DHAVE_CONFIG_H
lib /nologo /out:opus.lib opus.obj

echo == Compiling Qmini...
set CFLAGS=/nologo /O1 /MT /W3 /I. /I%OPUS_DIR% /I%SDK_INC%\um /I%SDK_INC%\shared /I%SDK_INC%\winrt /DWIN32_LEAN_AND_MEAN %WINVER%
set QMINI_SRC=src\main.c src\audio_capture.c src\audio_playback.c src\codec.c src\jitter_buffer.c src\network.c src\signaling.c src\tray.c src\hotkey.c src\config.c
set LIBS=opus.lib user32.lib gdi32.lib ole32.lib shell32.lib ws2_32.lib winmm.lib

cl %CFLAGS% /Foqmini.obj /Feqmini.exe %QMINI_SRC% /link %LIBS% /subsystem:windows /LTCG

echo == Build complete: qmini.exe
```

- [ ] **Step 2: Create .gitignore**

```gitignore
*.obj
*.lib
*.exe
*.tar.gz
src/opus/
opus.lib
```

---

### Task 2: Ring Buffer

**Files:**
- Create: `D:\Qmini\src\ringbuf.h`

A single-producer single-consumer lock-free ring buffer. Used by audio capture to push samples to the codec thread, and by network receive to push decoded audio to the mixer.

```c
#ifndef RINGBUF_H
#define RINGBUF_H

#include <stdint.h>
#include <stddef.h>
#include <windows.h>

typedef struct {
    uint8_t *buf;
    size_t   size;       /* power of 2 */
    size_t   mask;
    volatile LONG head;  /* producer index */
    volatile LONG tail;  /* consumer index */
} ringbuf_t;

/* Initialize ring buffer. size MUST be power of 2. buf must remain valid. */
static __inline void ringbuf_init(ringbuf_t *rb, uint8_t *buf, size_t size) {
    rb->buf  = buf;
    rb->size = size;
    rb->mask = size - 1;
    rb->head = 0;
    rb->tail = 0;
}

static __inline size_t ringbuf_avail(ringbuf_t *rb) {
    return ((size_t)(rb->head - rb->tail)) & rb->mask;
}

static __inline size_t ringbuf_space(ringbuf_t *rb) {
    return rb->size - 1 - ((size_t)(rb->head - rb->tail)) & rb->mask;
}

/* Push len bytes. Returns bytes written (less than len if full). */
static __inline size_t ringbuf_push(ringbuf_t *rb, const uint8_t *data, size_t len) {
    size_t i;
    LONG head = rb->head;
    for (i = 0; i < len; i++) {
        size_t avail = ((size_t)(head - rb->tail)) & rb->mask;
        if (avail >= rb->size - 1) break; /* full */
        rb->buf[head] = data[i];
        head = (head + 1) & rb->mask;
    }
    rb->head = head;
    return i;
}

/* Pop len bytes. Returns bytes read (less than len if empty). */
static __inline size_t ringbuf_pop(ringbuf_t *rb, uint8_t *data, size_t len) {
    size_t i;
    LONG tail = rb->tail;
    for (i = 0; i < len; i++) {
        size_t avail = ((size_t)(rb->head - tail)) & rb->mask;
        if (avail == 0) break; /* empty */
        data[i] = rb->buf[tail];
        tail = (tail + 1) & rb->mask;
    }
    rb->tail = tail;
    return i;
}

/* Peek at the next byte without consuming */
static __inline int ringbuf_peek(ringbuf_t *rb, uint8_t *byte) {
    if (rb->head == rb->tail) return 0;
    *byte = rb->buf[rb->tail];
    return 1;
}

#endif
```

---

### Task 3: Config Module

**Files:**
- Create: `D:\Qmini\src\config.c`
- Create: `D:\Qmini\src\config.h`

Reads/writes a simple INI-style config file at `%APPDATA%\Qmini\config.ini`.

```c
// config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include <stdint.h>

typedef struct {
    char  server_addr[64];    /* signaling server host:port */
    char  nickname[32];       /* display name */
    int   ptt_key;            /* virtual key code for PTT */
    int   mute_key;           /* virtual key code for mute toggle */
    int   enable_fec;         /* 0/1 enable FEC */
} config_t;

int  config_load(config_t *cfg);
int  config_save(config_t *cfg);

#endif
```

```c
// config.c
#include "config.h"
#include <shlobj.h>  /* SHGetFolderPathA */

static const char *DEFAULT_SERVER = "127.0.0.1:9800";
static const char *DEFAULT_NAME   = "Player";

static void config_set_defaults(config_t *cfg) {
    lstrcpyA(cfg->server_addr, DEFAULT_SERVER);
    lstrcpyA(cfg->nickname, DEFAULT_NAME);
    cfg->ptt_key  = VK_XBUTTON1;  /* mouse side button */
    cfg->mute_key = VK_F13;        /* or VK_F13, user can rebind */
    cfg->enable_fec = 1;
}

static void get_path(char *path, size_t sz) {
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path);
    lstrcatA(path, "\\Qmini");
    CreateDirectoryA(path, NULL);
    lstrcatA(path, "\\config.ini");
}

int config_load(config_t *cfg) {
    char path[MAX_PATH];
    char buf[256];
    config_set_defaults(cfg);
    get_path(path, sizeof(path));

    if (GetPrivateProfileStringA("qmini", "server", DEFAULT_SERVER, cfg->server_addr, sizeof(cfg->server_addr), path) <= 0)
        lstrcpyA(cfg->server_addr, DEFAULT_SERVER);
    if (GetPrivateProfileStringA("qmini", "nickname", DEFAULT_NAME, cfg->nickname, sizeof(cfg->nickname), path) <= 0)
        lstrcpyA(cfg->nickname, DEFAULT_NAME);
    cfg->ptt_key    = GetPrivateProfileIntA("qmini", "ptt_key", VK_XBUTTON1, path);
    cfg->mute_key   = GetPrivateProfileIntA("qmini", "mute_key", VK_F13, path);
    cfg->enable_fec = GetPrivateProfileIntA("qmini", "enable_fec", 1, path);
    return 1;
}

int config_save(config_t *cfg) {
    char path[MAX_PATH];
    char val[16];
    get_path(path, sizeof(path));
    WritePrivateProfileStringA("qmini", "server", cfg->server_addr, path);
    WritePrivateProfileStringA("qmini", "nickname", cfg->nickname, path);
    wsprintfA(val, "%d", cfg->ptt_key);  WritePrivateProfileStringA("qmini", "ptt_key", val, path);
    wsprintfA(val, "%d", cfg->mute_key); WritePrivateProfileStringA("qmini", "mute_key", val, path);
    wsprintfA(val, "%d", cfg->enable_fec); WritePrivateProfileStringA("qmini", "enable_fec", val, path);
    return 1;
}
```

---

### Task 4: Audio Capture (WASAPI)

**Files:**
- Create: `D:\Qmini\src\audio_capture.c`
- Create: `D:\Qmini\src\audio_capture.h`

Captures audio from the default input device at 16kHz, 16-bit mono. Uses WASAPI event-driven mode. Output goes into a ring buffer for the codec thread to pick up.

```c
// audio_capture.h
#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <windows.h>

typedef void (*audio_capture_cb)(const short *samples, int count, void *user);

typedef struct {
    int     running;
    HANDLE  thread;
    void    *user;
    audio_capture_cb callback;
} audio_capture_t;

/* Start capture. callback is called from a background thread. */
int  audio_capture_start(audio_capture_t *ac, audio_capture_cb cb, void *user);
void audio_capture_stop(audio_capture_t *ac);

#endif
```

```c
// audio_capture.c
#include "audio_capture.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#define REFTIMES_PER_SEC 10000000
#define REFTIMES_PER_MS  10000

static const CLSID CLSID_MMDeviceEnumerator = {0xbcde0395,0xe52f,0x467c,0x8e,0x3d,0xc4,0x57,0x92,0x91,0x69,0x2e};
static const IID IID_IMMDeviceEnumerator    = {0xa95664d2,0xdf14,0x4faf,0xa2,0x5f,0xbe,0x47,0x23,0xae,0x0e,0x63};
static const IID IID_IAudioClient           = {0x1cb9ad4c,0xdb7c,0x4a5d,0x8f,0x7e,0x24,0xba,0x5b,0x9b,0x0f,0x68};
static const IID IID_IAudioCaptureClient    = {0xc8adbd64,0xe71e,0x48a0,0xa4,0xde,0x18,0x5c,0x39,0x5c,0x96,0xee};

typedef struct capture_ctx {
    audio_capture_t *ac;
    IMMDevice       *device;
    IAudioClient    *client;
    IAudioCaptureClient *capture;
    HANDLE           event;
} capture_ctx_t;

static DWORD WINAPI capture_thread(LPVOID arg) {
    capture_ctx_t *ctx = (capture_ctx_t*)arg;
    UINT32 frames_avail;
    BYTE *data;
    DWORD flags;
    short buf[960]; /* max 20ms @ 48kHz, we'll downsample to 16kHz */

    while (ctx->ac->running) {
        WaitForSingleObject(ctx->event, INFINITE);
        if (!ctx->ac->running) break;

        while (1) {
            HRESULT hr = IAudioCaptureClient_GetBuffer(ctx->capture, &data, &frames_avail, &flags, NULL, NULL);
            if (hr != S_OK) break;

            /* GetBuffer returns float samples at device format (typically 48kHz stereo)
               We convert to 16-bit mono 16kHz by simple decimation.
               For simplicity, take left channel and decimate 48k->16k (skip 2:1 ratio effectively) */
            float *fdata = (float*)data;
            int out_count = frames_avail / 3; /* 48k/16k = 3 */
            if (out_count > 960) out_count = 960;

            for (int i = 0; i < out_count; i++) {
                float sample = fdata[i * 3 * 2]; /* *3 for decimation, *2 for stereo-left */
                if (sample > 1.0f) sample = 1.0f;
                if (sample < -1.0f) sample = -1.0f;
                buf[i] = (short)(sample * 32767.0f);
            }

            if (ctx->ac->callback)
                ctx->ac->callback(buf, out_count, ctx->ac->user);

            IAudioCaptureClient_ReleaseBuffer(ctx->capture, frames_avail);
        }
    }
    return 0;
}

int audio_capture_start(audio_capture_t *ac, audio_capture_cb cb, void *user) {
    HRESULT hr;
    IMMDeviceEnumerator *enumerator = NULL;
    capture_ctx_t *ctx;
    WAVEFORMATEX *pwfx = NULL;
    WAVEFORMATEX wfx_requested;

    ctx = (capture_ctx_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(capture_ctx_t));
    if (!ctx) return 0;
    ctx->ac = ac;
    ac->user = user;
    ac->callback = cb;
    ac->running = 1;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&enumerator);
    if (FAILED(hr)) goto fail;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eCapture, eCommunications, &ctx->device);
    if (FAILED(hr) && FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eCapture, eConsole, &ctx->device)))
        goto fail;

    hr = IMMDevice_Activate(ctx->device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&ctx->client);
    if (FAILED(hr)) goto fail;

    /* Request 16kHz mono 16-bit PCM */
    wfx_requested.wFormatTag      = WAVE_FORMAT_PCM;
    wfx_requested.nChannels       = 1;
    wfx_requested.nSamplesPerSec  = 16000;
    wfx_requested.nAvgBytesPerSec = 16000 * 2;
    wfx_requested.nBlockAlign     = 2;
    wfx_requested.wBitsPerSample  = 16;
    wfx_requested.cbSize          = 0;

    hr = IAudioClient_Initialize(ctx->client, AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK, REFTIMES_PER_SEC, 0, &wfx_requested, NULL);
    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) {
        /* Fallback: get mix format and do conversion manually */
        IAudioClient_GetMixFormat(ctx->client, &pwfx);
        hr = IAudioClient_Initialize(ctx->client, AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK, REFTIMES_PER_SEC, 0, pwfx, NULL);
        if (FAILED(hr)) goto fail;
    }
    if (FAILED(hr)) goto fail;

    ctx->event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!ctx->event) goto fail;
    IAudioClient_SetEventHandle(ctx->client, ctx->event);

    hr = IAudioClient_GetService(ctx->client, &IID_IAudioCaptureClient, (void**)&ctx->capture);
    if (FAILED(hr)) goto fail;

    if (pwfx) CoTaskMemFree(pwfx);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);

    IAudioClient_Start(ctx->client);

    ac->thread = CreateThread(NULL, 0, capture_thread, ctx, 0, NULL);
    return 1;

fail:
    ac->running = 0;
    if (ctx->event) CloseHandle(ctx->event);
    if (ctx->capture) IAudioCaptureClient_Release(ctx->capture);
    if (ctx->client) IAudioClient_Release(ctx->client);
    if (ctx->device) IMMDevice_Release(ctx->device);
    if (pwfx) CoTaskMemFree(pwfx);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    HeapFree(GetProcessHeap(), 0, ctx);
    return 0;
}

void audio_capture_stop(audio_capture_t *ac) {
    ac->running = 0;
    if (ac->thread) {
        WaitForSingleObject(ac->thread, 3000);
        CloseHandle(ac->thread);
        ac->thread = NULL;
    }
}
```

---

### Task 5: Audio Playback + Mixer

**Files:**
- Create: `D:\Qmini\src\audio_playback.c`
- Create: `D:\Qmini\src\audio_playback.h`

Outputs mixed audio via WASAPI render. Accepts PCM frames from multiple sources (each remote user) and mixes them.

```c
// audio_playback.h
#ifndef AUDIO_PLAYBACK_H
#define AUDIO_PLAYBACK_H

#include <windows.h>

/* Max remote speakers */
#define MAX_SPEAKERS 10

typedef struct {
    int     running;
    HANDLE  thread;
    short   *mix_buf;   /* temp mixing buffer */
    int     mix_buf_frames;
} audio_playback_t;

/* Each speaker provides its own buffer. Mixer sums all into one output. */
typedef struct {
    short  *buffer;    /* circular buffer of decoded samples */
    int     frames;    /* number of valid frames in buffer */
    int     read_pos;  /* current read position */
} speaker_t;

int  audio_playback_start(audio_playback_t *ap);
void audio_playback_stop(audio_playback_t *ap);

/* Mix all speakers and output. Called internally from playback thread. */
void audio_playback_mix(audio_playback_t *ap, speaker_t speakers[], int nspeakers, short *out, int frames);

#endif
```

```c
// audio_playback.c
#include "audio_playback.h"
#include <mmdeviceapi.h>
#include <audioclient.h>

static const IID IID_IAudioRenderClient = {0xf294acfc,0x3144,0x4483,0xa0,0xbf,0xbc,0xca,0x26,0x0e,0x67,0x27};

#define REFTIMES_PER_SEC 10000000

void audio_playback_mix(audio_playback_t *ap, speaker_t speakers[], int nspeakers, short *out, int frames) {
    int i, j;
    for (i = 0; i < frames; i++) {
        int sum = 0;
        for (j = 0; j < nspeakers; j++) {
            if (speakers[j].frames > 0) {
                sum += speakers[j].buffer[speakers[j].read_pos];
                speakers[j].read_pos++;
                speakers[j].frames--;
            }
        }
        /* Clamp */
        if (sum > 32767) sum = 32767;
        if (sum < -32768) sum = -32768;
        out[i] = (short)sum;
    }
}

static DWORD WINAPI playback_thread(LPVOID arg) {
    audio_playback_t *ap = (audio_playback_t*)arg;
    HRESULT hr;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *client = NULL;
    IAudioRenderClient *render = NULL;
    HANDLE event = NULL;
    WAVEFORMATEX wfx;
    UINT32 buffer_frames;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void**)&enumerator);
    if (FAILED(hr)) goto cleanup;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eCommunications, &device);
    if (FAILED(hr))
        hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
    if (FAILED(hr)) goto cleanup;

    hr = IMMDevice_Activate(device, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&client);
    if (FAILED(hr)) goto cleanup;

    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = 1;
    wfx.nSamplesPerSec  = 16000;
    wfx.nAvgBytesPerSec = 16000 * 2;
    wfx.nBlockAlign     = 2;
    wfx.wBitsPerSample  = 16;
    wfx.cbSize          = 0;

    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
        0, 0, &wfx, NULL);
    if (FAILED(hr)) goto cleanup;

    IAudioClient_GetBufferSize(client, &buffer_frames);

    event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!event) goto cleanup;
    IAudioClient_SetEventHandle(client, event);

    hr = IAudioClient_GetService(client, &IID_IAudioRenderClient, (void**)&render);
    if (FAILED(hr)) goto cleanup;

    IAudioClient_Start(client);

    while (ap->running) {
        WaitForSingleObject(event, INFINITE);
        if (!ap->running) break;

        UINT32 pad;
        IAudioClient_GetCurrentPadding(client, &pad);
        UINT32 avail = buffer_frames - pad;
        if (avail == 0) continue;

        BYTE *buf;
        hr = IAudioRenderClient_GetBuffer(render, avail, &buf);
        if (FAILED(hr)) continue;

        /* For now: fill with silence. Actual mixing is done from main integration. */
        memset(buf, 0, avail * 2);
        IAudioRenderClient_ReleaseBuffer(render, avail, 0);
    }

cleanup:
    if (client) { IAudioClient_Stop(client); IAudioClient_Release(client); }
    if (render) IAudioRenderClient_Release(render);
    if (event) CloseHandle(event);
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    CoUninitialize();
    return 0;
}

int audio_playback_start(audio_playback_t *ap) {
    ap->running = 1;
    ap->mix_buf = HeapAlloc(GetProcessHeap(), 0, 960 * sizeof(short)); /* max 20ms frames */
    if (!ap->mix_buf) return 0;
    ap->mix_buf_frames = 960;

    ap->thread = CreateThread(NULL, 0, playback_thread, ap, 0, NULL);
    return ap->thread != NULL;
}

void audio_playback_stop(audio_playback_t *ap) {
    ap->running = 0;
    if (ap->thread) {
        WaitForSingleObject(ap->thread, 3000);
        CloseHandle(ap->thread);
        ap->thread = NULL;
    }
    if (ap->mix_buf) {
        HeapFree(GetProcessHeap(), 0, ap->mix_buf);
        ap->mix_buf = NULL;
    }
}
```

---

### Task 6: Opus Codec Wrapper

**Files:**
- Create: `D:\Qmini\src\codec.c`
- Create: `D:\Qmini\src\codec.h`

Wraps libopus encode/decode with trivial API.

```c
// codec.h
#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>

typedef struct codec_enc codec_enc_t;
typedef struct codec_dec codec_dec_t;

codec_enc_t* codec_enc_create(int sample_rate, int channels);
void         codec_enc_destroy(codec_enc_t *e);
int          codec_enc_encode(codec_enc_t *e, const short *pcm, int frame_size, uint8_t *out, int max_out, int fec);

codec_dec_t* codec_dec_create(int sample_rate, int channels);
void         codec_dec_destroy(codec_dec_t *d);
int          codec_dec_decode(codec_dec_t *d, const uint8_t *data, int len, short *pcm, int frame_size, int fec);

#endif
```

```c
// codec.c
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
    opus_encoder_ctl(e->enc, OPUS_SET_BITRATE(20000));       /* 20 kbps */
    opus_encoder_ctl(e->enc, OPUS_SET_COMPLEXITY(3));        /* low complexity */
    opus_encoder_ctl(e->enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(e->enc, OPUS_SET_PACKET_LOSS_PERC(10)); /* assume 10% */
    opus_encoder_ctl(e->enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    return e;
}

void codec_enc_destroy(codec_enc_t *e) {
    if (e && e->enc) opus_encoder_destroy(e->enc);
    free(e);
}

int codec_enc_encode(codec_enc_t *e, const short *pcm, int frame_size, uint8_t *out, int max_out, int fec) {
    return opus_encode(e->enc, pcm, frame_size, out, max_out);
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
    if (d && d->dec) opus_decoder_destroy(d->dec);
    free(d);
}

int codec_dec_decode(codec_dec_t *d, const uint8_t *data, int len, short *pcm, int frame_size, int fec) {
    if (data == NULL || len <= 0) {
        return opus_decode(d->dec, NULL, 0, pcm, frame_size, fec ? 1 : 0);
    }
    return opus_decode(d->dec, data, len, pcm, frame_size, fec ? 1 : 0);
}
```

---

### Task 7: Jitter Buffer

**Files:**
- Create: `D:\Qmini\src\jitter_buffer.c`
- Create: `D:\Qmini\src\jitter_buffer.h`

```c
// jitter_buffer.h
#ifndef JITTER_BUFFER_H
#define JITTER_BUFFER_H

#include <stdint.h>

typedef struct {
    uint8_t  *packets[120];   /* 2.4 seconds @ 20ms packets */
    int       sizes[120];
    uint16_t  seq_numbers[120];
    int       count;
    int       read_cursor;
    int       write_cursor;
    int       target_level;   /* adaptive target fill (frames) */
} jitter_buffer_t;

void jitter_buffer_init(jitter_buffer_t *jb);
void jitter_buffer_push(jitter_buffer_t *jb, const uint8_t *data, int size, uint16_t seq);
/* Read next packet. Returns size, 0 if none. data must hold up to 400 bytes. */
int  jitter_buffer_pop(jitter_buffer_t *jb, uint8_t *data, uint16_t *seq);

#endif
```

```c
// jitter_buffer.c
#include "jitter_buffer.h"
#include <stdlib.h>
#include <string.h>

void jitter_buffer_init(jitter_buffer_t *jb) {
    memset(jb, 0, sizeof(*jb));
    jb->target_level = 4; /* start with 80ms jitter tolerance */
}

void jitter_buffer_push(jitter_buffer_t *jb, const uint8_t *data, int size, uint16_t seq) {
    /* If buffer is full or seq is too old, drop */
    if (jb->count >= 120) return;

    int idx = (jb->write_cursor + jb->count) & 0x7F;
    uint8_t *p = (uint8_t*)malloc(size);
    if (!p) return;
    memcpy(p, data, size);
    if (jb->packets[idx]) free(jb->packets[idx]);
    jb->packets[idx]      = p;
    jb->sizes[idx]        = size;
    jb->seq_numbers[idx]  = seq;
    jb->count++;
}

int jitter_buffer_pop(jitter_buffer_t *jb, uint8_t *data, uint16_t *seq) {
    if (jb->count == 0) return 0;

    /* Wait until we have target_level frames before delivering */
    if (jb->count <= jb->target_level) return 0;

    int idx = jb->read_cursor & 0x7F;
    if (!jb->packets[idx]) return 0;

    int size = jb->sizes[idx];
    memcpy(data, jb->packets[idx], size);
    if (seq) *seq = jb->seq_numbers[idx];

    free(jb->packets[idx]);
    jb->packets[idx] = NULL;
    jb->read_cursor++;
    jb->count--;
    return size;
}
```

---

### Task 8: Networking (UDP P2P + TURN)

**Files:**
- Create: `D:\Qmini\src\network.c`
- Create: `D:\Qmini\src\network.h`

```c
// network.h
#ifndef NETWORK_H
#define NETWORK_H

#include <windows.h>
#include <stdint.h>

#define MAX_PEERS 10
#define MAX_PACKET 1500

typedef struct {
    SOCKET      udp_sock;
    struct      sockaddr_in addr;
    char        id[32];
    int         connected;
    uint16_t    seq_send;   /* outgoing sequence counter */
} peer_t;

typedef struct {
    SOCKET      udp_sock;
    HANDLE      thread;
    int         running;
    peer_t      peers[MAX_PEERS];
    int         npeers;
    char        local_id[32];
    uint16_t    seq_send;
} network_t;

/* Received packet callback */
typedef void (*network_recv_cb)(const char *peer_id, const uint8_t *data, int len, void *user);

int  network_init(network_t *net, uint16_t port, network_recv_cb cb, void *user);
void network_close(network_t *net);
int  network_add_peer(network_t *net, const char *id, const struct sockaddr_in *addr);
void network_remove_peer(network_t *net, const char *id);
int  network_send(network_t *net, const char *peer_id, const uint8_t *data, int len);
int  network_send_all(network_t *net, const uint8_t *data, int len);

/* TURN relay mode: switch a peer to relay through TURN server */
int  network_set_turn_relay(network_t *net, const char *peer_id, const struct sockaddr_in *relay_addr);

#endif
```

```c
// network.c
#include "network.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

typedef struct net_ctx {
    network_t       *net;
    network_recv_cb  cb;
    void            *user;
} net_ctx_t;

static peer_t* find_peer(network_t *net, const char *id) {
    for (int i = 0; i < net->npeers; i++)
        if (strcmp(net->peers[i].id, id) == 0)
            return &net->peers[i];
    return NULL;
}

static DWORD WINAPI net_thread(LPVOID arg) {
    net_ctx_t *ctx = (net_ctx_t*)arg;
    network_t *net = ctx->net;
    uint8_t buf[MAX_PACKET];
    struct sockaddr_in from;
    int from_len = sizeof(from);

    while (net->running) {
        fd_set read_set;
        struct timeval tv = {0, 50000}; /* 50ms timeout */
        FD_ZERO(&read_set);
        FD_SET(net->udp_sock, &read_set);

        int ret = select(0, &read_set, NULL, NULL, &tv);
        if (ret <= 0) continue;

        int n = recvfrom(net->udp_sock, (char*)buf, MAX_PACKET, 0, (struct sockaddr*)&from, &from_len);
        if (n <= 0) continue;

        /* Small header: peer_id (up to 32 bytes) + data.
           For simplicity, we assume the first 32 bytes are the sender's peer ID. */
        char peer_id[33] = {0};
        if (n < 32) continue;
        memcpy(peer_id, buf, 32);
        peer_id[32] = 0;

        if (ctx->cb)
            ctx->cb(peer_id, buf + 32, n - 32, ctx->user);
    }
    return 0;
}

int network_init(network_t *net, uint16_t port, network_recv_cb cb, void *user) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;

    memset(net, 0, sizeof(*net));
    net->running = 1;

    net->udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (net->udp_sock == INVALID_SOCKET) { WSACleanup(); return 0; }

    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(port);
    if (bind(net->udp_sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        closesocket(net->udp_sock);
        WSACleanup();
        return 0;
    }

    /* Non-blocking mode for the socket */
    u_long nonblock = 1;
    ioctlsocket(net->udp_sock, FIONBIO, &nonblock);

    net_ctx_t *ctx = (net_ctx_t*)HeapAlloc(GetProcessHeap(), 0, sizeof(net_ctx_t));
    if (!ctx) { closesocket(net->udp_sock); WSACleanup(); return 0; }
    ctx->net = net;
    ctx->cb = cb;
    ctx->user = user;

    net->thread = CreateThread(NULL, 0, net_thread, ctx, 0, NULL);
    _snprintf(net->local_id, sizeof(net->local_id), "%d", port); /* temp: port as ID */
    return 1;
}

void network_close(network_t *net) {
    net->running = 0;
    if (net->thread) {
        WaitForSingleObject(net->thread, 2000);
        CloseHandle(net->thread);
        net->thread = NULL;
    }
    if (net->udp_sock != INVALID_SOCKET) closesocket(net->udp_sock);
    WSACleanup();
}

int network_add_peer(network_t *net, const char *id, const struct sockaddr_in *addr) {
    if (net->npeers >= MAX_PEERS) return 0;
    peer_t *p = &net->peers[net->npeers++];
    strncpy(p->id, id, sizeof(p->id) - 1);
    p->addr = *addr;
    p->connected = 1;
    p->seq_send = 0;
    return 1;
}

void network_remove_peer(network_t *net, const char *id) {
    for (int i = 0; i < net->npeers; i++) {
        if (strcmp(net->peers[i].id, id) == 0) {
            memmove(&net->peers[i], &net->peers[i+1], (net->npeers - i - 1) * sizeof(peer_t));
            net->npeers--;
            return;
        }
    }
}

int network_send(network_t *net, const char *peer_id, const uint8_t *data, int len) {
    peer_t *p = find_peer(net, peer_id);
    if (!p || !p->connected) return 0;

    uint8_t buf[MAX_PACKET];
    int hdr_len = (int)strlen(net->local_id) + 1;
    if (hdr_len + len > MAX_PACKET) return 0;
    memcpy(buf, net->local_id, hdr_len);
    memcpy(buf + hdr_len, data, len);

    int sent = sendto(net->udp_sock, (const char*)buf, hdr_len + len, 0,
                      (struct sockaddr*)&p->addr, sizeof(p->addr));
    return sent > 0;
}

int network_send_all(network_t *net, const uint8_t *data, int len) {
    int ok = 0;
    for (int i = 0; i < net->npeers; i++)
        if (network_send(net, net->peers[i].id, data, len)) ok = 1;
    return ok;
}

int network_set_turn_relay(network_t *net, const char *peer_id, const struct sockaddr_in *relay_addr) {
    peer_t *p = find_peer(net, peer_id);
    if (!p) return 0;
    p->addr = *relay_addr;
    return 1;
}
```

---

### Task 9: Signaling Client

**Files:**
- Create: `D:\Qmini\src\signaling.c`
- Create: `D:\Qmini\src\signaling.h`

```c
// signaling.h
#ifndef SIGNALING_H
#define SIGNALING_H

#include <windows.h>
#include <winsock2.h>

typedef struct {
    SOCKET  sock;
    HANDLE  thread;
    int     running;
    char    server_host[64];
    int     server_port;
    char    room[16];
    char    nickname[32];
    char    local_id[32];
    int     local_port;
} signaling_t;

/* Callback when a new peer joins the room */
typedef void (*signaling_peer_cb)(const char *peer_id, const char *nickname, struct sockaddr_in *addr);

/* Callback when a peer leaves */
typedef void (*signaling_leave_cb)(const char *peer_id);

typedef struct {
    signaling_t         *sig;
    signaling_peer_cb    peer_cb;
    signaling_leave_cb   leave_cb;
    void                *user;
} signaling_ctx_t;

int  signaling_connect(signaling_t *sig, const char *host, int port, const char *room,
                       const char *nickname, int local_port, uint32_t public_ip, int public_port);
void signaling_disconnect(signaling_t *sig);
int  signaling_send_ice(signaling_t *sig, const char *target_id, const char *sdp);

#endif
```

```c
// signaling.c
#include "signaling.h"
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

/* Simple TCP text protocol to signaling server.
   Commands (client->server):
     REGISTER <room> <nickname> <local_port> <public_ip> <public_port>
     ICE <target_id> <sdp_line>
   Server responses:
     OK <peer_id>
     PEER_JOIN <peer_id> <nickname> <ip> <port>
     PEER_LEAVE <peer_id>
     ICE <from_id> <sdp_line>
*/

static int recv_line(SOCKET s, char *buf, int bufsz) {
    int i = 0;
    while (i < bufsz - 1) {
        char c;
        int n = recv(s, &c, 1, 0);
        if (n <= 0) return 0;
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = 0;
    return i;
}

static DWORD WINAPI signaling_thread(LPVOID arg) {
    signaling_ctx_t *ctx = (signaling_ctx_t*)arg;
    signaling_t *sig = ctx->sig;
    char buf[1024];

    while (sig->running) {
        int n = recv_line(sig->sock, buf, sizeof(buf));
        if (n <= 0) break;

        if (strncmp(buf, "PEER_JOIN ", 10) == 0) {
            char pid[32], nick[32], ip[64];
            int port;
            if (sscanf(buf + 10, "%31s %31s %63s %d", pid, nick, ip, &port) >= 2) {
                if (ctx->peer_cb) {
                    struct sockaddr_in addr;
                    addr.sin_family = AF_INET;
                    addr.sin_addr.s_addr = inet_addr(ip);
                    addr.sin_port = htons((short)port);
                    ctx->peer_cb(pid, nick, &addr);
                }
            }
        } else if (strncmp(buf, "PEER_LEAVE ", 11) == 0) {
            if (ctx->leave_cb) ctx->leave_cb(buf + 11);
        }
        /* ICE messages handled at integration level */
    }

    return 0;
}

int signaling_connect(signaling_t *sig, const char *host, int port, const char *room,
                      const char *nickname, int local_port, uint32_t public_ip, int public_port) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    memset(sig, 0, sizeof(*sig));
    sig->running = 1;
    strncpy(sig->server_host, host, sizeof(sig->server_host) - 1);
    sig->server_port = port;
    strncpy(sig->room, room, sizeof(sig->room) - 1);
    strncpy(sig->nickname, nickname, sizeof(sig->nickname) - 1);
    sig->local_port = local_port;

    sig->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sig->sock == INVALID_SOCKET) return 0;

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(host);
    server.sin_port = htons((short)port);

    if (connect(sig->sock, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        closesocket(sig->sock);
        sig->sock = INVALID_SOCKET;
        return 0;
    }

    /* Send REGISTER command */
    char cmd[256];
    _snprintf(cmd, sizeof(cmd), "REGISTER %s %s %d %d %d\n",
              room, nickname, local_port, ntohl(public_ip), public_port);
    send(sig->sock, cmd, (int)strlen(cmd), 0);

    /* Read OK response */
    char resp[64];
    recv_line(sig->sock, resp, sizeof(resp));

    return 1;
}

void signaling_disconnect(signaling_t *sig) {
    sig->running = 0;
    if (sig->thread) {
        WaitForSingleObject(sig->thread, 2000);
        CloseHandle(sig->thread);
        sig->thread = NULL;
    }
    if (sig->sock != INVALID_SOCKET) {
        closesocket(sig->sock);
        sig->sock = INVALID_SOCKET;
    }
}

int signaling_send_ice(signaling_t *sig, const char *target_id, const char *sdp) {
    char cmd[1024];
    _snprintf(cmd, sizeof(cmd), "ICE %s %s\n", target_id, sdp ? sdp : "");
    return send(sig->sock, cmd, (int)strlen(cmd), 0) > 0;
}
```

---

### Task 10: System Tray

**Files:**
- Create: `D:\Qmini\src\tray.c`
- Create: `D:\Qmini\src\tray.h`

```c
// tray.h
#ifndef TRAY_H
#define TRAY_H

#include <windows.h>

typedef enum {
    TRAY_CMD_JOIN_ROOM = 1,
    TRAY_CMD_LEAVE_ROOM,
    TRAY_CMD_TOGGLE_MUTE,
    TRAY_CMD_EXIT,
} tray_cmd_t;

typedef struct {
    HWND     hwnd;
    HMENU    menu;
    HICON    icon;
    int      muted;   /* current mute state, for display */
} tray_t;

int  tray_create(tray_t *t, HINSTANCE inst);
void tray_destroy(tray_t *t);
void tray_set_muted(tray_t *t, int muted);

#endif
```

```c
// tray.c
#include "tray.h"
#include <shellapi.h>

#define WM_TRAYICON (WM_APP + 1)
#define TRAY_ICON_ID 1

static LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    tray_t *t = (tray_t*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (msg == WM_TRAYICON) {
        if (l == WM_RBUTTONDOWN) {
            SetForegroundWindow(hwnd);
            POINT pt;
            GetCursorPos(&pt);
            TrackPopupMenu(t->menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessage(hwnd, WM_NULL, 0, 0);
            return 0;
        }
        if (l == WM_LBUTTONDBLCLK) {
            /* Double-click: toggle mute */
            if (t) PostMessage(hwnd, WM_COMMAND, TRAY_CMD_TOGGLE_MUTE, 0);
            return 0;
        }
    }
    if (msg == WM_COMMAND) {
        if (w == TRAY_CMD_EXIT)
            PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, w, l);
}

int tray_create(tray_t *t, HINSTANCE inst) {
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = tray_wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = "QminiTrayWindow";
    RegisterClassA(&wc);

    t->hwnd = CreateWindowA("QminiTrayWindow", "Qmini", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, NULL, NULL, inst, NULL);
    SetWindowLongPtr(t->hwnd, GWLP_USERDATA, (LONG_PTR)t);

    /* Create a simple icon using GDI */
    HDC dc = GetDC(NULL);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, 16, 16);
    SelectObject(mem, bmp);
    RECT r = {0, 0, 16, 16};
    HBRUSH brush = CreateSolidBrush(RGB(0, 140, 200)); /* blue */
    FillRect(mem, &r, brush);
    DeleteObject(brush);
    /* Draw a small speaker icon */
    brush = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(mem, brush);
    /* Simple speaker shape */
    PatBlt(mem, 2, 5, 3, 6, PATCOPY);   /* body */
    PatBlt(mem, 5, 3, 3, 10, PATCOPY);  /* cone */
    DeleteObject(brush);
    DeleteDC(mem);

    ICONINFO ii;
    ii.fIcon    = TRUE;
    ii.hbmMask  = bmp;
    ii.hbmColor = bmp;
    t->icon = CreateIconIndirect(&ii);
    DeleteObject(bmp);
    ReleaseDC(NULL, dc);

    NOTIFYICONDATAA nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = t->hwnd;
    nid.uID    = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    lstrcpyA(nid.szTip, "Qmini Voice");
    nid.hIcon  = t->icon;
    Shell_NotifyIconA(NIM_ADD, &nid);

    /* Context menu */
    t->menu = CreatePopupMenu();
    AppendMenuA(t->menu, MF_STRING, TRAY_CMD_JOIN_ROOM,    "Join Room...");
    AppendMenuA(t->menu, MF_STRING, TRAY_CMD_LEAVE_ROOM,   "Leave Room");
    AppendMenuA(t->menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(t->menu, MF_STRING, TRAY_CMD_TOGGLE_MUTE,  "Toggle Mute");
    AppendMenuA(t->menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(t->menu, MF_STRING, TRAY_CMD_EXIT,         "Exit");

    t->muted = 0;
    return 1;
}

void tray_destroy(tray_t *t) {
    NOTIFYICONDATAA nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd = t->hwnd;
    nid.uID  = TRAY_ICON_ID;
    Shell_NotifyIconA(NIM_DELETE, &nid);
    if (t->icon) DestroyIcon(t->icon);
    if (t->menu) DestroyMenu(t->menu);
    if (t->hwnd) DestroyWindow(t->hwnd);
}

void tray_set_muted(tray_t *t, int muted) {
    t->muted = muted;
    NOTIFYICONDATAA nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd = t->hwnd;
    nid.uID  = TRAY_ICON_ID;
    nid.uFlags = NIF_TIP;
    lstrcpyA(nid.szTip, muted ? "Qmini Voice [MUTED]" : "Qmini Voice");
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}
```

---

### Task 11: Global Hotkeys

**Files:**
- Create: `D:\Qmini\src\hotkey.c`
- Create: `D:\Qmini\src\hotkey.h`

```c
// hotkey.h
#ifndef HOTKEY_H
#define HOTKEY_H

#include <windows.h>

#define HOTKEY_PTT    1
#define HOTKEY_MUTE   2

typedef struct {
    HWND   hwnd;
    int    ptt_key;     /* virtual key code */
    int    mute_key;
    int    ptt_down;    /* current PTT state */
    int    muted;       /* current mute state */
} hotkey_t;

int  hotkey_init(hotkey_t *hk, HWND hwnd, int ptt_key, int mute_key);
void hotkey_destroy(hotkey_t *hk);
void hotkey_set_mute(hotkey_t *hk, int muted);

#endif
```

```c
// hotkey.c
#include "hotkey.h"

int hotkey_init(hotkey_t *hk, HWND hwnd, int ptt_key, int mute_key) {
    hk->hwnd    = hwnd;
    hk->ptt_key = ptt_key;
    hk->mute_key = mute_key;
    hk->ptt_down = 0;
    hk->muted    = 0;

    RegisterHotKey(hwnd, HOTKEY_PTT, MOD_NOREPEAT, ptt_key);
    RegisterHotKey(hwnd, HOTKEY_MUTE, MOD_CONTROL | MOD_NOREPEAT, mute_key);
    return 1;
}

void hotkey_destroy(hotkey_t *hk) {
    UnregisterHotKey(hk->hwnd, HOTKEY_PTT);
    UnregisterHotKey(hk->hwnd, HOTKEY_MUTE);
}

void hotkey_set_mute(hotkey_t *hk, int muted) {
    hk->muted = muted;
}
```

---

### Task 12: Main Integration

**Files:**
- Create: `D:\Qmini\src\main.c`

This is the most complex file — it ties all modules together and runs the Windows message loop.

```c
// main.c
#include "audio_capture.h"
#include "audio_playback.h"
#include "codec.h"
#include "jitter_buffer.h"
#include "network.h"
#include "signaling.h"
#include "tray.h"
#include "hotkey.h"
#include "config.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#define OPUS_FRAME_SIZE 320   /* 20ms @ 16kHz */

/* Global state */
static config_t           g_cfg;
static tray_t             g_tray;
static hotkey_t           g_hk;
static audio_capture_t    g_capture;
static audio_playback_t   g_playback;
static network_t          g_net;
static signaling_t        g_sig;

/* Per-peer decoder state */
typedef struct {
    char          id[32];
    codec_dec_t  *dec;
    jitter_buffer_t jb;
    speaker_t     speaker;    /* for playback mixing */
    int           active;
} peer_state_t;

static peer_state_t g_peers[MAX_PEERS];
static int g_npeers = 0;
static int g_muted = 0;
static int g_in_room = 0;

/* Encoder */
static codec_enc_t *g_encoder = NULL;

/* Forward declarations */
static void on_audio_frame(const short *samples, int count, void *user);
static void on_network_packet(const char *peer_id, const uint8_t *data, int len, void *user);
static void on_peer_join(const char *peer_id, const char *nickname, struct sockaddr_in *addr);
static void on_peer_leave(const char *peer_id);

void on_audio_frame(const short *samples, int count, void *user) {
    if (g_muted) return;
    if (!g_encoder) return;
    /* Encode every full frame (320 samples = 20ms) */
    static short accum[OPUS_FRAME_SIZE];
    static int accum_pos = 0;
    int pos = 0;
    while (pos < count) {
        int room = OPUS_FRAME_SIZE - accum_pos;
        int copy = count - pos;
        if (copy > room) copy = room;
        memcpy(accum + accum_pos, samples + pos, copy * sizeof(short));
        accum_pos += copy;
        pos += copy;

        if (accum_pos >= OPUS_FRAME_SIZE) {
            uint8_t encoded[400];
            int len = codec_enc_encode(g_encoder, accum, OPUS_FRAME_SIZE / 2, encoded, sizeof(encoded), 1);
            if (len > 0) {
                g_net.seq_send++;
                /* Send to all peers */
                network_send_all(&g_net, encoded, len);
            }
            accum_pos = 0;
        }
    }
}

void on_network_packet(const char *peer_id, const uint8_t *data, int len, void *user) {
    /* Find or create peer state */
    peer_state_t *ps = NULL;
    for (int i = 0; i < g_npeers; i++) {
        if (strcmp(g_peers[i].id, peer_id) == 0) {
            ps = &g_peers[i];
            break;
        }
    }
    if (!ps) return;

    /* Push into jitter buffer */
    jitter_buffer_push(&ps->jb, data, len, 0); /* seq handling simplified */
}

void on_peer_join(const char *peer_id, const char *nickname, struct sockaddr_in *addr) {
    if (g_npeers >= MAX_PEERS) return;
    peer_state_t *ps = &g_peers[g_npeers++];
    strncpy(ps->id, peer_id, sizeof(ps->id) - 1);
    ps->dec = codec_dec_create(16000, 1);
    jitter_buffer_init(&ps->jb);
    ps->active = 1;
    ps->speaker.buffer = HeapAlloc(GetProcessHeap(), 0, OPUS_FRAME_SIZE * sizeof(short) * 10); /* 200ms buffer */
    ps->speaker.frames = 0;
    ps->speaker.read_pos = 0;

    network_add_peer(&g_net, peer_id, addr);
}

void on_peer_leave(const char *peer_id) {
    for (int i = 0; i < g_npeers; i++) {
        if (strcmp(g_peers[i].id, peer_id) == 0) {
            codec_dec_destroy(g_peers[i].dec);
            if (g_peers[i].speaker.buffer) HeapFree(GetProcessHeap(), 0, g_peers[i].speaker.buffer);
            network_remove_peer(&g_net, peer_id);
            memmove(&g_peers[i], &g_peers[i+1], (g_npeers - i - 1) * sizeof(peer_state_t));
            g_npeers--;
            return;
        }
    }
}

/* Decode pending jitter buffer frames into speaker buffers (called periodically from main loop) */
static void process_audio() {
    uint8_t data[400];
    for (int i = 0; i < g_npeers; i++) {
        short *pcm = g_peers[i].speaker.buffer + g_peers[i].speaker.read_pos + g_peers[i].speaker.frames;
        int max_frames = (OPUS_FRAME_SIZE * 10) - g_peers[i].speaker.frames;
        int decoded = 0;
        while (max_frames >= OPUS_FRAME_SIZE) {
            int sz = jitter_buffer_pop(&g_peers[i].jb, data, NULL);
            if (sz <= 0) break;
            int frames = codec_dec_decode(g_peers[i].dec, data, sz, pcm, OPUS_FRAME_SIZE / 2, 1);
            if (frames > 0) {
                pcm += frames * g_peers[i].speaker.buffer[0]; /* skip, simplified */
                g_peers[i].speaker.frames += frames;
                max_frames -= frames;
                decoded = 1;
            }
        }
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    /* Load config */
    config_load(&g_cfg);

    /* Create tray */
    tray_create(&g_tray, inst);

    /* Init hotkeys */
    hotkey_init(&g_hk, g_tray.hwnd, g_cfg.ptt_key, g_cfg.mute_key);

    /* Init encoder */
    g_encoder = codec_enc_create(16000, 1);

    /* Init playback */
    audio_playback_start(&g_playback);

    /* Init network on a random port */
    network_init(&g_net, 0, on_network_packet, NULL);

    /* Init capture */
    audio_capture_start(&g_capture, on_audio_frame, NULL);

    /* Register hotkey handlers in window procedure. We subclass by patching the existing window.
       For simplicity, we use a message loop that handles hotkey messages. */
    SetWindowLongPtr(g_tray.hwnd, GWLP_USERDATA, (LONG_PTR)&g_tray);

    /* Message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == HOTKEY_PTT) {
                /* PTT: check key state */
                int down = (GetAsyncKeyState(g_cfg.ptt_key) & 0x8000) != 0;
                g_muted = !down;
            } else if (msg.wParam == HOTKEY_MUTE) {
                g_muted = !g_muted;
                tray_set_muted(&g_tray, g_muted);
            }
        }
        if (msg.message == WM_TRAYICON) {
            /* Forward to tray window proc */
            tray_wndproc(g_tray.hwnd, msg.message, msg.wParam, msg.lParam);
        }
        if (msg.message == WM_COMMAND) {
            if (msg.wParam == TRAY_CMD_TOGGLE_MUTE) {
                g_muted = !g_muted;
                tray_set_muted(&g_tray, g_muted);
            }
            if (msg.wParam == TRAY_CMD_JOIN_ROOM) {
                /* Simple: just connect to signaling with hardcoded room for now */
                if (!g_in_room) {
                    /* Use config server address */
                    char host[64] = {0};
                    int port = 9800;
                    sscanf(g_cfg.server_addr, "%63[^:]:%d", host, &port);
                    if (signaling_connect(&g_sig, host, port, "default", g_cfg.nickname,
                                          g_net.peers[0].addr.sin_port, 0, 0)) {
                        g_in_room = 1;
                        MessageBoxA(NULL, "Connected to room 'default'", "Qmini", MB_OK);
                    } else {
                        MessageBoxA(NULL, "Failed to connect to signaling server", "Qmini", MB_OK);
                    }
                }
            }
            if (msg.wParam == TRAY_CMD_LEAVE_ROOM) {
                if (g_in_room) {
                    signaling_disconnect(&g_sig);
                    g_in_room = 0;
                }
            }
            if (msg.wParam == TRAY_CMD_EXIT) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        /* Periodically process audio (every 20ms roughly) */
        process_audio();
    }

    /* Cleanup */
    audio_capture_stop(&g_capture);
    audio_playback_stop(&g_playback);
    network_close(&g_net);
    if (g_in_room) signaling_disconnect(&g_sig);
    if (g_encoder) codec_enc_destroy(g_encoder);
    for (int i = 0; i < g_npeers; i++) {
        codec_dec_destroy(g_peers[i].dec);
        if (g_peers[i].speaker.buffer) HeapFree(GetProcessHeap(), 0, g_peers[i].speaker.buffer);
    }
    hotkey_destroy(&g_hk);
    tray_destroy(&g_tray);
    return 0;
}
```

---

### Task 13: Signaling Server (Standalone)

**Files:**
- Create: `D:\Qmini\signaling_server\signaling_server.c`
- Create: `D:\Qmini\signaling_server\build.bat`

```c
// signaling_server.c
/* Simple TCP signaling server for Qmini.
   Handles room registration, peer discovery, and ICE message relay.

   Protocol (text-based, \n terminated):
   Client -> Server:
     REGISTER <room> <nickname> <local_port> <public_ip> <public_port>
     ICE <target_id> <payload>
     UNREGISTER <room>
   Server -> Client:
     OK <peer_id>
     PEER_JOIN <peer_id> <nickname> <ip> <port>
     PEER_LEAVE <peer_id>
     ICE <from_id> <payload>
*/

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define MAX_CLIENTS 64
#define MAX_ROOMS 8
#define MAX_NAME 32
#define MAX_ID 32

typedef struct {
    SOCKET  sock;
    char    id[MAX_ID];
    char    nickname[MAX_NAME];
    char    room[MAX_NAME];
    struct  sockaddr_in public_addr;
    int     active;
} client_t;

static client_t g_clients[MAX_CLIENTS];
static int g_nclients = 0;
static int g_next_id = 1;

static CRITICAL_SECTION g_lock;

static client_t* find_client_by_sock(SOCKET s) {
    for (int i = 0; i < g_nclients; i++)
        if (g_clients[i].active && g_clients[i].sock == s)
            return &g_clients[i];
    return NULL;
}

static int send_to_client(SOCKET s, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        buf[n] = '\n';
        send(s, buf, n + 1, 0);
    }
    return n;
}

static void broadcast_room(const char *room, SOCKET exclude, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    buf[n] = '\n';

    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_nclients; i++) {
        if (g_clients[i].active && g_clients[i].sock != exclude &&
            strcmp(g_clients[i].room, room) == 0) {
            send(g_clients[i].sock, buf, n + 1, 0);
        }
    }
    LeaveCriticalSection(&g_lock);
}

static void remove_client(SOCKET s) {
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_nclients; i++) {
        if (g_clients[i].active && g_clients[i].sock == s) {
            g_clients[i].active = 0;
            broadcast_room(g_clients[i].room, s, "PEER_LEAVE %s", g_clients[i].id);
            closesocket(s);
            LeaveCriticalSection(&g_lock);
            return;
        }
    }
    LeaveCriticalSection(&g_lock);
}

static DWORD WINAPI client_thread(LPVOID arg) {
    SOCKET s = (SOCKET)(uintptr_t)arg;
    client_t *c = find_client_by_sock(s);
    char buf[2048];
    int pos = 0;

    while (1) {
        int n = recv(s, buf + pos, sizeof(buf) - pos - 1, 0);
        if (n <= 0) break;
        pos += n;
        buf[pos] = 0;

        /* Process complete lines */
        char *line = buf;
        while (1) {
            char *nl = strchr(line, '\n');
            if (!nl) break;
            *nl = 0;

            if (strncmp(line, "REGISTER ", 9) == 0) {
                char room[MAX_NAME], nick[MAX_NAME];
                int local_port, pub_ip, pub_port;
                if (sscanf(line + 9, "%31s %31s %d %d %d", room, nick, &local_port, &pub_ip, &pub_port) >= 2) {
                    char id[MAX_ID];
                    _snprintf(id, sizeof(id), "user%d", g_next_id++);
                    strncpy(c->room, room, sizeof(c->room) - 1);
                    strncpy(c->nickname, nick, sizeof(c->nickname) - 1);
                    strncpy(c->id, id, sizeof(c->id) - 1);
                    c->public_addr.sin_family = AF_INET;
                    c->public_addr.sin_addr.s_addr = htonl(pub_ip ? pub_ip : 0);
                    c->public_addr.sin_port = htons((short)(pub_port ? pub_port : local_port));

                    send_to_client(s, "OK %s", id);

                    /* Get sender's actual address */
                    struct sockaddr_in actual;
                    int actual_len = sizeof(actual);
                    getpeername(s, (struct sockaddr*)&actual, &actual_len);

                    /* Notify existing room members about new peer */
                    broadcast_room(room, s, "PEER_JOIN %s %s %s %d",
                                   id, nick, inet_ntoa(actual.sin_addr), local_port);

                    /* Tell new peer about existing members */
                    EnterCriticalSection(&g_lock);
                    for (int j = 0; j < g_nclients; j++) {
                        if (g_clients[j].active && g_clients[j].sock != s &&
                            strcmp(g_clients[j].room, room) == 0) {
                            struct sockaddr_in peer_addr;
                            int peer_len = sizeof(peer_addr);
                            getpeername(g_clients[j].sock, (struct sockaddr*)&peer_addr, &peer_len);
                            send_to_client(s, "PEER_JOIN %s %s %s %d",
                                           g_clients[j].id, g_clients[j].nickname,
                                           inet_ntoa(peer_addr.sin_addr), ntohs(g_clients[j].public_addr.sin_port));
                        }
                    }
                    LeaveCriticalSection(&g_lock);
                }
            } else if (strncmp(line, "ICE ", 4) == 0) {
                char target[MAX_ID];
                const char *payload = line + 4;
                char *space = strchr(payload, ' ');
                if (space) {
                    *space = 0;
                    strncpy(target, payload, sizeof(target) - 1);
                    payload = space + 1;
                } else {
                    strncpy(target, payload, sizeof(target) - 1);
                    payload = "";
                }
                EnterCriticalSection(&g_lock);
                for (int j = 0; j < g_nclients; j++) {
                    if (g_clients[j].active && g_clients[j].sock != s &&
                        strcmp(g_clients[j].id, target) == 0) {
                        send_to_client(g_clients[j].sock, "ICE %s %s", c->id, payload);
                        break;
                    }
                }
                LeaveCriticalSection(&g_lock);
            } else if (strncmp(line, "UNREGISTER ", 11) == 0) {
                remove_client(s);
                closesocket(s);
                return 0;
            }

            line = nl + 1;
        }

        /* Shift remaining partial data */
        int remaining = buf + pos - line;
        if (remaining > 0 && line != buf) memmove(buf, line, remaining);
        pos = remaining;
        buf[pos] = 0;
    }

    remove_client(s);
    return 0;
}

int main() {
    WSADATA wsa;
    SOCKET listen_sock;
    struct sockaddr_in addr;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    InitializeCriticalSection(&g_lock);

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(9800);

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("Failed to bind port 9800\n");
        return 1;
    }

    listen(listen_sock, SOMAXCONN);
    printf("Qmini signaling server listening on port 9800\n");

    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET s = accept(listen_sock, (struct sockaddr*)&client_addr, &client_len);
        if (s == INVALID_SOCKET) continue;

        EnterCriticalSection(&g_lock);
        if (g_nclients >= MAX_CLIENTS) {
            LeaveCriticalSection(&g_lock);
            closesocket(s);
            continue;
        }
        int idx = g_nclients++;
        g_clients[idx].sock = s;
        g_clients[idx].active = 1;
        LeaveCriticalSection(&g_lock);

        HANDLE h = CreateThread(NULL, 0, client_thread, (void*)(uintptr_t)s, 0, NULL);
        CloseHandle(h);
    }

    DeleteCriticalSection(&g_lock);
    closesocket(listen_sock);
    WSACleanup();
    return 0;
}
```

```bat
:: signaling_server\build.bat
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cl /nologo /O1 /MT /W3 signaling_server.c /Fesignaling_server.exe /link ws2_32.lib
echo Server built: signaling_server.exe
```

---

### Execution Strategy

**Order of execution:**

| Task | Description | Dependencies | Est. Time |
|------|-------------|-------------|-----------|
| 1  | Build script + scaffold | None | 5 min |
| 2  | Ring buffer (header) | None | 3 min |
| 3  | Config module | None | 5 min |
| 10 | System tray | None | 8 min |
| 11 | Global hotkeys | None | 5 min |
| 6  | Opus codec wrapper | Task 1 (opus lib built) | 5 min |
| 4  | Audio capture | Task 1, 2 | 8 min |
| 5  | Audio playback | Task 1, 2 | 8 min |
| 7  | Jitter buffer | None | 5 min |
| 8  | Networking | Task 1 | 10 min |
| 9  | Signaling client | Task 1, 8 | 8 min |
| 12 | Main integration | All above | 15 min |
| 13 | Signaling server | None (standalone) | 10 min |

**Parallel groups:**
- Group A (no deps): Task 1, 2, 3, 7, 10, 11, 13
- Group B (needs opus lib): Task 6 (after Task 1 builds opus)
- Group C (needs Task 1): Task 4, 5, 8
- Group D (needs networking): Task 9
- Group E (everything): Task 12
