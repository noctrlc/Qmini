#include "hotkey.h"
#include "logger.h"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

int hotkey_init(hotkey_t *hk, HWND hwnd, int ptt_key, int mute_key) {
    hk->hwnd     = hwnd;
    hk->ptt_key  = ptt_key;
    hk->mute_key = mute_key;
    hk->ptt_down = 0;
    hk->muted    = 0;

    /* BUG FIX: check RegisterHotKey return value */
    if (!RegisterHotKey(hwnd, HOTKEY_PTT, MOD_NOREPEAT, ptt_key)) {
        LOG_WARN("RegisterHotKey failed for PTT key %d (error %lu)", ptt_key, GetLastError());
    }
    if (!RegisterHotKey(hwnd, HOTKEY_MUTE, MOD_CONTROL | MOD_NOREPEAT, mute_key)) {
        LOG_WARN("RegisterHotKey failed for mute key %d (error %lu)", mute_key, GetLastError());
    }
    return 1;
}

void hotkey_destroy(hotkey_t *hk) {
    UnregisterHotKey(hk->hwnd, HOTKEY_PTT);
    UnregisterHotKey(hk->hwnd, HOTKEY_MUTE);
}

void hotkey_set_mute(hotkey_t *hk, int muted) {
    hk->muted = muted;
}
