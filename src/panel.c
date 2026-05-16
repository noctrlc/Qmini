#include "panel.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_W        350
#define PANEL_H_COL    40
#define PANEL_H_MEM    160
#define PANEL_H_JOIN   250

/* Internal control IDs (3000+ to avoid collision with panel_cmd_t 1-9) */
#define IDC_BRAND       3000
#define IDC_STATUS      3001
#define IDC_MEMBER_BTN  3002
#define IDC_SPEAKER_TAG 3003
#define IDC_MODE_MUTED  3004
#define IDC_MODE_PTT    3005
#define IDC_MODE_OPEN   3006
#define IDC_LEAVE       3007
#define IDC_JOIN        3008
#define IDC_CANCEL      3009
#define IDC_PIN         3010
#define IDC_EXPAND_INFO 3020
#define IDC_SERVER_EDIT 3013
#define IDC_ROOM_EDIT   3014
#define IDC_NICK_EDIT   3015
#define IDC_PASS_EDIT   3016
#define IDC_JOIN_OK     3017
#define IDC_JOIN_CANCEL 3018

static int  g_expanded = 0;  /* 0=collapsed, 1=members, 2=join */
static int  g_pinned = 0;
static HWND g_children_join[10];
static HWND g_children_mem[4];
static int  g_n_join = 0, g_n_mem = 0;

static void show_kids(HWND *kids, int n, int show) {
    for (int i = 0; i < n; i++) ShowWindow(kids[i], show ? SW_SHOW : SW_HIDE);
}

static void set_expand(panel_t *p, int mode) {
    g_expanded = mode;
    int h = (mode == 1) ? PANEL_H_MEM : (mode == 2) ? PANEL_H_JOIN : PANEL_H_COL;
    show_kids(g_children_mem, g_n_mem, mode == 1);
    show_kids(g_children_join, g_n_join, mode == 2);
    SetWindowPos(p->hwnd, NULL, 0, 0, PANEL_W, h, SWP_NOZORDER | SWP_NOMOVE);
}

