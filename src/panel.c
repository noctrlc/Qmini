#include "panel.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HUD_W  350
#define HUD_H   40

/* Internal child IDs (high range avoids collision) */
#define CID_STATUS     4001
#define CID_MEMBER_BTN 4002
#define CID_SPEAKER    4003
#define CID_JOIN       4004
#define CID_LEAVE      4005
#define CID_CANCEL     4006
#define CID_PIN        4007
#define CID_MUTE       4008
#define CID_PTT        4009
#define CID_OPEN       4010
#define CID_JOIN_OK    4011
#define CID_JOIN_CANCEL 4012
#define CID_SRV_EDIT   4013
#define CID_ROOM_EDIT  4014
#define CID_NICK_EDIT  4015
#define CID_PASS_EDIT  4016

/* Colors */
#define BG_DARK        RGB(10,10,30)
#define BG_SURFACE     RGB(22,33,62)
#define TEXT_WHITE      RGB(224,224,224)
#define TEXT_DIM        RGB(136,136,136)
#define TEXT_BLUE       RGB(70,160,230)
#define GREEN_BTN       RGB(67,181,129)
#define RED_BTN         RGB(231,76,60)
#define BLUE_BTN        RGB(74,144,217)
#define ORANGE_BTN      RGB(243,156,18)

static int       g_connected = 0;
static int       g_expanded  = 0;  /* 0=bar, 1=members, 2=join */
static int       g_pinned    = 0;
static HWND      g_expand_children[10];
static int       g_expand_count = 0;
static HFONT     g_font_bar  = NULL;
static HFONT     g_font_sm   = NULL;
static HBRUSH    g_brush_bg  = NULL;
static HBRUSH    g_brush_surface = NULL;
static char      g_join_server[64];
static char      g_join_room[32];
static char      g_join_nick[32];
static char      g_join_pass[64];

/* Forward */
static void apply_state(HWND hwnd, panel_t *p);

static void hide_expand() {
    for (int i = 0; i < g_expand_count; i++)
        ShowWindow(g_expand_children[i], SW_HIDE);
}

static void show_expand() {
    for (int i = 0; i < g_expand_count; i++)
        ShowWindow(g_expand_children[i], SW_SHOW);
}

/* Read join form values */
static void read_join() {
    GetWindowTextA(GetDlgItem(GetParent(g_expand_children[0]), CID_SRV_EDIT),  g_join_server, 64);
    GetWindowTextA(GetDlgItem(GetParent(g_expand_children[0]), CID_ROOM_EDIT), g_join_room, 32);
    GetWindowTextA(GetDlgItem(GetParent(g_expand_children[0]), CID_NICK_EDIT), g_join_nick, 32);
    GetWindowTextA(GetDlgItem(GetParent(g_expand_children[0]), CID_PASS_EDIT), g_join_pass, 64);
}

