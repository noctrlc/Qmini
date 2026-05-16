#include "panel.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_W        350
#define PANEL_H_COL    40
#define PANEL_H_MEM    160
#define PANEL_H_JOIN   250
#define BG_COLOR       RGB(10, 10, 30)
#define TEXT_COLOR      RGB(224, 224, 224)
#define DIM_COLOR       RGB(136, 136, 136)
#define ACCENT_COLOR    RGB(70, 160, 230)
#define GREEN_COLOR     RGB(67, 181, 129)
#define RED_COLOR       RGB(231, 76, 60)
#define ORANGE_COLOR    RGB(243, 156, 18)
#define BLUE_COLOR      RGB(74, 144, 217)

/* Internal control IDs (3000+ to avoid collision with command IDs) */
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
#define IDC_SETTINGS    3011
#define IDC_EXPAND_AREA 3012
#define IDC_SERVER_EDIT 3013
#define IDC_ROOM_EDIT   3014
#define IDC_NICK_EDIT   3015
#define IDC_PASS_EDIT   3016
#define IDC_JOIN_OK     3017
#define IDC_JOIN_CANCEL 3018
#define IDC_EXPAND_INFO 3020

static int  g_expanded = 0;  /* 0=collapsed, 1=members, 2=join */
static int  g_pinned = 0;
static HWND g_expand_area = NULL;
static HWND g_children_join[8];
static HWND g_children_mem[4];
static int  g_n_join = 0, g_n_mem = 0;

static void show_children(HWND *hwnds, int n, int show) {
    int cmd = show ? SW_SHOW : SW_HIDE;
    for (int i = 0; i < n; i++) ShowWindow(hwnds[i], cmd);
}

static void set_expand(panel_t *p, int mode) {
    g_expanded = mode;
    int collapsed_h = PANEL_H_COL;
    int expanded_h = (mode == 1) ? PANEL_H_MEM : PANEL_H_JOIN;
    int h = (mode > 0) ? expanded_h : collapsed_h;

    show_children(g_children_mem, g_n_mem, mode == 1);
    show_children(g_children_join, g_n_join, mode == 2);

    SetWindowPos(p->hwnd, NULL, 0, 0, PANEL_W, h, SWP_NOZORDER | SWP_NOMOVE);
    /* Update expand button text */
    if (mode == 1)
        SetWindowTextW(GetDlgItem(p->hwnd, IDC_MEMBER_BTN), L"\U0001F465 3▴");
    else
        SetWindowTextW(GetDlgItem(p->hwnd, IDC_MEMBER_BTN), L"\U0001F465 3▾");
}

