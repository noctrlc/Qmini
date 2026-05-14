#include "audio_capture.h"
#include "audio_playback.h"
#include "codec.h"
#include "jitter_buffer.h"
#include "network.h"
#include "signaling.h"
#include "panel.h"
#include "hotkey.h"
#include "config.h"
#include "dialog.h"
#include "notify.h"
#include "crypto.h"
#include "ringbuf.h"
#include "aec.h"
#include "congestion.h"
#include <windows.h>
#include <objbase.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <mmsystem.h>
#include <math.h>

#pragma comment(lib, "winmm.lib")


#define OPUS_FRAME_SIZE   320    /* 20ms @ 16kHz */
#define CAPTURE_RING_SIZE 8192
#define SPEAKER_BUF_FRAMES 4096  /* power of 2 for fast modulo */
#define SPEAKER_BUF_MASK  (SPEAKER_BUF_FRAMES - 1)

#define WM_JOIN_RESULT  (WM_APP + 10)

typedef struct {
    char host[64];
    int  port;
    char room[32];
    char nick[32];
    int  sig_port;
} join_params_t;

/* --- Global state --- */
static config_t           g_cfg;
static panel_t            g_panel;
static hotkey_t           g_hk;
static audio_capture_t    g_capture;
static audio_playback_t   g_playback;
static network_t          g_net;
static signaling_t        g_sig;
static codec_enc_t       *g_encoder = NULL;
static crypto_ctx_t       g_crypto;
aec_t                    *g_aec = NULL;
static congestion_ctrl_t  g_cc;

static uint8_t g_cap_ring_buf[CAPTURE_RING_SIZE];
static ringbuf_t g_cap_ring;

typedef struct {
    char            id[32];
    char            nick[32];
    codec_dec_t    *dec;
    jitter_buffer_t jb;
    speaker_t       speaker;
    int             active;
    DWORD           joined_time;    /* GetTickCount when peer was added */
    DWORD           last_udp_recv;  /* 0 = never received UDP from this peer */
    int             use_relay;      /* 1 = send via TCP relay */
} peer_state_t;

static peer_state_t g_peers[MAX_PEERS];
static int g_npeers = 0;
static CRITICAL_SECTION g_peer_lock;

static volatile int g_input_mode = INPUT_MODE_PTT;
static volatile int g_ptt_pressed = 0;
static int g_prev_mode = INPUT_MODE_PTT;  /* for mute toggle */
static int g_in_room = 0;
static volatile int g_connecting = 0;
static volatile int g_cancel_connect = 0;
static char g_svr[64] = {0};  /* current server address */
static char g_rm[32] = {0};   /* current room name */
static volatile int g_peak_pct = 0;    /* mic peak level 0-100 */
static int g_capture_count = 0;  /* number of capture callbacks */
static volatile int g_restart_capture = 0;
static volatile int g_restart_playback = 0;

/* Loopback test state */
static short *g_test_buf = NULL;
static int g_test_pos = 0;
static int g_test_max = 0;
static int g_test_playing = 0;
static int g_test_capturing = 0;

/* --- Callbacks --- */

static void on_capture_frame(const short *samples, int count, void *user) {
    (void)user;

    g_capture_count++;
    /* Compute peak level (0-100) */
    int peak = 0;
    for (int i = 0; i < count; i++) {
        int s = samples[i] > 0 ? samples[i] : -samples[i];
        if (s > peak) peak = s;
    }
    g_peak_pct = (peak * 100) / 32767;

    /* Loopback capture: fill test buffer */
    if (g_test_capturing && g_test_buf) {
        int remaining = g_test_max - g_test_pos;
        int copy = count < remaining ? count : remaining;
        memcpy(g_test_buf + g_test_pos, samples, copy * sizeof(short));
        g_test_pos += copy;
        if (g_test_pos >= g_test_max) {
            g_test_capturing = 0;
            /* Start playback after short delay */
            g_test_playing = 1;
        }
    }

    if (g_input_mode == INPUT_MODE_MUTED) return;
    if (g_input_mode == INPUT_MODE_PTT && !g_ptt_pressed) return;
    /* INPUT_MODE_OPEN: always send */
    ringbuf_push(&g_cap_ring, (const uint8_t*)samples, count * sizeof(short));
}