LRESULT CALLBACK panel_wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    panel_t *p = (panel_t*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_COMMAND: {
        WORD id = LOWORD(w);
        if (id == IDC_MEMBER_BTN && p && p->member_count > 0) {
            set_expand(p, g_expanded == 1 ? 0 : 1);
            return 0;
        }
        if (id == IDC_JOIN && p && !g_expanded) {
            set_expand(p, 2);
            return 0;
        }
        if (id == IDC_JOIN_CANCEL) {
            set_expand(p, 0);
            return 0;
        }
        if (id == IDC_PIN) {
            g_pinned = !g_pinned;
            SetWindowPos(hwnd, g_pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOMOVE);
            return 0;
        }
        if (id == IDC_JOIN_OK) {
            set_expand(p, 0);
            PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_JOIN_ROOM, 0);
            return 0;
        }
        if (id == IDC_CANCEL) {
            PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_LEAVE_ROOM, 0);
            return 0;
        }
        /* Map mode/leave buttons to panel_cmd_t */
        int cmd = 0;
        switch (id) {
        case IDC_LEAVE:      cmd = PANEL_CMD_LEAVE_ROOM; break;
        case IDC_MODE_MUTED: cmd = PANEL_CMD_MODE_MUTED; break;
        case IDC_MODE_PTT:   cmd = PANEL_CMD_MODE_PTT;   break;
        case IDC_MODE_OPEN:  cmd = PANEL_CMD_MODE_OPEN;  break;
        }
        if (cmd) { PostMessageW(hwnd, WM_COMMAND, cmd, 0); return 0; }
        /* Forward everything else to main loop */
        PostMessageW(hwnd, WM_COMMAND, w, l);
        return 0;
    }
    case WM_CLOSE:
        PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_EXIT, 0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CTLCOLORSTATIC: {
        /* Dark text on light background for labels inside expand area */
        HDC hdc = (HDC)w;
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, w, l);
        if (hit == HTCLIENT) return HTCAPTION;
        return hit;
    }
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
        wc.lpszClassName = L"QminiHUDPanel";
        RegisterClassW(&wc);
        registered = 1;
    }

    p->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"QminiHUDPanel",
        L"Qmini",
        WS_POPUP | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, PANEL_W, PANEL_H_COL,
        NULL, NULL, inst, NULL);
    if (!p->hwnd) return 0;

    SetWindowLongPtrW(p->hwnd, GWLP_USERDATA, (LONG_PTR)p);
    p->inst = inst;
    p->muted = 0;
    p->peak_pct = 0;
    p->member_count = 0;
    p->server[0] = 0;
    p->room[0] = 0;

    HWND pw = p->hwnd;
    HINSTANCE hi = inst;

    /* Row 1: status bar (y=4, h=14) */
    CreateWindowExW(0, L"STATIC", L"Qmini",
        WS_CHILD | WS_VISIBLE, 4, 5, 30, 14, pw, (HMENU)IDC_BRAND, hi, NULL);
    CreateWindowExW(0, L"STATIC", L"○ 未连接",
        WS_CHILD | WS_VISIBLE, 38, 5, 80, 14, pw, (HMENU)IDC_STATUS, hi, NULL);
    /* Member button (hidden until connected) */
    HWND mbtn = CreateWindowExW(0, L"BUTTON", L"👥 0▾",
        WS_CHILD | BS_PUSHBUTTON, 120, 3, 56, 16, pw, (HMENU)IDC_MEMBER_BTN, hi, NULL);
    ShowWindow(mbtn, SW_HIDE);
    /* Speaker tag (hidden initially) */
    HWND stag = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_CENTER, 178, 5, 80, 14, pw, (HMENU)IDC_SPEAKER_TAG, hi, NULL);
    ShowWindow(stag, SW_HIDE);
    /* Mlaiou brand */
    CreateWindowExW(0, L"STATIC", L"Mlaiou",
        WS_CHILD | WS_VISIBLE | SS_RIGHT, 260, 5, 50, 14, pw, NULL, hi, NULL);

    /* Right-side buttons */
    CreateWindowExW(0, L"BUTTON", L"+ 加入",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 278, 3, 60, 18, pw, (HMENU)IDC_JOIN, hi, NULL);
    HWND canc = CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD | BS_PUSHBUTTON, 290, 3, 50, 18, pw, (HMENU)IDC_CANCEL, hi, NULL);
    ShowWindow(canc, SW_HIDE);
    HWND pinb = CreateWindowExW(0, L"BUTTON", L"📌",
        WS_CHILD | BS_PUSHBUTTON, 238, 3, 22, 18, pw, (HMENU)IDC_PIN, hi, NULL);
    ShowWindow(pinb, SW_HIDE);

    /* Row 2: mode bar + leave (hidden until connected, y=22, h=18) */
    HWND mute = CreateWindowExW(0, L"BUTTON", L"🔇",
        WS_CHILD | BS_PUSHBUTTON, 4, 22, 42, 16, pw, (HMENU)IDC_MODE_MUTED, hi, NULL);
    HWND ptt  = CreateWindowExW(0, L"BUTTON", L"🎤 说话",
        WS_CHILD | BS_PUSHBUTTON, 48, 22, 74, 16, pw, (HMENU)IDC_MODE_PTT, hi, NULL);
    HWND open_m = CreateWindowExW(0, L"BUTTON", L"📢",
        WS_CHILD | BS_PUSHBUTTON, 124, 22, 42, 16, pw, (HMENU)IDC_MODE_OPEN, hi, NULL);
    HWND leave = CreateWindowExW(0, L"BUTTON", L"✕ 离开",
        WS_CHILD | BS_PUSHBUTTON, 290, 22, 54, 16, pw, (HMENU)IDC_LEAVE, hi, NULL);
    ShowWindow(mute, SW_HIDE);
    ShowWindow(ptt, SW_HIDE);
    ShowWindow(open_m, SW_HIDE);
    ShowWindow(leave, SW_HIDE);

    /* === Expand area: members (hidden, starts at y=PANEL_H_COL) === */
    int ey = PANEL_H_COL;
    HWND minfo = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_BORDER, 6, ey + 2, 338, 14, pw, (HMENU)IDC_EXPAND_INFO, hi, NULL);
    HWND mlist = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
        6, ey + 18, 338, 96, pw, (HMENU)IDC_MEMBER_LIST, hi, NULL);
    ShowWindow(minfo, SW_HIDE);
    ShowWindow(mlist, SW_HIDE);
    g_children_mem[0] = minfo;
    g_children_mem[1] = mlist;
    g_n_mem = 2;

    /* === Expand area: join form (hidden) === */
    int jy = PANEL_H_COL;
    CreateWindowExW(0, L"STATIC", L"服务器地址",
        WS_CHILD, 8, jy+5, 60, 14, pw, NULL, hi, NULL);
    HWND se = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        8, jy+20, 334, 21, pw, (HMENU)IDC_SERVER_EDIT, hi, NULL);
    HWND re = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        8, jy+50, 164, 21, pw, (HMENU)IDC_ROOM_EDIT, hi, NULL);
    HWND ne = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        178, jy+50, 164, 21, pw, (HMENU)IDC_NICK_EDIT, hi, NULL);
    CreateWindowExW(0, L"STATIC", L"密码 (可选)",
        WS_CHILD, 8, jy+74, 80, 14, pw, NULL, hi, NULL);
    HWND pe = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL,
        8, jy+89, 334, 21, pw, (HMENU)IDC_PASS_EDIT, hi, NULL);
    HWND okb = CreateWindowExW(0, L"BUTTON", L"确定",
        WS_CHILD | BS_PUSHBUTTON, 8, jy+120, 164, 24, pw, (HMENU)IDC_JOIN_OK, hi, NULL);
    HWND nob = CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD | BS_PUSHBUTTON, 178, jy+120, 164, 24, pw, (HMENU)IDC_JOIN_CANCEL, hi, NULL);

    g_children_join[0] = se;  g_children_join[1] = re;
    g_children_join[2] = ne;  g_children_join[3] = pe;
    g_children_join[4] = okb; g_children_join[5] = nob;
    g_n_join = 6;
    show_kids(g_children_join, g_n_join, 0);

    ShowWindow(p->hwnd, SW_SHOW);
    UpdateWindow(p->hwnd);
    return 1;
}

