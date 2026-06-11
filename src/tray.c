#include "tray.h"
#include "audio_capture.h"
#include "audio_playback.h"
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>

#define TRAY_ICON_ID 1

static HICON create_icon(COLORREF color) {
    HDC dc = GetDC(NULL);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, 16, 16);
    SelectObject(mem, bmp);

    RECT r = {0, 0, 16, 16};
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(mem, &r, brush);
    DeleteObject(brush);

    brush = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(mem, brush);
    PatBlt(mem, 2, 4, 4, 8, PATCOPY);
    PatBlt(mem, 6, 2, 3, 12, PATCOPY);
    DeleteObject(brush);
    DeleteDC(mem);

    ICONINFO ii;
    ii.fIcon    = TRUE;
    ii.hbmMask  = bmp;
    ii.hbmColor = bmp;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(bmp);
    ReleaseDC(NULL, dc);
    return icon;
}

LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    tray_t *t = (tray_t*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (t && msg == t->wm_trayicon) {
        if (l == WM_RBUTTONDOWN) {
            SetForegroundWindow(hwnd);
            POINT pt;
            GetCursorPos(&pt);
            TrackPopupMenu(t->menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        }
        if (l == WM_LBUTTONDBLCLK) {
            PostMessageW(hwnd, WM_COMMAND, TRAY_CMD_TOGGLE_MUTE, 0);
            return 0;
        }
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(w) == TRAY_CMD_EXIT) {
            PostQuitMessage(0);
            return 0;
        }
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

int tray_create(tray_t *t, HINSTANCE inst) {
    static int class_registered = 0;

    if (!class_registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = tray_wndproc;
        wc.hInstance = inst;
        wc.lpszClassName = L"QminiTrayWindow";
        RegisterClassW(&wc);
        class_registered = 1;
    }

    t->wm_trayicon = WM_APP + 1;
    t->hwnd = CreateWindowW(L"QminiTrayWindow", L"Qmini", WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, NULL, NULL, inst, NULL);
    if (!t->hwnd) return 0;
    SetWindowLongPtrW(t->hwnd, GWLP_USERDATA, (LONG_PTR)t);

    t->icon        = create_icon(RGB(0, 140, 200));
    t->icon_active = create_icon(RGB(0, 180, 80));
    t->active = 0;
    t->peak_pct = 0;

    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = t->hwnd;
    nid.uID    = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = t->wm_trayicon;
    wcscpy_s(nid.szTip, 128, L"Qmini 语音");
    nid.hIcon  = t->icon;
    Shell_NotifyIconW(NIM_ADD, &nid);

    t->menu = CreatePopupMenu();
    AppendMenuW(t->menu, MF_STRING, TRAY_CMD_JOIN_ROOM,    L"加入房间...");
    AppendMenuW(t->menu, MF_STRING, TRAY_CMD_LEAVE_ROOM,   L"离开房间");
    t->member_menu = CreatePopupMenu();
    AppendMenuW(t->menu, MF_POPUP, (UINT_PTR)t->member_menu, L"房间成员");
    EnableMenuItem(t->menu, (UINT)(UINT_PTR)t->member_menu, MF_BYCOMMAND | MF_GRAYED);
    AppendMenuW(t->menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(t->menu, MF_STRING, TRAY_CMD_TOGGLE_MUTE,  L"切换静音");
    AppendMenuW(t->menu, MF_SEPARATOR, 0, NULL);

    t->mode_menu = CreatePopupMenu();
    AppendMenuW(t->mode_menu, MF_STRING, TRAY_CMD_MODE_MUTED, L"静音");
    AppendMenuW(t->mode_menu, MF_STRING, TRAY_CMD_MODE_PTT,   L"按键说话");
    AppendMenuW(t->mode_menu, MF_STRING, TRAY_CMD_MODE_OPEN,  L"自由发言");
    AppendMenuW(t->menu, MF_POPUP, (UINT_PTR)t->mode_menu, L"输入模式");
    AppendMenuW(t->menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(t->menu, MF_STRING, TRAY_CMD_TEST_AUDIO, L"音频测试");
    AppendMenuW(t->menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(t->menu, MF_STRING, TRAY_CMD_AUDIO_DEVICES, L"音频设备...");
    AppendMenuW(t->menu, MF_SEPARATOR, 0, NULL);

    AppendMenuW(t->menu, MF_STRING, TRAY_CMD_EXIT,         L"退出");

    t->muted = 0;
    t->inst = inst;
    return 1;
}

void tray_destroy(tray_t *t) {
    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd = t->hwnd;
    nid.uID  = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (t->icon) DestroyIcon(t->icon);
    if (t->icon_active) DestroyIcon(t->icon_active);
    /* BUG FIX: destroy submenus explicitly before parent menu */
    if (t->mode_menu) { DestroyMenu(t->mode_menu); t->mode_menu = NULL; }
    if (t->member_menu) { DestroyMenu(t->member_menu); t->member_menu = NULL; }
    if (t->menu) { DestroyMenu(t->menu); t->menu = NULL; }
    if (t->hwnd) DestroyWindow(t->hwnd);
}

void tray_set_input_mode(tray_t *t, int mode) {
    CheckMenuRadioItem(t->mode_menu, TRAY_CMD_MODE_MUTED, TRAY_CMD_MODE_OPEN,
                       TRAY_CMD_MODE_MUTED + mode, MF_BYCOMMAND);
}

void tray_set_volume(tray_t *t, int peak_pct) {
    t->peak_pct = peak_pct;
    int is_active = (peak_pct > 5);

    if (is_active != t->active) {
        t->active = is_active;
        NOTIFYICONDATAW nid = {0};
        nid.cbSize = sizeof(nid);
        nid.hWnd = t->hwnd;
        nid.uID  = TRAY_ICON_ID;
        nid.uFlags = NIF_ICON;
        nid.hIcon = is_active ? t->icon_active : t->icon;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    /* Update tooltip with volume level and member count */
    {
        NOTIFYICONDATAW nid = {0};
        nid.cbSize = sizeof(nid);
        nid.hWnd = t->hwnd;
        nid.uID  = TRAY_ICON_ID;
        nid.uFlags = NIF_TIP;
        if (t->room[0]) {
            _snwprintf(nid.szTip, 128,
                L"Qmini [%hs] @ %hs\n音量: %d%% | 在线: %d 人%hs\n%hs\n%hs",
                t->room, t->server, peak_pct, t->member_count,
                t->muted ? " [已静音]" : "",
                audio_capture_get_device_name(),
                audio_playback_get_device_name());
        } else {
            _snwprintf(nid.szTip, 128, L"Qmini | 音量: %d%%%hs\n%hs\n%hs",
                peak_pct,
                t->muted ? " [已静音]" : "",
                audio_capture_get_device_name(),
                audio_playback_get_device_name());
        }
        /* BUG FIX: ensure null-termination after _snwprintf */
        nid.szTip[127] = L'\0';
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
}

void tray_set_connection(tray_t *t, const char *server, const char *room) {
    if (server) {
        strncpy(t->server, server, sizeof(t->server) - 1);
        t->server[sizeof(t->server) - 1] = 0;
    }
    if (room) {
        strncpy(t->room, room, sizeof(t->room) - 1);
        t->room[sizeof(t->room) - 1] = 0;
    }
    tray_set_muted(t, t->muted);
}

void tray_set_muted(tray_t *t, int muted) {
    t->muted = muted;
    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(nid);
    nid.hWnd = t->hwnd;
    nid.uID  = TRAY_ICON_ID;
    nid.uFlags = NIF_TIP;

    if (t->room[0]) {
        _snwprintf(nid.szTip, 128,
            L"Qmini [%hs] @ %hs\n在线: %d 人%hs\n%hs\n%hs",
            t->room, t->server, t->member_count,
            muted ? " [已静音]" : "",
            audio_capture_get_device_name(),
            audio_playback_get_device_name());
    } else {
        _snwprintf(nid.szTip, 128, L"Qmini 语音%hs\n%hs\n%hs",
            muted ? " [已静音]" : "",
            audio_capture_get_device_name(),
            audio_playback_get_device_name());
    }
    /* BUG FIX: ensure null-termination after _snwprintf */
    nid.szTip[127] = L'\0';
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void tray_set_members(tray_t *t, const char *names[], int count) {
    t->member_count = count;

    /* Clear existing member menu items */
    while (RemoveMenu(t->member_menu, 0, MF_BYPOSITION));

    if (count == 0) {
        AppendMenuW(t->member_menu, MF_STRING | MF_GRAYED, 0, L"(自己，无其他成员)");
        EnableMenuItem(t->menu, (UINT)(UINT_PTR)t->member_menu, MF_BYCOMMAND | MF_ENABLED);
    } else {
        for (int i = 0; i < count; i++) {
            wchar_t wbuf[64];
            /* BUG FIX: use MultiByteToWideChar for proper UTF-8 support */
            MultiByteToWideChar(CP_UTF8, 0, names[i], -1, wbuf, 64);
            wbuf[63] = L'\0';
            AppendMenuW(t->member_menu, MF_STRING, 0, wbuf);
        }
        EnableMenuItem(t->menu, (UINT)(UINT_PTR)t->member_menu, MF_BYCOMMAND | MF_ENABLED);
    }

    /* Refresh tooltip */
    tray_set_muted(t, t->muted);
}