static void on_network_recv(const char *peer_id, const uint8_t *data, int len, void *user) {
    (void)user;
    EnterCriticalSection(&g_peer_lock);
    for (int i = 0; i < g_npeers; i++) {
        if (strcmp(g_peers[i].id, peer_id) == 0) {
            g_peers[i].last_udp_recv = GetTickCount();
            g_peers[i].use_relay = 0;  /* UDP is working, disable relay */
            jitter_buffer_push(&g_peers[i].jb, data, len, 0);
            break;
        }
    }
    LeaveCriticalSection(&g_peer_lock);
}

static void on_keepalive_recv(const char *peer_id, void *user) {
    (void)user;
    EnterCriticalSection(&g_peer_lock);
    for (int i = 0; i < g_npeers; i++) {
        if (strcmp(g_peers[i].id, peer_id) == 0) {
            g_peers[i].last_udp_recv = GetTickCount();
            g_peers[i].use_relay = 0;  /* UDP keepalive received, switch back to UDP */
            break;
        }
    }
    LeaveCriticalSection(&g_peer_lock);
}

static void on_relay_recv(const char *from_id, const uint8_t *data, int len, void *user) {
    (void)user;
    EnterCriticalSection(&g_peer_lock);
    for (int i = 0; i < g_npeers; i++) {
        if (strcmp(g_peers[i].id, from_id) == 0) {
            jitter_buffer_push(&g_peers[i].jb, data, len, 0);
            break;
        }
    }
    LeaveCriticalSection(&g_peer_lock);
}

static void update_member_list(void) {
    const char *names[MAX_PEERS + 1];
    int n = 0;
    /* Self always first */
    names[n++] = g_cfg.nickname;
    for (int i = 0; i < g_npeers && n <= MAX_PEERS; i++)
        names[n++] = g_peers[i].nick[0] ? g_peers[i].nick : g_peers[i].id;
    panel_set_members(&g_panel, names, n);
}

static void on_peer_join(const char *peer_id, const char *nickname, struct sockaddr_in *addr, void *user) {
    (void)user;
    EnterCriticalSection(&g_peer_lock);
    /* Duplicate check: if peer already exists, just update address */
    for (int i = 0; i < g_npeers; i++) {
        if (g_peers[i].active && strcmp(g_peers[i].id, peer_id) == 0) {
            network_add_peer(&g_net, peer_id, addr);
            LeaveCriticalSection(&g_peer_lock);
            return;
        }
    }
    if (g_npeers >= MAX_PEERS) { LeaveCriticalSection(&g_peer_lock); return; }

    peer_state_t *ps = &g_peers[g_npeers++];
    strncpy(ps->id, peer_id, sizeof(ps->id) - 1);
    ps->id[sizeof(ps->id) - 1] = 0;
    strncpy(ps->nick, nickname, sizeof(ps->nick) - 1);
    ps->nick[sizeof(ps->nick) - 1] = 0;
    ps->dec = codec_dec_create(16000, 1);
    jitter_buffer_init(&ps->jb);
    ps->active = 1;
    ps->joined_time = GetTickCount();
    ps->last_udp_recv = 0;
    ps->use_relay = 0;  /* start with UDP, switch to relay if needed */
    ps->speaker.buffer = (short*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, SPEAKER_BUF_FRAMES * sizeof(short));
    ps->speaker.capacity = SPEAKER_BUF_FRAMES;
    ps->speaker.frames = 0;
    ps->speaker.read_pos = 0;

    network_add_peer(&g_net, peer_id, addr);
    LeaveCriticalSection(&g_peer_lock);
    update_member_list();
}

