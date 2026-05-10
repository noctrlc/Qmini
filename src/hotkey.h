#ifndef HOTKEY_H
#define HOTKEY_H

#define _WIN32_WINNT 0x0600
#include <windows.h>

#define HOTKEY_PTT    1
#define HOTKEY_MUTE   2

typedef struct {
    HWND   hwnd;
    int    ptt_key;
    int    mute_key;
    int    ptt_down;
    int    muted;
} hotkey_t;

int  hotkey_init(hotkey_t *hk, HWND hwnd, int ptt_key, int mute_key);
void hotkey_destroy(hotkey_t *hk);
void hotkey_set_mute(hotkey_t *hk, int muted);

#endif