LRESULT CALLBACK panel_wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    panel_t *p = (panel_t*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(w) == IDC_CANCEL) {
            PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_LEAVE_ROOM, 0);
            return 0;
        }
        /* Forward member-btn toggle to ourselves, rest to main */
        if (LOWORD(w) == IDC_MEMBER_BTN && p && p->member_count > 0) {
            set_expand(p, g_expanded == 1 ? 0 : 1);
            return 0;
        }
        if (LOWORD(w) == IDC_PIN) {
            g_pinned = !g_pinned;
            SetWindowPos(hwnd, g_pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOMOVE);
            SetWindowTextW(GetDlgItem(hwnd, IDC_PIN), g_pinned ? L"\U0001F4CC" : L"\U0001F4CC");
            return 0;
        }
        if (LOWORD(w) == IDC_JOIN && p) {
            set_expand(p, g_expanded == 2 ? 0 : 2);
            return 0;
        }
        if (LOWORD(w) == IDC_JOIN_CANCEL && p) {
            set_expand(p, 0);
            return 0;
        }
        /* Forward join OK / leave / mode / exit to main message queue */
        if (LOWORD(w) == IDC_JOIN_OK) {
            /* Read edit fields, send WM_COMMAND JOIN_ROOM to main */
            char server[64] = {0}, room[32] = {0}, nick[32] = {0}, pass[64] = {0};
            GetWindowTextA(GetDlgItem(hwnd, IDC_SERVER_EDIT), server, sizeof(server));
            GetWindowTextA(GetDlgItem(hwnd, IDC_ROOM_EDIT), room, sizeof(room));
            GetWindowTextA(GetDlgItem(hwnd, IDC_NICK_EDIT), nick, sizeof(nick));
            GetWindowTextA(GetDlgItem(hwnd, IDC_PASS_EDIT), pass, sizeof(pass));
            set_expand(p, 0);
            /* Post join command to main loop (main.c handles dialog now,
               but we need to wire these values). For now post basic join. */
            PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_JOIN_ROOM, 0);
            return 0;
        }
        /* Map internal IDs to panel_cmd_t for main.c */
        {
            WORD id = LOWORD(w);
            int cmd = 0;
            switch (id) {
            case IDC_LEAVE:    cmd = PANEL_CMD_LEAVE_ROOM; break;
            case IDC_MODE_MUTED: cmd = PANEL_CMD_MODE_MUTED; break;
            case IDC_MODE_PTT:   cmd = PANEL_CMD_MODE_PTT; break;
            case IDC_MODE_OPEN:  cmd = PANEL_CMD_MODE_OPEN; break;
            }
            if (cmd) { PostMessageW(hwnd, WM_COMMAND, cmd, 0); return 0; }
        }
        /* Forward unhandled commands to main */
        PostMessageW(hwnd, WM_COMMAND, w, l);
        return 0;

    case WM_CLOSE:
        PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_EXIT, 0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)w;
        SetBkColor(hdc, BG_COLOR);
        SetTextColor(hdc, TEXT_COLOR);
        return (LRESULT)GetStockObject(DC_BRUSH);
    }
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, w, l);
        if (hit == HTCLIENT) return HTCAPTION;  /* drag anywhere */
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
        wc.hbrBackground = CreateSolidBrush(BG_COLOR);
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
    int y = 4;

    /* Line 1 (top bar) */
    CreateWindowExW(0, L"STATIC", L"Qmini",
        WS_CHILD | WS_VISIBLE, 4, y, 32, 14, pw, (HMENU)IDC_BRAND, hi, NULL);

    CreateWindowExW(0, L"STATIC", L"○ 未连接",
        WS_CHILD | WS_VISIBLE, 38, y, 80, 14, pw, (HMENU)IDC_STATUS, hi, NULL);

    /* Mlaiou brand */
    CreateWindowExW(0, L"STATIC", L"Mlaiou",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        200, y, 60, 14, pw, NULL, hi, NULL);

    /* Member expand button (hidden initially) */
    HWND memb_btn = CreateWindowExW(0, L"BUTTON", L"\U0001F465 0▾",
        WS_CHILD, 120, y, 50, 14, pw, (HMENU)IDC_MEMBER_BTN, hi, NULL);
    ShowWindow(memb_btn, SW_HIDE);

    /* Speaker tag (hidden initially) */
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD, 172, y, 70, 14, pw, (HMENU)IDC_SPEAKER_TAG, hi, NULL);

    /* Join button */
    CreateWindowExW(0, L"BUTTON", L"+ 加入",
        WS_CHILD | WS_VISIBLE, 280, 2, 60, 18, pw, (HMENU)IDC_JOIN, hi, NULL);

    /* Cancel button (hidden initially) */
    HWND cancel_btn = CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD, 280, 3, 50, 16, pw, (HMENU)IDC_CANCEL, hi, NULL);
    ShowWindow(cancel_btn, SW_HIDE);

    /* Pin button (hidden initially) */
    HWND pin_btn = CreateWindowExW(0, L"BUTTON", L"\U0001F4CC",
        WS_CHILD, 240, 3, 18, 16, pw, (HMENU)IDC_PIN, hi, NULL);
    ShowWindow(pin_btn, SW_HIDE);

    /* Settings button */
    CreateWindowExW(0, L"BUTTON", L"⚙",
        WS_CHILD | WS_VISIBLE, 310, 2, 30, 18, pw, (HMENU)IDC_SETTINGS, hi, NULL);

    /* Line 2: mode buttons + leave (hidden initially) */
    int y2 = 22;
    HWND mute_btn = CreateWindowExW(0, L"BUTTON", L"\U0001F507",
        WS_CHILD, 4, y2, 36, 15, pw, (HMENU)IDC_MODE_MUTED, hi, NULL);
    HWND ptt_btn = CreateWindowExW(0, L"BUTTON", L"\U0001F3A4 PTT",
        WS_CHILD, 42, y2, 55, 15, pw, (HMENU)IDC_MODE_PTT, hi, NULL);
    HWND open_btn = CreateWindowExW(0, L"BUTTON", L"\U0001F4E2",
        WS_CHILD, 99, y2, 36, 15, pw, (HMENU)IDC_MODE_OPEN, hi, NULL);
    HWND leave_btn = CreateWindowExW(0, L"BUTTON", L"离开",
        WS_CHILD, 292, y2, 50, 15, pw, (HMENU)IDC_LEAVE, hi, NULL);
    ShowWindow(mute_btn, SW_HIDE);
    ShowWindow(ptt_btn, SW_HIDE);
    ShowWindow(open_btn, SW_HIDE);
    ShowWindow(leave_btn, SW_HIDE);

    /* ---- Expand area (members) ---- */
    int ey = PANEL_H_COL;
    HWND mem_info = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD, 8, ey + 2, 334, 12, pw, (HMENU)IDC_EXPAND_INFO, hi, NULL);
    HWND mem_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
        6, ey + 16, 338, 88, pw, (HMENU)IDC_MEMBER_LIST, hi, NULL);
    ShowWindow(mem_info, SW_HIDE);
    ShowWindow(mem_list, SW_HIDE);
    g_children_mem[0] = mem_info;
    g_children_mem[1] = mem_list;
    g_n_mem = 2;

    /* ---- Expand area (join form) ---- */
    int jy = PANEL_H_COL;
    HWND srv_lbl = CreateWindowExW(0, L"STATIC", L"服务器地址",
        WS_CHILD, 8, jy + 4, 60, 12, pw, NULL, hi, NULL);
    HWND srv_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_BORDER, 8, jy + 17, 334, 20, pw, (HMENU)IDC_SERVER_EDIT, hi, NULL);
    HWND room_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_BORDER, 8, jy + 48, 162, 20, pw, (HMENU)IDC_ROOM_EDIT, hi, NULL);
    HWND nick_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_BORDER, 178, jy + 48, 164, 20, pw, (HMENU)IDC_NICK_EDIT, hi, NULL);
    HWND pass_lbl = CreateWindowExW(0, L"STATIC", L"密码 (可选)",
        WS_CHILD, 8, jy + 70, 80, 12, pw, NULL, hi, NULL);
    HWND pass_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_PASSWORD, 8, jy + 83, 334, 20, pw, (HMENU)IDC_PASS_EDIT, hi, NULL);
    HWND ok_btn = CreateWindowExW(0, L"BUTTON", L"确定",
        WS_CHILD, 8, jy + 114, 162, 22, pw, (HMENU)IDC_JOIN_OK, hi, NULL);
    HWND cancel_join = CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD, 178, jy + 114, 164, 22, pw, (HMENU)IDC_JOIN_CANCEL, hi, NULL);

    g_children_join[0] = srv_lbl;  g_children_join[1] = srv_edit;
    g_children_join[2] = room_edit; g_children_join[3] = nick_edit;
    g_children_join[4] = pass_lbl;  g_children_join[5] = pass_edit;
    g_children_join[6] = ok_btn;    g_children_join[7] = cancel_join;
    g_n_join = 8;
    show_children(g_children_join, g_n_join, 0);

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
    HWND mute = GetDlgItem(p->hwnd, IDC_MODE_MUTED);
    HWND ptt  = GetDlgItem(p->hwnd, IDC_MODE_PTT);
    HWND open = GetDlgItem(p->hwnd, IDC_MODE_OPEN);
    if (muted) {
        SetWindowTextW(mute, L"\U0001F507 已静音");
        SetWindowTextW(ptt,  L"\U0001F3A4");
        SetWindowTextW(open, L"\U0001F4E2");
    }
}