static void on_peer_leave(const char *peer_id, void *user) {
    (void)user;
    EnterCriticalSection(&g_peer_lock);
    for (int i = 0; i < g_npeers; i++) {
        if (strcmp(g_peers[i].id, peer_id) == 0) {
            codec_dec_destroy(g_peers[i].dec);
            jitter_buffer_destroy(&g_peers[i].jb);
            if (g_peers[i].speaker.buffer)
                HeapFree(GetProcessHeap(), 0, g_peers[i].speaker.buffer);
            network_remove_peer(&g_net, peer_id);
            memmove(&g_peers[i], &g_peers[i+1], (g_npeers - i - 1) * sizeof(peer_state_t));
            g_npeers--;
            break;
        }
    }
    LeaveCriticalSection(&g_peer_lock);
    update_member_list();
}

static void on_ice_msg(const char *from_id, const char *sdp, void *user) {
    (void)user; (void)from_id; (void)sdp;
}

/* --- Audio processing --- */

static DWORD WINAPI join_thread(LPVOID arg) {
    join_params_t *jp = (join_params_t*)arg;
    HWND hwnd = g_panel.hwnd;

    if (g_cancel_connect) { free(jp); g_connecting = 0; return 0; }

    /* Clean up stale peers from previous session */
    EnterCriticalSection(&g_peer_lock);
    for (int i = 0; i < g_npeers; i++) {
        codec_dec_destroy(g_peers[i].dec);
        jitter_buffer_destroy(&g_peers[i].jb);
        if (g_peers[i].speaker.buffer)
            HeapFree(GetProcessHeap(), 0, g_peers[i].speaker.buffer);
    }
    g_npeers = 0;
    LeaveCriticalSection(&g_peer_lock);

    int ok = signaling_connect(&g_sig, jp->host, jp->port, jp->room, jp->nick, jp->sig_port, g_net.udp_sock);
    if (g_cancel_connect) {
        /* User cancelled while connecting */
        if (ok) signaling_disconnect(&g_sig);
        free(jp);
        g_connecting = 0;
        g_cancel_connect = 0;
        PostMessageW(hwnd, WM_JOIN_RESULT, 2, 0);  /* wParam=2 means cancelled */
        return 0;
    }

    if (ok) {
        /* Sync P2P packet ID with server-assigned signaling ID */
        network_set_local_id(&g_net, g_sig.local_id);
        PostMessageW(hwnd, WM_JOIN_RESULT, 1, (LPARAM)jp);
    } else {
        PostMessageW(hwnd, WM_JOIN_RESULT, 0, (LPARAM)jp);
    }
    g_connecting = 0;
    return 0;
}

static void log_playback_msg(const char *msg);