/* Window procedure */
LRESULT CALLBACK panel_wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    panel_t *p = (panel_t*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT*)l;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        p = (panel_t*)cs->lpCreateParams;

        g_brush_bg = CreateSolidBrush(BG_DARK);
        g_brush_surface = CreateSolidBrush(BG_SURFACE);
        g_font_bar = CreateFontW(14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
        g_font_sm  = CreateFontW(12,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");

        HINSTANCE hi = cs->hInstance;

        /* Row 1: status bar line */
        CreateWindowExW(0, L"STATIC", L"Qmini", WS_CHILD|WS_VISIBLE, 6,4,32,16, hwnd, NULL, hi, NULL);
        CreateWindowExW(0, L"STATIC", L"○ 未连接", WS_CHILD|WS_VISIBLE, 40,4,100,16, hwnd, (HMENU)CID_STATUS, hi, NULL);
        CreateWindowExW(0, L"STATIC", L"Mlaiou", WS_CHILD|WS_VISIBLE|SS_RIGHT, 260,4,55,16, hwnd, NULL, hi, NULL);

        /* Member count button (hidden start) */
        HWND mb = CreateWindowExW(0, L"BUTTON", L"\U0001F465 0▾", WS_CHILD|BS_PUSHBUTTON, 140,2,52,18, hwnd, (HMENU)CID_MEMBER_BTN, hi, NULL);
        ShowWindow(mb, SW_HIDE);

        /* Speaker tag */
        HWND sp = CreateWindowExW(0, L"STATIC", L"", WS_CHILD|SS_CENTER, 56,22,100,16, hwnd, (HMENU)CID_SPEAKER, hi, NULL);
        ShowWindow(sp, SW_HIDE);

        /* Join button */
        CreateWindowExW(0, L"BUTTON", L"+ 加入", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 278,2,62,20, hwnd, (HMENU)CID_JOIN, hi, NULL);

        /* Cancel button (hidden start) */
        HWND cb = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD|BS_PUSHBUTTON, 290,2,54,20, hwnd, (HMENU)CID_CANCEL, hi, NULL);
        ShowWindow(cb, SW_HIDE);

        /* Pin button (hidden start) */
        HWND pb = CreateWindowExW(0, L"BUTTON", L"\U0001F4CC", WS_CHILD|BS_PUSHBUTTON, 238,2,22,20, hwnd, (HMENU)CID_PIN, hi, NULL);
        ShowWindow(pb, SW_HIDE);

        /* Row 2: mode buttons + leave (hidden start) */
        HWND mu = CreateWindowExW(0, L"BUTTON", L"\U0001F507", WS_CHILD|BS_PUSHBUTTON, 4,22,38,16, hwnd, (HMENU)CID_MUTE, hi, NULL);
        HWND pt = CreateWindowExW(0, L"BUTTON", L"\U0001F3A4 说话", WS_CHILD|BS_PUSHBUTTON, 44,22,74,16, hwnd, (HMENU)CID_PTT, hi, NULL);
        HWND op = CreateWindowExW(0, L"BUTTON", L"\U0001F4E2", WS_CHILD|BS_PUSHBUTTON, 120,22,38,16, hwnd, (HMENU)CID_OPEN, hi, NULL);
        HWND lv = CreateWindowExW(0, L"BUTTON", L"离开", WS_CHILD|BS_PUSHBUTTON, 278,22,62,16, hwnd, (HMENU)CID_LEAVE, hi, NULL);
        ShowWindow(mu, SW_HIDE); ShowWindow(pt, SW_HIDE); ShowWindow(op, SW_HIDE); ShowWindow(lv, SW_HIDE);

        /* === EXPAND AREA (hidden initially) === */
        int ey = 42;
        /* -- Members -- */
        HWND ml = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD|WS_VSCROLL|LBS_NOINTEGRALHEIGHT|LBS_NOTIFY,
            6, ey+2, 338, 110, hwnd, (HMENU)IDC_MEMBER_LIST, hi, NULL);

        /* -- Join form -- */
        CreateWindowExW(0, L"STATIC", L"服务器地址", WS_CHILD, 8,ey+4,80,14, hwnd,NULL,hi,NULL);
        HWND se = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"192.144.133.168:9088",
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL, 8,ey+18,334,21, hwnd,(HMENU)CID_SRV_EDIT,hi,NULL);
        HWND re = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"default",
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL, 8,ey+48,164,21, hwnd,(HMENU)CID_ROOM_EDIT,hi,NULL);
        HWND ne = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Player",
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL, 178,ey+48,164,21, hwnd,(HMENU)CID_NICK_EDIT,hi,NULL);
        CreateWindowExW(0, L"STATIC", L"密码 (留空不加密)", WS_CHILD, 8,ey+74,120,14, hwnd,NULL,hi,NULL);
        HWND pe = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL, 8,ey+89,334,21, hwnd,(HMENU)CID_PASS_EDIT,hi,NULL);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD|BS_PUSHBUTTON, 8,ey+118,164,24, hwnd,(HMENU)CID_JOIN_OK,hi,NULL);
        HWND no = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD|BS_PUSHBUTTON, 178,ey+118,164,24, hwnd,(HMENU)CID_JOIN_CANCEL,hi,NULL);

        /* Store all expand children for show/hide */
        g_expand_children[0]=ml; g_expand_children[1]=se; g_expand_children[2]=re;
        g_expand_children[3]=ne; g_expand_children[4]=pe; g_expand_children[5]=ok; g_expand_children[6]=no;
        g_expand_count = 7;
        hide_expand();

        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)w;
        SetBkMode(hdc, TRANSPARENT);
        if (msg == WM_CTLCOLORSTATIC) {
            UINT id = GetDlgCtrlID((HWND)l);
            if (id == CID_STATUS && g_connected)
                SetTextColor(hdc, GREEN_BTN);
            else if (id == CID_SPEAKER)
                SetTextColor(hdc, GREEN_BTN);
            else if (id == 0 && GetWindowTextLengthW((HWND)l) > 0) {
                /* Mlaiou label (no ID) */
            }
            SetTextColor(hdc, g_connected && id == CID_STATUS ? GREEN_BTN : TEXT_DIM);
            return (LRESULT)g_brush_bg;
        }
        return (LRESULT)g_brush_bg;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)w, BG_SURFACE);
        SetTextColor((HDC)w, TEXT_WHITE);
        return (LRESULT)g_brush_surface;

    case WM_ERASEBKGND:
        { RECT rc; GetClientRect(hwnd, &rc); FillRect((HDC)w, &rc, g_brush_bg); }
        return 1;

    case WM_COMMAND: {
        WORD id = LOWORD(w);
        switch (id) {
        case CID_JOIN:
            g_expanded = 2;
            hide_expand();
            ShowWindow(g_expand_children[1], SW_SHOW); /* srv */
            ShowWindow(g_expand_children[2], SW_SHOW); /* room */
            ShowWindow(g_expand_children[3], SW_SHOW); /* nick */
            ShowWindow(g_expand_children[4], SW_SHOW); /* pass */
            ShowWindow(g_expand_children[5], SW_SHOW); /* ok */
            ShowWindow(g_expand_children[6], SW_SHOW); /* cancel */
            SetWindowPos(hwnd,NULL,0,0,HUD_W,200,SWP_NOZORDER|SWP_NOMOVE);
            return 0;
        case CID_JOIN_CANCEL:
            g_expanded = 0; hide_expand();
            SetWindowPos(hwnd,NULL,0,0,HUD_W,HUD_H,SWP_NOZORDER|SWP_NOMOVE);
            return 0;
        case CID_JOIN_OK:
            read_join();
            g_expanded = 0; hide_expand();
            SetWindowPos(hwnd,NULL,0,0,HUD_W,HUD_H,SWP_NOZORDER|SWP_NOMOVE);
            PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_JOIN_ROOM, 0);
            return 0;
        case CID_LEAVE:
            PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_LEAVE_ROOM, 0);
            return 0;
        case CID_CANCEL:
            PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_LEAVE_ROOM, 0);
            return 0;
        case CID_MUTE:    PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_MODE_MUTED, 0); return 0;
        case CID_PTT:     PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_MODE_PTT, 0);   return 0;
        case CID_OPEN:    PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_MODE_OPEN, 0);  return 0;
        case CID_PIN:
            g_pinned = !g_pinned;
            SetWindowPos(hwnd, g_pinned?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOOWNERZORDER|SWP_NOSIZE|SWP_NOMOVE);
            return 0;
        case CID_MEMBER_BTN:
            if (g_expanded == 1) { g_expanded = 0; hide_expand(); SetWindowPos(hwnd,NULL,0,0,HUD_W,HUD_H,SWP_NOZORDER|SWP_NOMOVE); }
            else { g_expanded = 1; hide_expand(); ShowWindow(g_expand_children[0], SW_SHOW); SetWindowPos(hwnd,NULL,0,0,HUD_W,170,SWP_NOZORDER|SWP_NOMOVE); }
            return 0;
        }
        /* Forward unknown commands to main loop */
        PostMessageW(hwnd, WM_COMMAND, w, l);
        return 0;
    }

    case WM_CLOSE:
        PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_EXIT, 0);
        return 0;
    case WM_DESTROY:
        if (g_font_bar) DeleteObject(g_font_bar);
        if (g_font_sm) DeleteObject(g_font_sm);
        if (g_brush_bg) DeleteObject(g_brush_bg);
        if (g_brush_surface) DeleteObject(g_brush_surface);
        g_font_bar = NULL; g_font_sm = NULL; g_brush_bg = NULL; g_brush_surface = NULL;
        PostQuitMessage(0);
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, w, l);
        return (hit == HTCLIENT) ? HTCAPTION : hit;
    }
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