void panel_set_input_mode(panel_t *p, int mode) {
    HWND mute = GetDlgItem(p->hwnd, IDC_MODE_MUTED);
    HWND ptt  = GetDlgItem(p->hwnd, IDC_MODE_PTT);
    HWND open = GetDlgItem(p->hwnd, IDC_MODE_OPEN);
    int ids[] = { IDC_MODE_MUTED, IDC_MODE_PTT, IDC_MODE_OPEN };
    CheckRadioButton(p->hwnd, IDC_MODE_MUTED, IDC_MODE_OPEN, ids[mode]);

    if (mode == INPUT_MODE_MUTED) {
        SetWindowTextW(mute, L"\U0001F507 已静音");
        SetWindowTextW(ptt,  L"\U0001F3A4");
        SetWindowTextW(open, L"\U0001F4E2");
    } else if (mode == INPUT_MODE_PTT) {
        SetWindowTextW(mute, L"\U0001F507");
        SetWindowTextW(ptt,  L"\U0001F3A4 PTT");
        SetWindowTextW(open, L"\U0001F4E2");
    } else {
        SetWindowTextW(mute, L"\U0001F507");
        SetWindowTextW(ptt,  L"\U0001F3A4");
        SetWindowTextW(open, L"\U0001F4E2 自由");
    }
    panel_set_muted(p, mode == INPUT_MODE_MUTED);
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

    int connected = (server && server[0] && room && room[0]);
    HWND status   = GetDlgItem(p->hwnd, IDC_STATUS);
    HWND join_btn = GetDlgItem(p->hwnd, IDC_JOIN);
    HWND memb_btn = GetDlgItem(p->hwnd, IDC_MEMBER_BTN);
    HWND cancel   = GetDlgItem(p->hwnd, IDC_CANCEL);
    HWND pin      = GetDlgItem(p->hwnd, IDC_PIN);
    HWND settings = GetDlgItem(p->hwnd, IDC_SETTINGS);
    HWND mute     = GetDlgItem(p->hwnd, IDC_MODE_MUTED);
    HWND ptt      = GetDlgItem(p->hwnd, IDC_MODE_PTT);
    HWND open_m   = GetDlgItem(p->hwnd, IDC_MODE_OPEN);
    HWND leave    = GetDlgItem(p->hwnd, IDC_LEAVE);
    HWND srv_tag  = GetDlgItem(p->hwnd, IDC_SPEAKER_TAG);

    if (connected) {
        SetWindowTextW(status, L"● 已连接");
        ShowWindow(join_btn, SW_HIDE);
        ShowWindow(cancel, SW_HIDE);
        ShowWindow(pin, SW_SHOW);
        ShowWindow(memb_btn, SW_SHOW);
        ShowWindow(mute, SW_SHOW);
        ShowWindow(ptt, SW_SHOW);
        ShowWindow(open_m, SW_SHOW);
        ShowWindow(leave, SW_SHOW);
        ShowWindow(settings, SW_SHOW);
    } else {
        SetWindowTextW(status, L"○ 未连接");
        ShowWindow(join_btn, SW_SHOW);
        ShowWindow(cancel, SW_HIDE);
        ShowWindow(pin, SW_HIDE);
        ShowWindow(memb_btn, SW_HIDE);
        ShowWindow(mute, SW_HIDE);
        ShowWindow(ptt, SW_HIDE);
        ShowWindow(open_m, SW_HIDE);
        ShowWindow(leave, SW_HIDE);
        ShowWindow(settings, SW_SHOW);
        ShowWindow(srv_tag, SW_HIDE);
    }
}

