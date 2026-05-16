#include "panel.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <d2d1.h>
#include <dwrite.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#define PANEL_W         350
#define PANEL_H_COL     40
#define PANEL_H_MEM     160
#define PANEL_H_JOIN    250

#define C_BG           0x1a1a2e
#define C_SURFACE      0x16213e
#define C_TEXT         0xe0e0e0
#define C_DIM          0x888888
#define C_ACCENT       0x46a0e6
#define C_GREEN        0x2ecc71
#define C_RED          0xe74c3c
#define C_ORANGE       0xf39c12
#define C_BLUE         0x4a90d9
#define C_HOVER        0x252540
#define C_PRESS        0x151525
#define C_SEPARATOR    0x2a2a4e

#define BID_JOIN        1
#define BID_LEAVE       2
#define BID_MODE_MUTED  3
#define BID_MODE_PTT    4
#define BID_MODE_OPEN   5
#define BID_CANCEL      6
#define BID_MEMBER      7
#define BID_PIN         8
#define BID_JOIN_OK     9
#define BID_JOIN_CANCEL 10

static ID2D1Factory           *g_factory = nullptr;
static ID2D1HwndRenderTarget  *g_rt = nullptr;
static IDWriteFactory         *g_dw = nullptr;
static IDWriteTextFormat      *g_font = nullptr;
static IDWriteTextFormat      *g_font_sm = nullptr;
static ID2D1SolidColorBrush   *g_br[16] = {0};

static int   g_expanded = 0, g_pinned = 0, g_hover = 0, g_press = 0;
static HWND  g_ed_srv = NULL, g_ed_room = NULL, g_ed_nick = NULL, g_ed_pass = NULL;
static HWND  g_ed_ok = NULL, g_ed_cancel = NULL;
static char  g_join_server[64], g_join_room[32], g_join_nick[32], g_join_pass[64];

struct btn_def { float x, y, w, h; int id; const wchar_t *text; UINT32 bg, fg; };
static btn_def g_btns[16];
static int g_nbtns = 0;

/* === Helpers === */
static D2D1_COLOR_F rgb(UINT32 c, float a) {
    return D2D1::ColorF(((c>>16)&0xFF)/255.0f, ((c>>8)&0xFF)/255.0f, (c&0xFF)/255.0f, a);
}

static ID2D1SolidColorBrush* br(UINT32 color, float alpha) {
    for (auto & b : g_br) {
        if (b) { b->SetColor(rgb(color, alpha)); return b; }
    }
    int i = 0;
    while (i < 15 && g_br[i]) i++;
    if (g_rt) g_rt->CreateSolidColorBrush(rgb(color, alpha), &g_br[i]);
    return g_br[i];
}