/* === Public API === */
int panel_create(panel_t *p, HINSTANCE inst) {
    static int registered = 0;
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = panel_wndproc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;  /* handled in WM_ERASEBKGND */
        wc.lpszClassName = L"QminiHUD";
        RegisterClassW(&wc);
        registered = 1;
    }
    p->hwnd = CreateWindowExW(0, L"QminiHUD", L"Qmini HUD",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, HUD_W, HUD_H,
        NULL, NULL, inst, p);
    if (!p->hwnd) return 0;

    p->inst = inst;
    p->muted = 0;
    p->peak_pct = 0;
    p->member_count = 0;
    memset(p->server, 0, sizeof(p->server));
    memset(p->room, 0, sizeof(p->room));

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
    HWND h = GetDlgItem(p->hwnd, CID_MUTE);
    if (h) SetWindowTextW(h, muted ? L"\U0001F507 静音" : L"\U0001F507");
}

void panel_set_input_mode(panel_t *p, int mode) {
    p->muted = (mode == INPUT_MODE_MUTED);
    HWND mu = GetDlgItem(p->hwnd, CID_MUTE);
    HWND pt = GetDlgItem(p->hwnd, CID_PTT);
    HWND op = GetDlgItem(p->hwnd, CID_OPEN);
    if (mode == INPUT_MODE_MUTED) {
        if (mu) SetWindowTextW(mu, L"\U0001F507 静音");
        if (pt) SetWindowTextW(pt, L"\U0001F3A4");
        if (op) SetWindowTextW(op, L"\U0001F4E2");
    } else if (mode == INPUT_MODE_PTT) {
        if (mu) SetWindowTextW(mu, L"\U0001F507");
        if (pt) SetWindowTextW(pt, L"\U0001F3A4 说话");
        if (op) SetWindowTextW(op, L"\U0001F4E2");
    } else {
        if (mu) SetWindowTextW(mu, L"\U0001F507");
        if (pt) SetWindowTextW(pt, L"\U0001F3A4");
        if (op) SetWindowTextW(op, L"\U0001F4E2 自由");
    }
}

