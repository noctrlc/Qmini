#include "hotkey.h"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

int hotkey_init(hotkey_t *hk, HWND hwnd, int ptt_key, int mute_key) {
    hk->hwnd     = hwnd;
    hk->ptt_key  = ptt_key;
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