void panel_destroy(panel_t *p) {
    if (p->hwnd) DestroyWindow(p->hwnd);
    memset(p, 0, sizeof(*p));
}

void panel_set_muted(panel_t *p, int muted) {
    p->muted = muted;
    HWND m = GetDlgItem(p->hwnd, IDC_MODE_MUTED);
    if (m) SetWindowTextW(m, muted ? L"🔇 已静音" : L"🔇");
}

void panel_set_input_mode(panel_t *p, int mode) {
    HWND m = GetDlgItem(p->hwnd, IDC_MODE_MUTED);
    HWND t = GetDlgItem(p->hwnd, IDC_MODE_PTT);
    HWND o = GetDlgItem(p->hwnd, IDC_MODE_OPEN);
    if (!m || !t || !o) return;

    if (mode == INPUT_MODE_MUTED) {
        SetWindowTextW(m, L"🔇 已静音");
        SetWindowTextW(t, L"🎤");
        SetWindowTextW(o, L"📢");
    } else if (mode == INPUT_MODE_PTT) {
        SetWindowTextW(m, L"🔇");
        SetWindowTextW(t, L"🎤 说话");
        SetWindowTextW(o, L"📢");
    } else {
        SetWindowTextW(m, L"🔇");
        SetWindowTextW(t, L"🎤");
        SetWindowTextW(o, L"📢 自由");
    }
    panel_set_muted(p, mode == INPUT_MODE_MUTED);
}