void panel_set_volume(panel_t *p, int peak_pct) {
    p->peak_pct = peak_pct;
    HWND tag = GetDlgItem(p->hwnd, IDC_SPEAKER_TAG);
    if (!tag || !IsWindowVisible(tag)) return;
    wchar_t buf[64];
    int blocks = (peak_pct * 8) / 100;
    if (blocks > 8) blocks = 8;
    wchar_t bar[16];
    for (int i = 0; i < 8; i++) bar[i] = i < blocks ? L'█' : L'░';
    bar[8] = 0;
    _snwprintf(buf, 64, L"\U0001F3A4 %s %d%%", bar, peak_pct);
    SetWindowTextW(tag, buf);
}

void panel_set_members(panel_t *p, const char *names[], int count) {
    p->member_count = count;
    HWND lb = GetDlgItem(p->hwnd, IDC_MEMBER_LIST);
    HWND btn = GetDlgItem(p->hwnd, IDC_MEMBER_BTN);
    HWND info = GetDlgItem(p->hwnd, IDC_EXPAND_INFO);

    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < count; i++) {
        wchar_t wbuf[128];
        MultiByteToWideChar(CP_UTF8, 0, names[i], -1, wbuf, 128);
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)wbuf);
    }

    wchar_t cnt[32];
    _snwprintf(cnt, 32, L"\U0001F465 %d▾", count);
    SetWindowTextW(btn, cnt);

    if (p->server[0] && p->room[0]) {
        wchar_t wbuf[256];
        _snwprintf(wbuf, 256, L"%hs · %hs · 192.144.133.168:9088", p->room, p->server);
        SetWindowTextW(info, wbuf);
    }
}