static void process_capture(void) {
    short samples[OPUS_FRAME_SIZE];
    uint8_t encoded[400];
    static float dc_state = 0.0f;
    static int noise_gate_frames = 0;
    static int was_speaking = 0;
    /* RMS^2 threshold: (260)^2 = 67600 */
    const int NOISE_FLOOR_SQ = 67600;

    __try {
    while (ringbuf_avail(&g_cap_ring) >= sizeof(samples)) {
        size_t n = ringbuf_pop(&g_cap_ring, (uint8_t*)samples, sizeof(samples));
        if (n < sizeof(samples)) break;

        /* DC offset removal (first-order high-pass, fc ~20Hz @16kHz) */
        for (int i = 0; i < OPUS_FRAME_SIZE; i++) {
            float x = (float)samples[i];
            float y = x - dc_state;
            dc_state = dc_state * 0.995f + (x - dc_state);
            samples[i] = (short)(y > 32767 ? 32767 : (y < -32768 ? -32768 : y));
        }

        /* Noise gate: compare RMS^2 directly (avoid sqrt) */
        long long rms_sq = 0;
        for (int i = 0; i < OPUS_FRAME_SIZE; i++) {
            long long s = samples[i];
            rms_sq += s * s;
        }
        rms_sq /= OPUS_FRAME_SIZE;

        if (rms_sq >= NOISE_FLOOR_SQ) {
            noise_gate_frames = 10;  /* hold open for 10 frames (200ms) */
        } else if (noise_gate_frames > 0) {
            noise_gate_frames--;
        } else {
            /* Send DTX silence packet once when speech ends */
            if (was_speaking && g_encoder && g_npeers > 0) {
                int len = codec_enc_encode(g_encoder, NULL, OPUS_FRAME_SIZE, encoded, sizeof(encoded));
                if (len > 0) {
                    EnterCriticalSection(&g_peer_lock);
                    for (int i = 0; i < g_npeers; i++) {
                        if (!g_peers[i].active) continue;
                        if (g_peers[i].use_relay)
                            signaling_send_relay(&g_sig, g_peers[i].id, encoded, len);
                        else
                            network_send(&g_net, g_peers[i].id, encoded, len);
                    }
                    LeaveCriticalSection(&g_peer_lock);
                }
                was_speaking = 0;
            }
            continue;
        }

        was_speaking = 1;
        if (g_encoder && g_npeers > 0) {
            /* Adaptive bitrate: adjust encoder bitrate based on congestion */
            {
                int br = congestion_get_bitrate(&g_cc);
                codec_enc_set_bitrate(g_encoder, br);
            }
            int len = codec_enc_encode(g_encoder, samples, OPUS_FRAME_SIZE, encoded, sizeof(encoded));
            if (len > 0) {
                congestion_update_sent(&g_cc);
                DWORD now = GetTickCount();
                EnterCriticalSection(&g_peer_lock);
                for (int i = 0; i < g_npeers; i++) {
                    if (!g_peers[i].active) continue;
                    /* Auto-detect: if no UDP received for 3s after join, switch to relay */
                    if (!g_peers[i].use_relay) {
                        DWORD ref_time = g_peers[i].last_udp_recv > 0 ?
                                         g_peers[i].last_udp_recv : g_peers[i].joined_time;
                        if (now - ref_time > 3000) {
                            g_peers[i].use_relay = 1;
                        }
                    }
                    if (g_peers[i].use_relay) {
                        signaling_send_relay(&g_sig, g_peers[i].id, encoded, len);
                    } else {
                        network_send(&g_net, g_peers[i].id, encoded, len);
                    }
                }
                LeaveCriticalSection(&g_peer_lock);
            }
        }
    }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        char log_buf[128];
        _snprintf(log_buf, sizeof(log_buf), "CRASH in process_capture! code=0x%08X", GetExceptionCode());
        log_playback_msg(log_buf);
    }
}

static void log_playback_msg(const char *msg) {
    FILE *f = fopen("D:\\QminiDoctor\\sig_log.txt", "a");
    if (f) { fprintf(f, "[playback] %s\n", msg); fclose(f); }
}