static void draw_text(const wchar_t *txt, float x, float y, float w, float h,
                       IDWriteTextFormat *fmt, UINT32 color) {
    if (!txt || !txt[0]) return;
    auto b = br(color, 1.0f);
    if (!b) return;
    D2D1_RECT_F rc = {x, y, x+w, y+h};
    g_rt->DrawText(txt, (UINT32)wcslen(txt), fmt, rc, b,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
}

static void draw_btn(float x, float y, float w, float h, const wchar_t *txt,
                      int bid, UINT32 bg_c, UINT32 fg_c, float r) {
    UINT32 c = (g_press == bid) ? C_PRESS : (g_hover == bid) ? C_HOVER : bg_c;
    D2D1_ROUNDED_RECT rr = {{x, y, x+w, y+h}, r, r};
    g_rt->FillRoundedRectangle(rr, br(c, 1.0f));
    draw_text(txt, x, y, w, h, g_font, fg_c);
}

static void draw_rect(float x, float y, float w, float h, UINT32 color, float r) {
    D2D1_ROUNDED_RECT rr = {{x, y, x+w, y+h}, r, r};
    g_rt->FillRoundedRectangle(rr, br(color, 1.0f));
}

static void draw_circle(float cx, float cy, float r, UINT32 color) {
    D2D1_ELLIPSE e = {{cx, cy}, r, r};
    g_rt->FillEllipse(e, br(color, 1.0f));
}

static void btn(float x, float y, float w, float h, int id, const wchar_t *txt, UINT32 bg, UINT32 fg) {
    if (g_nbtns < 16) {
        g_btns[g_nbtns++] = {x, y, w, h, id, txt, bg, fg};
    }
}

static int hit_test(float px, float py) {
    for (int i = g_nbtns-1; i >= 0; i--) {
        auto &b = g_btns[i];
        if (px >= b.x && px <= b.x+b.w && py >= b.y && py <= b.y+b.h) return b.id;
    }
    return 0;
}

static void setup_btns(panel_t *p, int connected) {
    g_nbtns = 0;
    if (g_expanded == 2) {
        btn(8, 130, 166, 22, BID_JOIN_OK, L"确定", C_BLUE, C_TEXT);
        btn(176, 130, 166, 22, BID_JOIN_CANCEL, L"取消", C_SURFACE, C_DIM);
    } else if (g_expanded == 1) {
        btn(4, 4, 50, 14, BID_MEMBER, L"👥 ▴", C_SURFACE, C_DIM);
        btn(250, 4, 24, 14, BID_PIN, L"📌", g_pinned ? C_ACCENT : C_SURFACE, g_pinned ? C_TEXT : C_DIM);
        btn(4, PANEL_H_MEM-18, 42, 16, BID_MODE_MUTED, p->muted ? L"🔇 静音" : L"🔇", p->muted ? C_RED : C_SURFACE, p->muted ? C_TEXT : C_DIM);
        btn(48, PANEL_H_MEM-18, 74, 16, BID_MODE_PTT, L"🎤 说话", C_GREEN, C_TEXT);
        btn(124, PANEL_H_MEM-18, 42, 16, BID_MODE_OPEN, L"📢", C_SURFACE, C_DIM);
        btn(276, PANEL_H_MEM-18, 68, 16, BID_LEAVE, L"✕ 离开", C_RED, C_TEXT);
    } else if (connected) {
        btn(4, 4, 50, 14, BID_MEMBER, L"👥 ▾", C_SURFACE, C_DIM);
        btn(4, 22, 42, 16, BID_MODE_MUTED, p->muted ? L"🔇 静音" : L"🔇", p->muted ? C_RED : C_SURFACE, p->muted ? C_TEXT : C_DIM);
        btn(48, 22, 74, 16, BID_MODE_PTT, L"🎤 说话", C_GREEN, C_TEXT);
        btn(124, 22, 42, 16, BID_MODE_OPEN, L"📢", C_SURFACE, C_DIM);
        btn(250, 22, 24, 16, BID_PIN, L"📌", g_pinned ? C_ACCENT : C_SURFACE, g_pinned ? C_TEXT : C_DIM);
        btn(276, 22, 68, 16, BID_LEAVE, L"✕ 离开", C_RED, C_TEXT);
    } else {
        btn(276, 4, 68, 18, BID_JOIN, L"+ 加入", C_BLUE, C_TEXT);
    }
}

/* D2D lifecycle */
static int d2d_init(HWND hwnd) {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_factory);
    if (!g_factory) return 0;
    RECT rc; GetClientRect(hwnd, &rc);
    g_factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)),
        &g_rt);
    if (!g_rt) return 0;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw);
    if (!g_dw) return 0;
    g_dw->CreateTextFormat(L"Microsoft YaHei", NULL, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"", &g_font);
    g_dw->CreateTextFormat(L"Microsoft YaHei", NULL, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.0f, L"", &g_font_sm);
    if (g_font) { g_font->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); g_font->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
    if (g_font_sm) { g_font_sm->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); g_font_sm->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); }
    return 1;
}

static void d2d_resize(HWND hwnd) {
    if (g_rt) { RECT rc; GetClientRect(hwnd, &rc); g_rt->Resize(D2D1::SizeU(rc.right, rc.bottom)); }
}

