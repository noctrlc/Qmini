#include "panel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BOOL CALLBACK enum_set_font(HWND hwnd, LPARAM lParam) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

LRESULT CALLBACK panel_wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    panel_t *p = (panel_t*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_COMMAND:
        /* Forward to message queue so main.c intercepts it */
        PostMessageW(hwnd, WM_COMMAND, w, l);
        return 0;
    case WM_CLOSE:
        PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_EXIT, 0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CTLCOLORSTATIC:
        if (p && (HWND)l == GetDlgItem(hwnd, IDC_BRAND_LABEL)) {
            HDC hdc = (HDC)w;
            SetTextColor(hdc, RGB(70, 160, 230));
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        if (p && (HWND)l == GetDlgItem(hwnd, IDC_MIC_BAR)) {
            HDC hdc = (HDC)w;
            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)(p->peak_pct > 5 ? p->hbrush_mic_on : p->hbrush_mic_bg);
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

int panel_create(panel_t *p, HINSTANCE inst) {
    static int registered = 0;

    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc   = panel_wndproc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"QminiPanelWindow";
        RegisterClassW(&wc);
        registered = 1;
    }

    p->hwnd = CreateWindowExW(0, L"QminiPanelWindow",
        L"Qmini 控制面板",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 326, 415,
        NULL, NULL, inst, NULL);
    if (!p->hwnd) return 0;

    SetWindowLongPtrW(p->hwnd, GWLP_USERDATA, (LONG_PTR)p);
    p->inst = inst;
    p->muted = 0;
    p->peak_pct = 0;
    p->member_count = 0;
    p->server[0] = 0;
    p->room[0] = 0;

    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    p->hfont = CreateFontIndirectW(&ncm.lfMessageFont);
    p->hfont_brand = CreateFontW(22, 0, 0, 0, FW_BOLD, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_SCRIPT, L"Segoe Script");
    p->hbrush_mic_bg = CreateSolidBrush(RGB(60, 60, 60));
    p->hbrush_mic_on = CreateSolidBrush(RGB(0, 200, 60));

    HWND pw = p->hwnd;
    HINSTANCE hi = inst;

    /* Group: 连接状态 = 连接状态 */
    CreateWindowExW(0, L"BUTTON", L"连接状态",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        5, 2, 300, 68, pw, (HMENU)IDC_STATUS_GROUP, hi, NULL);

    /* 服务器: = 服务器: */
    CreateWindowExW(0, L"STATIC", L"服务器:",
        WS_CHILD | WS_VISIBLE, 12, 18, 45, 14, pw, (HMENU)IDC_SERVER_LBL, hi, NULL);
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
        60, 18, 235, 14, pw, (HMENU)IDC_SERVER_VAL, hi, NULL);

    /* 房间: = 房间: */
    CreateWindowExW(0, L"STATIC", L"房间:",
        WS_CHILD | WS_VISIBLE, 12, 34, 45, 14, pw, (HMENU)IDC_ROOM_LBL, hi, NULL);
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
        60, 34, 235, 14, pw, (HMENU)IDC_ROOM_VAL, hi, NULL);

    /* 成员: 0 人在线 = 成员: 0 人在线 */
    CreateWindowExW(0, L"STATIC", L"成员: 0 人在线",
        WS_CHILD | WS_VISIBLE, 12, 50, 200, 14, pw, (HMENU)IDC_MEMBERS_LBL, hi, NULL);

    /* Brand label - right side of connection status box */
    CreateWindowExW(0, L"STATIC", L"Mlaiou",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        210, 26, 92, 30, pw, (HMENU)IDC_BRAND_LABEL, hi, NULL);

    /* Group: 麦克风 = 麦克风 */
    CreateWindowExW(0, L"BUTTON", L"麦克风",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        5, 72, 300, 65, pw, (HMENU)IDC_MIC_GROUP, hi, NULL);

    /* 麦克风音量 = 麦克风音量 */
    CreateWindowExW(0, L"STATIC", L"麦克风音量",
        WS_CHILD | WS_VISIBLE, 12, 90, 55, 14, pw, NULL, hi, NULL);

    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_SIMPLE,
        70, 88, 105, 14, pw, (HMENU)IDC_MIC_BAR, hi, NULL);

    CreateWindowExW(0, L"STATIC", L"0%",
        WS_CHILD | WS_VISIBLE, 180, 90, 35, 14, pw, (HMENU)IDC_MIC_PCT, hi, NULL);

    /* Radio buttons: WS_GROUP on first creates a radio group */
    /* 静音 = 静音 */
    CreateWindowExW(0, L"BUTTON", L"静音",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
        12, 112, 55, 16, pw, (HMENU)PANEL_CMD_MODE_MUTED, hi, NULL);

    /* 按键说话 = 按键说话 */
    CreateWindowExW(0, L"BUTTON", L"按键说话",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        70, 112, 72, 16, pw, (HMENU)PANEL_CMD_MODE_PTT, hi, NULL);

    /* 自由发言 = 自由发言 */
    CreateWindowExW(0, L"BUTTON", L"自由发言",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        145, 112, 72, 16, pw, (HMENU)PANEL_CMD_MODE_OPEN, hi, NULL);

    /* 切换静音 = 切换静音 */
    CreateWindowExW(0, L"BUTTON", L"切换静音",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        225, 110, 68, 20, pw, (HMENU)PANEL_CMD_TOGGLE_MUTE, hi, NULL);

    /* Group: 房间成员 = 房间成员 */
    CreateWindowExW(0, L"BUTTON", L"房间成员",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        5, 140, 300, 115, pw, (HMENU)IDC_MEMBERS_GROUP, hi, NULL);

    CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
        12, 156, 286, 84, pw, (HMENU)IDC_MEMBER_LIST, hi, NULL);

    /* Action buttons */
    /* 加入房间 = 加入房间 */
    CreateWindowExW(0, L"BUTTON", L"加入房间",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 264, 56, 24, pw, (HMENU)PANEL_CMD_JOIN_ROOM, hi, NULL);

    /* 离开房间 = 离开房间 */
    CreateWindowExW(0, L"BUTTON", L"离开房间",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        70, 264, 56, 24, pw, (HMENU)PANEL_CMD_LEAVE_ROOM, hi, NULL);

    /* 音频测试 = 音频测试 */
    CreateWindowExW(0, L"BUTTON", L"音频测试",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        130, 264, 56, 24, pw, (HMENU)PANEL_CMD_TEST_AUDIO, hi, NULL);

    /* 音频设备 = 音频设备 */
    CreateWindowExW(0, L"BUTTON", L"音频设备",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        190, 264, 56, 24, pw, (HMENU)PANEL_CMD_AUDIO_DEVICES, hi, NULL);

    /* 退出 = 退出 */
    CreateWindowExW(0, L"BUTTON", L"退出",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        250, 264, 50, 24, pw, (HMENU)PANEL_CMD_EXIT, hi, NULL);

    EnumChildWindows(p->hwnd, enum_set_font, (LPARAM)p->hfont);
    /* Override brand label with artistic font */
    SendMessageW(GetDlgItem(p->hwnd, IDC_BRAND_LABEL), WM_SETFONT, (WPARAM)p->hfont_brand, TRUE);

    /* Default: PTT selected */
    CheckRadioButton(p->hwnd, PANEL_CMD_MODE_MUTED, PANEL_CMD_MODE_OPEN, PANEL_CMD_MODE_PTT);

    ShowWindow(p->hwnd, SW_SHOW);
    UpdateWindow(p->hwnd);
    return 1;
}