static void process_playback(void) {
    uint8_t data[400];
    short pcm[OPUS_FRAME_SIZE];

    __try {
    EnterCriticalSection(&g_peer_lock);
    for (int i = 0; i < g_npeers; i++) {
        if (!g_peers[i].active) continue;
        if (!g_peers[i].speaker.buffer || !g_peers[i].dec) continue;

        while (g_peers[i].speaker.frames + OPUS_FRAME_SIZE <= g_peers[i].speaker.capacity) {
            int sz = jitter_buffer_pop(&g_peers[i].jb, data, NULL);
            if (sz <= 0) break;

            if (sz < 1 || sz > 400) break;
            int dst_idx = (g_peers[i].speaker.read_pos + g_peers[i].speaker.frames) & SPEAKER_BUF_MASK;
            int frames = codec_dec_decode(g_peers[i].dec, data, sz, pcm, OPUS_FRAME_SIZE, 1);
            if (frames > 0 && frames <= OPUS_FRAME_SIZE) {
                for (int j = 0; j < frames; j++) {
                    g_peers[i].speaker.buffer[(dst_idx + j) & SPEAKER_BUF_MASK] = pcm[j];
                }
                g_peers[i].speaker.frames += frames;
            }
        }
    }

    /* Mix all active speakers and submit to playback thread */
    if (g_npeers > 0) {
        short mix[960];
        int mix_frames = 0;
        for (int i = 0; i < g_npeers; i++)
            if (g_peers[i].active && g_peers[i].speaker.buffer && g_peers[i].speaker.frames > mix_frames)
                mix_frames = g_peers[i].speaker.frames;
        if (mix_frames > 960) mix_frames = 960;

        if (mix_frames > 0) {
            for (int i = 0; i < mix_frames; i++) {
                int sum = 0;
                for (int j = 0; j < g_npeers; j++) {
                    if (g_peers[j].active && g_peers[j].speaker.buffer && g_peers[j].speaker.frames > 0) {
                        sum += g_peers[j].speaker.buffer[g_peers[j].speaker.read_pos];
                        g_peers[j].speaker.read_pos = (g_peers[j].speaker.read_pos + 1) & SPEAKER_BUF_MASK;
                        g_peers[j].speaker.frames--;
                    }
                }
                if (sum > 32767) sum = 32767;
                if (sum < -32768) sum = -32768;
                mix[i] = (short)sum;
            }
            audio_playback_submit(&g_playback, mix, mix_frames);
        }
    }
    LeaveCriticalSection(&g_peer_lock);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        char log_buf[128];
        _snprintf(log_buf, sizeof(log_buf), "CRASH in process_playback! code=0x%08X", GetExceptionCode());
        log_playback_msg(log_buf);
        for (int i = 0; i < g_npeers; i++) {
            g_peers[i].speaker.frames = 0;
            g_peers[i].speaker.read_pos = 0;
        }
    }
}

static void CALLBACK process_timer(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    (void)hwnd; (void)msg; (void)id; (void)time;

    /* Poll PTT key state (WM_HOTKEY only fires on key down, not release) */
    if (g_input_mode == INPUT_MODE_PTT) {
        g_ptt_pressed = (GetAsyncKeyState(g_cfg.ptt_key) & 0x8000) != 0;
    }

    process_capture();
    process_playback();
    if (g_in_room) {
        network_tick(&g_net);
        /* Detect signaling connection loss and clean up */
        if (!signaling_is_connected(&g_sig)) {
            EnterCriticalSection(&g_peer_lock);
            for (int i = 0; i < g_npeers; i++) {
                codec_dec_destroy(g_peers[i].dec);
                jitter_buffer_destroy(&g_peers[i].jb);
                if (g_peers[i].speaker.buffer)
                    HeapFree(GetProcessHeap(), 0, g_peers[i].speaker.buffer);
            }
            g_npeers = 0;
            LeaveCriticalSection(&g_peer_lock);
            signaling_disconnect(&g_sig);
            g_in_room = 0;
            panel_set_members(&g_panel, NULL, 0);
        }
    }
    panel_set_volume(&g_panel, g_peak_pct);

    /* Loopback playback: submit captured audio in chunks */
    if (g_test_playing && g_test_buf && g_test_pos > 0) {
        static int test_play_pos = 0;
        static int test_play_started = 0;
        if (!test_play_started) {
            test_play_pos = 0;
            test_play_started = 1;
        }
        int chunk = 320; /* 20ms */
        if (test_play_pos + chunk <= g_test_pos) {
            audio_playback_submit(&g_playback, g_test_buf + test_play_pos, chunk);
            test_play_pos += chunk;
        } else {
            /* Done playing */
            test_play_started = 0;
            g_test_playing = 0;
            free(g_test_buf);
            g_test_buf = NULL;
        }
    }

    /* Auto-restart audio when Windows switches default device */
    if (g_restart_capture) {
        g_restart_capture = 0;
        audio_capture_stop(&g_capture);
        audio_capture_start(&g_capture, on_capture_frame, NULL);
        /* Refresh device name in tooltip */
        panel_set_muted(&g_panel, g_input_mode == INPUT_MODE_MUTED);
    }
    if (g_restart_playback) {
        g_restart_playback = 0;
        audio_playback_stop(&g_playback);
        audio_playback_start(&g_playback);
        panel_set_muted(&g_panel, g_input_mode == INPUT_MODE_MUTED);
    }
}