static void d2d_cleanup() {
    for (auto &b : g_br) { if (b) { b->Release(); b = nullptr; } }
    if (g_font_sm) { g_font_sm->Release(); g_font_sm = nullptr; }
    if (g_font) { g_font->Release(); g_font = nullptr; }
    if (g_dw) { g_dw->Release(); g_dw = nullptr; }
    if (g_rt) { g_rt->Release(); g_rt = nullptr; }
    if (g_factory) { g_factory->Release(); g_factory = nullptr; }
}

/* EDIT management */
static void show_edits(panel_t *p, int show) {
    int sw = show ? SW_SHOW : SW_HIDE;
    ShowWindow(g_ed_srv, sw); ShowWindow(g_ed_room, sw);
    ShowWindow(g_ed_nick, sw); ShowWindow(g_ed_pass, sw);
    ShowWindow(g_ed_ok, sw); ShowWindow(g_ed_cancel, sw);
}

static void read_edits() {
    GetWindowTextA(g_ed_srv, g_join_server, sizeof(g_join_server));
    GetWindowTextA(g_ed_room, g_join_room, sizeof(g_join_room));
    GetWindowTextA(g_ed_nick, g_join_nick, sizeof(g_join_nick));
    GetWindowTextA(g_ed_pass, g_join_pass, sizeof(g_join_pass));
}

/* Render */
static void render(panel_t *p, HWND hwnd) {
    if (!g_rt) return;
    g_rt->BeginDraw();
    g_rt->Clear(rgb(C_BG, 1.0f));

    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    int connected = (p->server[0] != 0);
    setup_btns(p, connected);

    if (g_expanded == 2) {
        draw_text(L"Qmini  ○ 加入房间", 6, 6, 200, 14, g_font, C_TEXT);
        draw_text(L"Mlaiou", (float)(W-60), 6, 50, 14, g_font, C_ACCENT);
        draw_rect(4, 24, (float)(W-8), 1, C_SEPARATOR, 0);
        draw_text(L"服务器地址", 10, 32, 80, 14, g_font_sm, C_DIM);
        draw_text(L"房间名 / 昵称", 10, 62, 140, 14, g_font_sm, C_DIM);
        draw_text(L"密码 (可选)", 10, 92, 80, 14, g_font_sm, C_DIM);
    } else if (g_expanded == 1) {
        wchar_t hdr[128];
        _snwprintf(hdr, 128, L"● %hs @ %hs", p->room, p->server);
        draw_text(hdr, 6, 6, 180, 14, g_font, C_TEXT);
        draw_text(L"Mlaiou", (float)(W-60), 6, 50, 14, g_font, C_ACCENT);
        float my = 24.0f;
        draw_rect(4, my, (float)(W-8), (float)(H-my-22), C_SURFACE, 4.0f);
        HWND lb = GetDlgItem(hwnd, IDC_MEMBER_LIST);
        if (lb) {
            int count = (int)SendMessageW(lb, LB_GETCOUNT, 0, 0);
            for (int i = 0; i < count && i < 6; i++) {
                wchar_t buf[128] = {0};
                SendMessageW(lb, LB_GETTEXT, (WPARAM)i, (LPARAM)buf);
                float y = my + 6 + i * 18.0f;
                if (i == 0) draw_rect(8, y-1, (float)(W-16), 18, 0x0a2a0a, 3.0f);
                draw_circle(16, y+8, 3, i == 0 ? C_GREEN : C_DIM);
                draw_text(buf, 24, y, 200, 16, g_font_sm, i == 0 ? C_GREEN : C_TEXT);
            }
        }
    } else {
        wchar_t status[64];
        if (connected) _snwprintf(status, 64, L"● 已连接 · %hs", p->room);
        else wcscpy(status, L"○ 未连接");
        draw_text(status, 6, 6, 150, 14, g_font, connected ? C_GREEN : C_DIM);
        draw_text(L"Mlaiou", 190, 6, 70, 14, g_font, C_ACCENT);
        if (connected && p->peak_pct > 0) {
            wchar_t vm[32];
            int blocks = (p->peak_pct * 6) / 100;
            if (blocks > 6) blocks = 6;
            wchar_t bar[8];
            for (int i = 0; i < 6; i++) bar[i] = i < blocks ? L'█' : L'░';
            bar[6] = 0;
            _snwprintf(vm, 32, L"🎤%s %d%%", bar, p->peak_pct);
            draw_text(vm, 58, 22, 100, 16, g_font_sm, C_GREEN);
        }
    }

    for (int i = 0; i < g_nbtns; i++) {
        auto &b = g_btns[i];
        draw_btn(b.x, b.y, b.w, b.h, b.text, b.id, b.bg, b.fg, 3.0f);
    }

    if (connected && g_expanded == 0)
        draw_rect(4, 20, (float)(W-8), 1, C_SEPARATOR, 0);

    g_rt->EndDraw();
}