void panel_destroy(panel_t *p) {
    if (p->hfont) DeleteObject(p->hfont);
    if (p->hfont_brand) DeleteObject(p->hfont_brand);
    if (p->hbrush_mic_bg) DeleteObject(p->hbrush_mic_bg);
    if (p->hbrush_mic_on) DeleteObject(p->hbrush_mic_on);
    if (p->hwnd) DestroyWindow(p->hwnd);
    memset(p, 0, sizeof(*p));
}

void panel_set_muted(panel_t *p, int muted) {
    p->muted = muted;
    SetDlgItemTextW(p->hwnd, PANEL_CMD_TOGGLE_MUTE,
        muted ? L"已静音" : L"切换静音");
    /* "已静音" : "切换静音" */
}

void panel_set_input_mode(panel_t *p, int mode) {
    int ids[] = { PANEL_CMD_MODE_MUTED, PANEL_CMD_MODE_PTT, PANEL_CMD_MODE_OPEN };
    CheckRadioButton(p->hwnd, PANEL_CMD_MODE_MUTED, PANEL_CMD_MODE_OPEN, ids[mode]);
    int muted = (mode == INPUT_MODE_MUTED);
    if (p->muted != muted) {
        p->muted = muted;
        SetDlgItemTextW(p->hwnd, PANEL_CMD_TOGGLE_MUTE,
            muted ? L"已静音" : L"切换静音");
    }
}

void panel_set_connection(panel_t *p, const char *server, const char *room) {
    if (server) {
        strncpy(p->server, server, sizeof(p->server) - 1);
        p->server[sizeof(p->server) - 1] = 0;
    }
    if (room) {
        strncpy(p->room, room, sizeof(p->room) - 1);
        p->room[sizeof(p->room) - 1] = 0;
    }

    wchar_t wbuf[256];
    MultiByteToWideChar(CP_ACP, 0, p->server, -1, wbuf, 256);
    SetDlgItemTextW(p->hwnd, IDC_SERVER_VAL, wbuf);
    MultiByteToWideChar(CP_ACP, 0, p->room, -1, wbuf, 256);
    SetDlgItemTextW(p->hwnd, IDC_ROOM_VAL, wbuf);

    wchar_t cnt[64];
    _snwprintf(cnt, 64, L"成员: %d 人在线", p->member_count);
    SetDlgItemTextW(p->hwnd, IDC_MEMBERS_LBL, cnt);

    panel_set_muted(p, p->muted);
}

void panel_set_volume(panel_t *p, int peak_pct) {
    p->peak_pct = peak_pct;
    wchar_t buf[16];
    _snwprintf(buf, 16, L"%d%%", peak_pct);
    SetDlgItemTextW(p->hwnd, IDC_MIC_PCT, buf);

    /* Resize mic bar proportionally */
    HWND bar = GetDlgItem(p->hwnd, IDC_MIC_BAR);
    int bar_w = (peak_pct * 105) / 100;
    if (bar_w < 2) bar_w = 2;
    SetWindowPos(bar, NULL, 0, 0, bar_w, 14, SWP_NOZORDER | SWP_NOMOVE);
    InvalidateRect(bar, NULL, TRUE);
}

void panel_set_members(panel_t *p, const char *names[], int count) {
    p->member_count = count;
    HWND lb = GetDlgItem(p->hwnd, IDC_MEMBER_LIST);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);

    for (int i = 0; i < count; i++) {
        wchar_t wbuf[64];
        MultiByteToWideChar(CP_UTF8, 0, names[i], -1, wbuf, 64);
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)wbuf);
    }

    wchar_t cnt[64];
    _snwprintf(cnt, 64, L"成员: %d 人在线", count);
    SetDlgItemTextW(p->hwnd, IDC_MEMBERS_LBL, cnt);
}