/* --- Main --- */

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)cmd; (void)show;

    /* Wrap in SEH to catch and report crashes */
    __try {

    config_load(&g_cfg);

    if (!panel_create(&g_panel, inst)) {
        /* panel_create failed - continuing is safe as long as we check hwnd before using */
    }

    panel_set_input_mode(&g_panel, g_input_mode);
    panel_set_muted(&g_panel, g_input_mode == INPUT_MODE_MUTED);
    hotkey_init(&g_hk, g_panel.hwnd, g_cfg.ptt_key, g_cfg.mute_key);

    g_encoder = codec_enc_create(16000, 1);
    ringbuf_init(&g_cap_ring, g_cap_ring_buf, CAPTURE_RING_SIZE);
    InitializeCriticalSection(&g_peer_lock);
    InitializeCriticalSection(&g_sig.send_lock);

    audio_playback_start(&g_playback);
    g_aec = aec_create();
    network_init(&g_net, 0, on_network_recv, NULL);
    g_net.keepalive_cb = on_keepalive_recv;
    congestion_init(&g_cc);
    if (!audio_capture_start(&g_capture, on_capture_frame, NULL)) {
        MessageBoxW(NULL, L"音频采集初始化失败。\n请检查麦克风设备和权限。", L"Qmini 错误", MB_OK | MB_ICONERROR);
    }

    UINT_PTR timer_id = SetTimer(g_panel.hwnd, 1, 20, process_timer);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_JOIN_RESULT) {
            join_params_t *jp = (join_params_t*)msg.lParam;
            if (msg.wParam == 2) {
                /* Cancelled by user */
                g_svr[0] = 0;
                g_rm[0] = 0;
                panel_set_connection(&g_panel, "", "");
            } else if (msg.wParam == 1) {
                g_in_room = 1;
                update_member_list();
                panel_set_connection(&g_panel, g_svr, g_rm);
            } else if (jp) {
                wchar_t werr[256];
                _snwprintf(werr, 256,
                    L"连接信令服务器失败。\n\n"
                    L"服务器: %hs:%d\n"
                    L"房间: %hs\n"
                    L"本机 UDP 端口: %d\n\n"
                    L"请检查服务器地址和服务器是否正在运行。",
                    jp->host, jp->port, jp->room, jp->sig_port);
                MessageBoxW(NULL, werr, L"Qmini 连接失败", MB_OK | MB_ICONERROR);
                g_svr[0] = 0;
                g_rm[0] = 0;
                panel_set_connection(&g_panel, "", "");
            }
            free(jp);
            continue;
        }
        if (msg.message == WM_HOTKEY) {
            if (msg.wParam == HOTKEY_PTT) {
                int down = (GetAsyncKeyState(g_cfg.ptt_key) & 0x8000) != 0;
                g_ptt_pressed = down;
            } else if (msg.wParam == HOTKEY_MUTE) {
                /* Toggle: muted ? previous mode */
                if (g_input_mode == INPUT_MODE_MUTED) {
                    g_input_mode = g_prev_mode;
                } else {
                    g_prev_mode = g_input_mode;
                    g_input_mode = INPUT_MODE_MUTED;
                }
                panel_set_input_mode(&g_panel, g_input_mode);
                panel_set_muted(&g_panel, g_input_mode == INPUT_MODE_MUTED);
            }
        }

        if (msg.message == WM_COMMAND) {
            WORD cmd_id = LOWORD(msg.wParam);
            if (cmd_id == TRAY_CMD_TOGGLE_MUTE) {
                /* Toggle: muted ? previous mode */
                if (g_input_mode == INPUT_MODE_MUTED) {
                    g_input_mode = g_prev_mode;
                } else {
                    g_prev_mode = g_input_mode;
                    g_input_mode = INPUT_MODE_MUTED;
                }
                panel_set_input_mode(&g_panel, g_input_mode);
                panel_set_muted(&g_panel, g_input_mode == INPUT_MODE_MUTED);
            } else if (cmd_id == TRAY_CMD_MODE_MUTED) {
                if (g_input_mode != INPUT_MODE_MUTED) g_prev_mode = g_input_mode;
                g_input_mode = INPUT_MODE_MUTED;
                panel_set_input_mode(&g_panel, g_input_mode);
                panel_set_muted(&g_panel, 1);
            } else if (cmd_id == TRAY_CMD_MODE_PTT) {
                g_input_mode = INPUT_MODE_PTT;
                panel_set_input_mode(&g_panel, g_input_mode);
                panel_set_muted(&g_panel, 0);
            } else if (cmd_id == TRAY_CMD_MODE_OPEN) {
                g_input_mode = INPUT_MODE_OPEN;
                panel_set_input_mode(&g_panel, g_input_mode);
                panel_set_muted(&g_panel, 0);
            } else if (cmd_id == TRAY_CMD_JOIN_ROOM) {
                if (!g_in_room && !g_connecting) {
                    char server[64] = {0};
                    char room[32] = "default";
                    char nick[32] = {0};
                    char password[64] = {0};
                    strncpy(server, g_cfg.server_addr, sizeof(server) - 1);
                    strncpy(nick, g_cfg.nickname, sizeof(nick) - 1);

                    if (join_dialog_show(g_panel.inst, g_panel.hwnd,
                                         server, sizeof(server),
                                         room, sizeof(room),
                                         nick, sizeof(nick),
                                         password, sizeof(password))) {
                        /* Initialize encryption from password */
                        if (password[0]) {
                            crypto_init_from_password(&g_crypto, password);
                            network_set_crypto(&g_net, &g_crypto);
                        }

                        join_params_t *jp = (join_params_t*)malloc(sizeof(join_params_t));
                        if (!jp) continue;
                        memset(jp, 0, sizeof(*jp));
                        if (sscanf(server, "%63[^:]:%d", jp->host, &jp->port) < 2)
                            jp->port = 9088;
                        strncpy(jp->room, room, sizeof(jp->room) - 1);
                        strncpy(jp->nick, nick, sizeof(jp->nick) - 1);
                        jp->sig_port = network_get_port(&g_net);

                        /* Set callbacks before connect: server may send PEER_JOIN immediately */
                        g_sig.peer_join_cb = on_peer_join;
                        g_sig.peer_leave_cb = on_peer_leave;
                        g_sig.ice_cb = on_ice_msg;
                        g_sig.relay_cb = on_relay_recv;
                        g_sig.user_data = NULL;

                        /* Save config so UI reflects intent */
                        strncpy(g_cfg.server_addr, server, sizeof(g_cfg.server_addr) - 1);
                        strncpy(g_cfg.nickname, nick, sizeof(g_cfg.nickname) - 1);
                        config_save(&g_cfg);

                        /* Tooltip shows connecting state */
                        strncpy(g_svr, server, sizeof(g_svr) - 1);
                        strncpy(g_rm, room, sizeof(g_rm) - 1);
                        panel_set_connection(&g_panel, g_svr, g_rm);

                        g_cancel_connect = 0;
                        g_connecting = 1;
                        HANDLE h = CreateThread(NULL, 0, join_thread, jp, 0, NULL);
                        CloseHandle(h);
                    }
                }
            } else if (cmd_id == TRAY_CMD_LEAVE_ROOM) {
                if (g_connecting) {
                    /* Cancel pending connection */
                    g_cancel_connect = 1;
                    g_connecting = 0;
                    g_svr[0] = 0;
                    g_rm[0] = 0;
                    panel_set_connection(&g_panel, "", "");
                } else if (g_in_room) {
                    /* Clear crypto context */
                    memset(&g_crypto, 0, sizeof(g_crypto));
                    network_set_crypto(&g_net, NULL);

                    EnterCriticalSection(&g_peer_lock);
                    for (int i = 0; i < g_npeers; i++) {
                        codec_dec_destroy(g_peers[i].dec);
                        jitter_buffer_destroy(&g_peers[i].jb);
                        if (g_peers[i].speaker.buffer)
                            HeapFree(GetProcessHeap(), 0, g_peers[i].speaker.buffer);
                    }
                    g_npeers = 0;
                    LeaveCriticalSection(&g_peer_lock);

                    signaling_disconnect(&g_sig);
                    g_in_room = 0;
                    g_svr[0] = 0;
                    g_rm[0] = 0;
                    panel_set_connection(&g_panel, "", "");
                    panel_set_members(&g_panel, NULL, 0);
                }
            } else if (cmd_id == TRAY_CMD_TEST_AUDIO) {
                /* Step 1: Test tone to verify output */
                short tone[16000];
                for (int i = 0; i < 16000; i++) {
                    double val = sin(2.0 * 3.141592653589793 * 440.0 * i / 16000.0);
                    tone[i] = (short)(val * 8000.0);
                }
                audio_playback_submit(&g_playback, tone, 16000);

                /* Step 2: Loopback test - capture 1.5s of mic, then play back */
                if (g_test_buf) free(g_test_buf);
                g_test_buf = (short*)malloc(24000 * sizeof(short));
                if (g_test_buf) {
                    g_test_pos = 0;
                    g_test_max = 24000;
                    g_test_capturing = 1;
                    g_test_playing = 0;
                }

                MessageBoxW(NULL,
                    L"正在播放测试音...你应该听到 1 秒蜂鸣声。\n\n"
                    L"然后对着麦克风说话 1.5 秒，"
                    L"之后会回放你的声音。\n\n"
                    L"测试流程：蜂鸣声 → 录音 → 回放",
                    L"音频测试", MB_OK | MB_ICONINFORMATION);
            } else if (cmd_id == TRAY_CMD_AUDIO_DEVICES) {
                MessageBoxW(NULL,
                    L"输入设备和输出设备由 Windows 声音设置管理。\nQmini 自动跟随默认通信设备。",
                    L"音频设备", MB_OK | MB_ICONINFORMATION);
            } else if (cmd_id == TRAY_CMD_EXIT) {
                break;
            }
            continue;  /* skip DispatchMessage: panel_wndproc re-posts WM_COMMAND */
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    KillTimer(g_panel.hwnd, timer_id);
    audio_capture_stop(&g_capture);
    aec_destroy(g_aec);
    g_aec = NULL;
    audio_playback_stop(&g_playback);
    if (g_in_room) signaling_disconnect(&g_sig);   /* TCP before WSACleanup */
    network_close(&g_net);                          /* UDP + WSACleanup last */
    if (g_encoder) codec_enc_destroy(g_encoder);

    EnterCriticalSection(&g_peer_lock);
    for (int i = 0; i < g_npeers; i++) {
        codec_dec_destroy(g_peers[i].dec);
        jitter_buffer_destroy(&g_peers[i].jb);
        if (g_peers[i].speaker.buffer)
            HeapFree(GetProcessHeap(), 0, g_peers[i].speaker.buffer);
    }
    g_npeers = 0;
    LeaveCriticalSection(&g_peer_lock);
    DeleteCriticalSection(&g_peer_lock);
    DeleteCriticalSection(&g_sig.send_lock);

    hotkey_destroy(&g_hk);
    panel_destroy(&g_panel);
    notify_shutdown(NULL, NULL);
    return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        MessageBoxW(NULL, L"Qmini 遇到错误，需要关闭。", L"Qmini 错误", MB_OK | MB_ICONERROR);
        return 1;
    }
}