/* Window proc */
LRESULT CALLBACK panel_wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    panel_t *p = (panel_t*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE:
        d2d_init(hwnd);
        g_ed_srv = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"192.144.133.168:9088",
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL, 10,46,330,20, hwnd,(HMENU)100,NULL,NULL);
        g_ed_room = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"default",
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL, 10,74,160,20, hwnd,(HMENU)101,NULL,NULL);
        g_ed_nick = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Player",
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL, 176,74,164,20, hwnd,(HMENU)102,NULL,NULL);
        g_ed_pass = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD|WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL, 10,104,330,20, hwnd,(HMENU)103,NULL,NULL);
        g_ed_ok = CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD|BS_PUSHBUTTON, 10,132,160,22, hwnd,(HMENU)BID_JOIN_OK,NULL,NULL);
        g_ed_cancel = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD|BS_PUSHBUTTON, 176,132,164,22, hwnd,(HMENU)BID_JOIN_CANCEL,NULL,NULL);
        show_edits(p, 0);
        return 0;

    case WM_SIZE: d2d_resize(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;

    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); render(p, hwnd); EndPaint(hwnd, &ps); return 0; }

    case WM_LBUTTONDOWN:
        g_press = hit_test((float)LOWORD(l), (float)HIWORD(l));
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP: {
        int hit = hit_test((float)LOWORD(l), (float)HIWORD(l));
        g_press = 0; InvalidateRect(hwnd, NULL, FALSE);
        if (!hit) return 0;
        switch (hit) {
        case BID_JOIN: g_expanded = 2; show_edits(p, 1);
            SetWindowPos(hwnd, NULL,0,0,PANEL_W,PANEL_H_JOIN,SWP_NOZORDER|SWP_NOMOVE);
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        case BID_JOIN_CANCEL: case BID_CANCEL:
            g_expanded = 0; show_edits(p, 0);
            SetWindowPos(hwnd, NULL,0,0,PANEL_W,PANEL_H_COL,SWP_NOZORDER|SWP_NOMOVE);
            InvalidateRect(hwnd, NULL, FALSE);
            if (hit == BID_CANCEL) PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_LEAVE_ROOM, 0);
            return 0;
        case BID_JOIN_OK: read_edits(); g_expanded = 0; show_edits(p, 0);
            SetWindowPos(hwnd, NULL,0,0,PANEL_W,PANEL_H_COL,SWP_NOZORDER|SWP_NOMOVE);
            InvalidateRect(hwnd, NULL, FALSE);
            PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_JOIN_ROOM, 0);
            return 0;
        case BID_LEAVE: PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_LEAVE_ROOM, 0); return 0;
        case BID_MODE_MUTED: PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_MODE_MUTED, 0); return 0;
        case BID_MODE_PTT:   PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_MODE_PTT, 0); return 0;
        case BID_MODE_OPEN:  PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_MODE_OPEN, 0); return 0;
        case BID_PIN: g_pinned = !g_pinned;
            SetWindowPos(hwnd, g_pinned?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,
                         SWP_NOOWNERZORDER|SWP_NOSIZE|SWP_NOMOVE);
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        case BID_MEMBER: g_expanded = (g_expanded==1)?0:1; show_edits(p, 0);
            { int h = (g_expanded==1)?PANEL_H_MEM:PANEL_H_COL;
              SetWindowPos(hwnd, NULL,0,0,PANEL_W,h,SWP_NOZORDER|SWP_NOMOVE); }
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int prev = g_hover;
        g_hover = hit_test((float)LOWORD(l), (float)HIWORD(l));
        if (g_hover != prev) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_COMMAND:
        PostMessageW(hwnd, WM_COMMAND, w, l);
        return 0;
    case WM_CLOSE:
        PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_EXIT, 0);
        return 0;
    case WM_DESTROY:
        d2d_cleanup();
        PostQuitMessage(0);
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, w, l);
        if (hit == HTCLIENT) return HTCAPTION;
        return hit;
    }
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