void panel_set_connection(panel_t *p, const char *server, const char *room) {
    if (server) { strncpy(p->server, server, sizeof(p->server)-1); p->server[sizeof(p->server)-1]=0; }
    if (room)   { strncpy(p->room, room, sizeof(p->room)-1); p->room[sizeof(p->room)-1]=0; }
    g_connected = (server && server[0] && room && room[0]);
    HWND st  = GetDlgItem(p->hwnd, CID_STATUS);
    HWND jb  = GetDlgItem(p->hwnd, CID_JOIN);
    HWND cb  = GetDlgItem(p->hwnd, CID_CANCEL);
    HWND mb  = GetDlgItem(p->hwnd, CID_MEMBER_BTN);
    HWND pb  = GetDlgItem(p->hwnd, CID_PIN);
    HWND mu  = GetDlgItem(p->hwnd, CID_MUTE);
    HWND pt  = GetDlgItem(p->hwnd, CID_PTT);
    HWND op  = GetDlgItem(p->hwnd, CID_OPEN);
    HWND lv  = GetDlgItem(p->hwnd, CID_LEAVE);
    HWND sp  = GetDlgItem(p->hwnd, CID_SPEAKER);

    if (g_connected) {
        if (st) SetWindowTextW(st, L"● 已连接");
        ShowWindow(jb, SW_HIDE);
        ShowWindow(cb, SW_HIDE);
        ShowWindow(mb, SW_SHOW);
        ShowWindow(pb, SW_SHOW);
        ShowWindow(mu, SW_SHOW);
        ShowWindow(pt, SW_SHOW);
        ShowWindow(op, SW_SHOW);
        ShowWindow(lv, SW_SHOW);
        ShowWindow(sp, SW_SHOW);
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
        ShowWindow(sp, SW_HIDE);
        g_expanded = 0;
        hide_expand();
        SetWindowPos(p->hwnd, NULL, 0, 0, HUD_W, HUD_H, SWP_NOZORDER | SWP_NOMOVE);
    }
}

void panel_set_volume(panel_t *p, int peak_pct) {
    p->peak_pct = peak_pct;
    HWND sp = GetDlgItem(p->hwnd, CID_SPEAKER);
    if (!sp || !IsWindowVisible(sp)) return;
    int blocks = (peak_pct * 8) / 100;
    if (blocks > 8) blocks = 8;
    wchar_t bar[16], buf[64];
    for (int i = 0; i < 8; i++) bar[i] = i < blocks ? L'█' : L'░';
    bar[8] = 0;
    _snwprintf(buf, 64, L"\U0001F3A4 %s %d%%", bar, peak_pct);
    SetWindowTextW(sp, buf);
}

void panel_set_members(panel_t *p, const char *names[], int count) {
    p->member_count = count;
    HWND lb = GetDlgItem(p->hwnd, IDC_MEMBER_LIST);
    HWND mb = GetDlgItem(p->hwnd, CID_MEMBER_BTN);
    if (lb) {
        SendMessageW(lb, LB_RESETCONTENT, 0, 0);
        for (int i = 0; i < count; i++) {
            wchar_t wbuf[128];
            MultiByteToWideChar(CP_UTF8, 0, names[i], -1, wbuf, 128);
            SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)wbuf);
        }
    }
    if (mb) {
        wchar_t cnt[32];
        _snwprintf(cnt, 32, L"\U0001F465 %d▾", count);
        SetWindowTextW(mb, cnt);
    }
}

const char* panel_get_join_server() { return g_join_server; }
const char* panel_get_join_room()   { return g_join_room; }
const char* panel_get_join_nick()   { return g_join_nick; }
const char* panel_get_join_pass()   { return g_join_pass; }