void panel_set_connection(panel_t *p, const char *server, const char *room) {
    if (server) { strncpy(p->server, server, sizeof(p->server)-1); p->server[sizeof(p->server)-1]=0; }
    if (room)   { strncpy(p->room, room, sizeof(p->room)-1);     p->room[sizeof(p->room)-1]=0; }

    int ok = (server && server[0] && room && room[0]);
    HWND st  = GetDlgItem(p->hwnd, IDC_STATUS);
    HWND jb  = GetDlgItem(p->hwnd, IDC_JOIN);
    HWND cb  = GetDlgItem(p->hwnd, IDC_CANCEL);
    HWND mb  = GetDlgItem(p->hwnd, IDC_MEMBER_BTN);
    HWND pb  = GetDlgItem(p->hwnd, IDC_PIN);
    HWND mu  = GetDlgItem(p->hwnd, IDC_MODE_MUTED);
    HWND pt  = GetDlgItem(p->hwnd, IDC_MODE_PTT);
    HWND op  = GetDlgItem(p->hwnd, IDC_MODE_OPEN);
    HWND lv  = GetDlgItem(p->hwnd, IDC_LEAVE);
    HWND sg  = GetDlgItem(p->hwnd, IDC_SPEAKER_TAG);

    if (ok) {
        if (st) SetWindowTextW(st, L"● 已连接");
        ShowWindow(jb, SW_HIDE);
        ShowWindow(cb, SW_HIDE);
        ShowWindow(mb, SW_SHOW);
        ShowWindow(pb, SW_SHOW);
        ShowWindow(mu, SW_SHOW);
        ShowWindow(pt, SW_SHOW);
        ShowWindow(op, SW_SHOW);
        ShowWindow(lv, SW_SHOW);
        ShowWindow(sg, SW_SHOW);
    } else {
        if (st) SetWindowTextW(st, L"○ 未连接");
        ShowWindow(jb, SW_SHOW);
        ShowWindow(cb, SW_HIDE);
        ShowWindow(mb, SW_HIDE);
        ShowWindow(pb, SW_HIDE);
        ShowWindow(mu, SW_HIDE);
        ShowWindow(pt, SW_HIDE);
        ShowWindow(op, SW_HIDE);
        ShowWindow(lv, SW_HIDE);
        ShowWindow(sg, SW_HIDE);
        g_expanded = 0;
        SetWindowPos(p->hwnd, NULL, 0, 0, PANEL_W, PANEL_H_COL, SWP_NOZORDER | SWP_NOMOVE);
    }
}

void panel_set_volume(panel_t *p, int peak_pct) {
    p->peak_pct = peak_pct;
    HWND tag = GetDlgItem(p->hwnd, IDC_SPEAKER_TAG);
    if (!tag || !IsWindowVisible(tag)) return;
    int blocks = (peak_pct * 8) / 100;
    if (blocks > 8) blocks = 8;
    wchar_t bar[16], buf[64];
    for (int i = 0; i < 8; i++) bar[i] = i < blocks ? L'█' : L'░';
    bar[8] = 0;
    _snwprintf(buf, 64, L"🎤 %s %d%%", bar, peak_pct);
    SetWindowTextW(tag, buf);
}

void panel_set_members(panel_t *p, const char *names[], int count) {
    p->member_count = count;
    HWND lb  = GetDlgItem(p->hwnd, IDC_MEMBER_LIST);
    HWND btn = GetDlgItem(p->hwnd, IDC_MEMBER_BTN);
    HWND inf = GetDlgItem(p->hwnd, IDC_EXPAND_INFO);

    if (lb) {
        SendMessageW(lb, LB_RESETCONTENT, 0, 0);
        for (int i = 0; i < count; i++) {
            wchar_t wbuf[128];
            MultiByteToWideChar(CP_UTF8, 0, names[i], -1, wbuf, 128);
            SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)wbuf);
        }
    }
    if (btn) {
        wchar_t cnt[32];
        _snwprintf(cnt, 32, L"👥 %d▾", count);
        SetWindowTextW(btn, cnt);
    }
    if (inf && p->server[0]) {
        wchar_t wbuf[256];
        _snwprintf(wbuf, 256, L"%hs @ %hs", p->room, p->server);
        SetWindowTextW(inf, wbuf);
    }
}