/* Public API */
int panel_create(panel_t *p, HINSTANCE inst) {
    static int registered = 0;
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc = panel_wndproc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"QminiHUDPanel";
        RegisterClassW(&wc);
        registered = 1;
    }
    p->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"QminiHUDPanel", L"Qmini",
        WS_POPUP | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, PANEL_W, PANEL_H_COL,
        NULL, NULL, inst, p);
    if (!p->hwnd) return 0;
    SetWindowLongPtrW(p->hwnd, GWLP_USERDATA, (LONG_PTR)p);
    p->inst = inst; p->muted = 0; p->peak_pct = 0; p->member_count = 0;
    p->server[0] = 0; p->room[0] = 0;
    p->hfont = NULL; p->hfont_brand = NULL; p->hbrush_mic_bg = NULL; p->hbrush_mic_on = NULL;
    ShowWindow(p->hwnd, SW_SHOW);
    UpdateWindow(p->hwnd);
    return 1;
}

void panel_destroy(panel_t *p) { if (p->hwnd) DestroyWindow(p->hwnd); memset(p, 0, sizeof(*p)); }
void panel_set_muted(panel_t *p, int muted) { p->muted = muted; InvalidateRect(p->hwnd, NULL, FALSE); }
void panel_set_input_mode(panel_t *p, int mode) { p->muted = (mode == INPUT_MODE_MUTED); InvalidateRect(p->hwnd, NULL, FALSE); }

void panel_set_connection(panel_t *p, const char *server, const char *room) {
    if (server) { strncpy(p->server, server, sizeof(p->server)-1); p->server[sizeof(p->server)-1]=0; }
    if (room)   { strncpy(p->room, room, sizeof(p->room)-1); p->room[sizeof(p->room)-1]=0; }
    if (!server || !server[0]) g_expanded = 0;
    InvalidateRect(p->hwnd, NULL, FALSE);
}

void panel_set_volume(panel_t *p, int peak_pct) { p->peak_pct = peak_pct; if (g_expanded==0) InvalidateRect(p->hwnd, NULL, FALSE); }

void panel_set_members(panel_t *p, const char *names[], int count) {
    p->member_count = count;
    HWND lb = GetDlgItem(p->hwnd, IDC_MEMBER_LIST);
    if (!lb) lb = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD, 0,0,1,1, p->hwnd, (HMENU)IDC_MEMBER_LIST, NULL, NULL);
    if (lb) {
        SendMessageW(lb, LB_RESETCONTENT, 0, 0);
        for (int i = 0; i < count; i++) {
            wchar_t wbuf[128];
            MultiByteToWideChar(CP_UTF8, 0, names[i], -1, wbuf, 128);
            SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)wbuf);
        }
    }
    InvalidateRect(p->hwnd, NULL, FALSE);
}

const char* panel_get_join_server() { return g_join_server; }
const char* panel_get_join_room()   { return g_join_room; }
const char* panel_get_join_nick()   { return g_join_nick; }
const char* panel_get_join_pass()   { return g_join_pass; }
