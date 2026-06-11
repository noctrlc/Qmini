#include "panel.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Layout constants --- */
#define PANEL_W        340
#define PANEL_H_COL    56
#define PANEL_H_MEM    190
#define PANEL_H_JOIN   232
#define PAD            8      /* general padding */
#define BTN_H          20     /* button height */
#define ROW1_Y         6      /* status bar y */
#define ROW2_Y         30     /* action row y */

/* --- Dark theme colors --- */
#define CLR_BG         RGB(30,  30,  46)   /* #1E1E2E main background */
#define CLR_PANEL      RGB(45,  45,  63)   /* #2D2D3F input/list bg */
#define CLR_ACCENT     RGB(91, 141, 239)   /* #5B8DEF highlight blue */
#define CLR_OK         RGB(74, 222, 128)   /* #4ADE80 connected green */
#define CLR_ERR        RGB(248, 113, 113)  /* #F87171 error red */
#define CLR_TEXT       RGB(228, 228, 231)  /* #E4E4E7 light text */
#define CLR_TEXT_DIM   RGB(161, 161, 170)  /* #A1A1AA dim text */
#define CLR_BTN_FACE   RGB(61,  61,  82)   /* #3D3D52 button face */
#define CLR_BTN_HOT    RGB(75,  75, 100)   /* #4B4B64 button hover */
#define CLR_BTN_PRESS  RGB(50,  50,  68)   /* #323244 button pressed */
#define CLR_INPUT_BG   RGB(26,  26,  46)   /* #1A1A2E edit box bg */
#define CLR_BORDER     RGB(60,  60,  80)   /* border lines */
#define CLR_BRAND      RGB(91, 141, 239)   /* brand text color */

/* --- Internal control IDs (3000+ to avoid collision with panel_cmd_t 1-9) --- */
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
#define IDC_CLOSE       3019

static int  g_expanded = 0;  /* 0=collapsed, 1=members, 2=join */
static int  g_pinned = 0;
static HWND g_children_join[10];
static HWND g_children_mem[4];
static int  g_n_join = 0, g_n_mem = 0;

/* --- Helper: create dark-theme GDI resources --- */
static void create_theme_resources(panel_t *p) {
    p->hfont       = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                 DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    p->hfont_brand = CreateFontW(-13, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                 DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    p->hbrush_bg      = CreateSolidBrush(CLR_BG);
    p->hbrush_panel   = CreateSolidBrush(CLR_PANEL);
    p->hbrush_mic_bg  = CreateSolidBrush(RGB(40, 40, 55));
    p->hbrush_mic_on  = CreateSolidBrush(CLR_OK);
    p->hpen_border    = CreatePen(PS_SOLID, 1, CLR_BORDER);
    p->hbrush_btn     = CreateSolidBrush(CLR_BTN_FACE);
    p->hbrush_btn_hot = CreateSolidBrush(CLR_BTN_HOT);
}

static void destroy_theme_resources(panel_t *p) {
    if (p->hfont)       { DeleteObject(p->hfont);       p->hfont = NULL; }
    if (p->hfont_brand) { DeleteObject(p->hfont_brand); p->hfont_brand = NULL; }
    if (p->hbrush_bg)      { DeleteObject(p->hbrush_bg);      p->hbrush_bg = NULL; }
    if (p->hbrush_panel)   { DeleteObject(p->hbrush_panel);   p->hbrush_panel = NULL; }
    if (p->hbrush_mic_bg)  { DeleteObject(p->hbrush_mic_bg);  p->hbrush_mic_bg = NULL; }
    if (p->hbrush_mic_on)  { DeleteObject(p->hbrush_mic_on);  p->hbrush_mic_on = NULL; }
    if (p->hpen_border)    { DeleteObject(p->hpen_border);    p->hpen_border = NULL; }
    if (p->hbrush_btn)     { DeleteObject(p->hbrush_btn);     p->hbrush_btn = NULL; }
    if (p->hbrush_btn_hot) { DeleteObject(p->hbrush_btn_hot); p->hbrush_btn_hot = NULL; }
}

/* --- Helper: apply font to a control --- */
static void set_ctrl_font(HWND hwnd, HFONT font) {
    if (hwnd && font) SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

/* --- Expand/collapse --- */
static void show_kids(HWND *kids, int n, int show) {
    for (int i = 0; i < n; i++) ShowWindow(kids[i], show ? SW_SHOW : SW_HIDE);
}

static void set_expand(panel_t *p, int mode) {
    g_expanded = mode;
    int h = (mode == 1) ? PANEL_H_MEM : (mode == 2) ? PANEL_H_JOIN : PANEL_H_COL;
    show_kids(g_children_mem, g_n_mem, mode == 1);
    show_kids(g_children_join, g_n_join, mode == 2);
    SetWindowPos(p->hwnd, NULL, 0, 0, PANEL_W, h, SWP_NOZORDER | SWP_NOMOVE);
    InvalidateRect(p->hwnd, NULL, TRUE);
}

/* --- Owner-drawn button rendering --- */
static void draw_button(panel_t *p, DRAWITEMSTRUCT *dis) {
    int is_hot = (dis->itemState & ODS_HOTLIGHT) || (dis->itemState & ODS_SELECTED);
    int is_pressed = (dis->itemState & ODS_SELECTED);

    COLORREF bg = is_pressed ? CLR_BTN_PRESS : (is_hot ? CLR_BTN_HOT : CLR_BTN_FACE);
    COLORREF border = is_hot ? CLR_ACCENT : CLR_BORDER;

    /* Rounded rectangle background */
    HBRUSH br = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HBRUSH old_br = (HBRUSH)SelectObject(dis->hDC, br);
    HPEN old_pen = (HPEN)SelectObject(dis->hDC, pen);
    RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
              dis->rcItem.right, dis->rcItem.bottom, 6, 6);
    SelectObject(dis->hDC, old_br);
    SelectObject(dis->hDC, old_pen);
    DeleteObject(br);
    DeleteObject(pen);

    /* Button text */
    wchar_t text[64];
    GetWindowTextW(dis->hwndItem, text, 64);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, CLR_TEXT);
    SelectObject(dis->hDC, p->hfont);
    DrawTextW(dis->hDC, text, -1, &dis->rcItem,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Focus rect */
    if (dis->itemState & ODS_FOCUS) {
        RECT fr = dis->rcItem;
        InflateRect(&fr, -3, -3);
        DrawFocusRect(dis->hDC, &fr);
    }
}

/* --- Window procedure --- */
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
        if (id == IDC_CLOSE) {
            PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_EXIT, 0);
            return 0;
        }
        if (id == IDC_PIN) {
            g_pinned = !g_pinned;
            SetWindowPos(hwnd, g_pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOMOVE);
            /* Update pin button visual */
            HWND pin_btn = GetDlgItem(hwnd, IDC_PIN);
            if (pin_btn) InvalidateRect(pin_btn, NULL, TRUE);
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
        /* BUG FIX: Do NOT re-post unrecognized WM_COMMAND (was infinite loop) */
        return 0;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)l;
        if (dis->CtlType == ODT_BUTTON && p) {
            draw_button(p, dis);
            return TRUE;
        }
        /* Owner-drawn static: volume bar + text (IDC_SPEAKER_TAG) */
        if (dis->CtlType == ODT_STATIC && p && dis->CtlID == IDC_SPEAKER_TAG) {
            /* Volume bar background */
            RECT bar_rc = dis->rcItem;
            FillRect(dis->hDC, &bar_rc, p->hbrush_mic_bg);

            /* Volume bar fill */
            int bar_w = bar_rc.right - bar_rc.left;
            int fill_w = (p->peak_pct * bar_w) / 100;
            if (fill_w > bar_w) fill_w = bar_w;
            if (fill_w > 0) {
                RECT fill_rc = {bar_rc.left, bar_rc.top, bar_rc.left + fill_w, bar_rc.bottom};
                HBRUSH fill_br = CreateSolidBrush(
                    p->peak_pct > 80 ? CLR_ERR :
                    p->peak_pct > 50 ? CLR_OK : CLR_ACCENT);
                FillRect(dis->hDC, &fill_rc, fill_br);
                DeleteObject(fill_br);
            }

            /* Volume text overlay */
            wchar_t buf[32];
            _snwprintf(buf, 32, L"%d%%", p->peak_pct);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, CLR_TEXT);
            SelectObject(dis->hDC, p->hfont);
            DrawTextW(dis->hDC, buf, -1, &dis->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w;
        HWND child = (HWND)l;
        SetBkMode(hdc, TRANSPARENT);
        /* Brand label gets accent color */
        if (GetDlgCtrlID(child) == IDC_BRAND) {
            SetTextColor(hdc, CLR_BRAND);
        } else if (GetDlgCtrlID(child) == IDC_SPEAKER_TAG) {
            SetTextColor(hdc, CLR_OK);
        } else {
            SetTextColor(hdc, CLR_TEXT);
        }
        return (LRESULT)p ? (LRESULT)p->hbrush_bg : (LRESULT)GetStockObject(BLACK_BRUSH);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)w;
        SetBkColor(hdc, CLR_INPUT_BG);
        SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)p ? (LRESULT)p->hbrush_panel : (LRESULT)GetStockObject(BLACK_BRUSH);
    }
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)w;
        SetBkColor(hdc, CLR_PANEL);
        SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)p ? (LRESULT)p->hbrush_panel : (LRESULT)GetStockObject(BLACK_BRUSH);
    }
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)w;
        SetBkColor(hdc, CLR_BG);
        SetTextColor(hdc, CLR_TEXT);
        return (LRESULT)p ? (LRESULT)p->hbrush_bg : (LRESULT)GetStockObject(BLACK_BRUSH);
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)w;
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH br = p ? p->hbrush_bg : CreateSolidBrush(CLR_BG);
        FillRect(hdc, &rc, br);
        if (!p) DeleteObject(br);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        /* Fill background (already done by WM_ERASEBKGND, but be safe) */
        FillRect(hdc, &rc, p ? p->hbrush_bg : CreateSolidBrush(CLR_BG));

        /* Draw rounded border */
        if (p && p->hpen_border) {
            HPEN old_pen = (HPEN)SelectObject(hdc, p->hpen_border);
            HBRUSH old_br = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, 0, 0, rc.right, rc.bottom, 10, 10);
            SelectObject(hdc, old_pen);
            SelectObject(hdc, old_br);
        }

        /* Draw status indicator dot */
        if (p) {
            HWND st = GetDlgItem(hwnd, IDC_STATUS);
            if (st) {
                wchar_t st_text[32];
                GetWindowTextW(st, st_text, 32);
                int connected = (wcsstr(st_text, L"●") != NULL);
                int x = 36, y = 12;
                HBRUSH dot_br = CreateSolidBrush(connected ? CLR_OK : CLR_ERR);
                HBRUSH old = (HBRUSH)SelectObject(hdc, dot_br);
                Ellipse(hdc, x - 4, y - 4, x + 4, y + 4);
                SelectObject(hdc, old);
                DeleteObject(dot_br);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        PostMessageW(hwnd, WM_COMMAND, PANEL_CMD_EXIT, 0);
        return 0;
    case WM_DESTROY:
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

/* --- Create button helper (owner-drawn) --- */
static HWND create_btn(HWND parent, HINSTANCE inst, const wchar_t *text,
                       int x, int y, int w, int h, int id, int visible) {
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | BS_OWNERDRAW,
        x, y, w, h, parent, (HMENU)(intptr_t)id, inst, NULL);
    if (visible) ShowWindow(btn, SW_SHOW);
    return btn;
}

int panel_create(panel_t *p, HINSTANCE inst) {
    static int registered = 0;
    if (!registered) {
        WNDCLASSW wc = {0};
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
        wc.lpfnWndProc   = panel_wndproc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;  /* we handle WM_ERASEBKGND */
        wc.lpszClassName = L"QminiHUDPanel";
        RegisterClassW(&wc);
        registered = 1;
    }

    p->hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        L"QminiHUDPanel", L"Qmini",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, PANEL_W, PANEL_H_COL,
        NULL, NULL, inst, NULL);
    if (!p->hwnd) return 0;

    SetWindowLongPtrW(p->hwnd, GWLP_USERDATA, (LONG_PTR)p);
    p->inst = inst;
    p->muted = 0;
    p->peak_pct = 0;
    p->member_count = 0;
    p->btn_hover = 0;
    p->server[0] = 0;
    p->room[0] = 0;

    /* Create theme resources */
    create_theme_resources(p);

    HWND pw = p->hwnd;
    HINSTANCE hi = inst;

    /*
     * Row 1 (y=6):  [Qmini] [●已连接] [2人] [Mlaiou] [pin] [+加入] [X]
     * Row 2 (y=30): [vol bar] [静音] [说话] [自由] [离开]
     */

    /* --- Row 1: status bar --- */
    HWND brand = CreateWindowExW(0, L"STATIC", L"Qmini",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        PAD, ROW1_Y, 36, 16, pw, (HMENU)IDC_BRAND, hi, NULL);
    set_ctrl_font(brand, p->hfont_brand);

    HWND status = CreateWindowExW(0, L"STATIC", L" 未连接",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        44, ROW1_Y, 72, 16, pw, (HMENU)IDC_STATUS, hi, NULL);
    set_ctrl_font(status, p->hfont);

    /* Member button (hidden until connected) */
    HWND mbtn = create_btn(pw, hi, L"0 人", 118, ROW1_Y, 44, BTN_H, IDC_MEMBER_BTN, 0);
    set_ctrl_font(mbtn, p->hfont);

    /* Brand watermark */
    HWND mlbl = CreateWindowExW(0, L"STATIC", L"Mlaiou",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        PANEL_W - 160, ROW1_Y, 50, 16, pw, NULL, hi, NULL);
    set_ctrl_font(mlbl, p->hfont);

    /* Pin button (always visible) */
    HWND pinb = create_btn(pw, hi, L"\U0001F4CC", PANEL_W - 46, ROW1_Y, 22, BTN_H, IDC_PIN, 1);
    set_ctrl_font(pinb, p->hfont);

    /* Join / Cancel (same position, alternate visibility) */
    HWND join_btn = create_btn(pw, hi, L"+加入",
                               PANEL_W - 104, ROW1_Y, 50, BTN_H, IDC_JOIN, 1);
    set_ctrl_font(join_btn, p->hfont);

    HWND canc = create_btn(pw, hi, L"取消",
                           PANEL_W - 104, ROW1_Y, 50, BTN_H, IDC_CANCEL, 0);
    set_ctrl_font(canc, p->hfont);

    /* Close button (always visible) */
    HWND close_btn = create_btn(pw, hi, L"X",
                                PANEL_W - 22, ROW1_Y, 18, BTN_H, IDC_CLOSE, 1);
    set_ctrl_font(close_btn, p->hfont);

    /* --- Row 2: volume bar + mode buttons + leave --- */
    /* Volume bar: SS_OWNERDRAW, drawn by parent in WM_DRAWITEM */
    HWND stag = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_OWNERDRAW,
        PAD, ROW2_Y + 2, 80, BTN_H - 4, pw, (HMENU)IDC_SPEAKER_TAG, hi, NULL);
    set_ctrl_font(stag, p->hfont);

    /* Mode buttons */
    HWND mute = create_btn(pw, hi, L"静音",
                           96, ROW2_Y, 42, BTN_H, IDC_MODE_MUTED, 0);
    set_ctrl_font(mute, p->hfont);

    HWND ptt = create_btn(pw, hi, L"说话",
                          140, ROW2_Y, 42, BTN_H, IDC_MODE_PTT, 0);
    set_ctrl_font(ptt, p->hfont);

    HWND open_m = create_btn(pw, hi, L"自由",
                             184, ROW2_Y, 42, BTN_H, IDC_MODE_OPEN, 0);
    set_ctrl_font(open_m, p->hfont);

    /* Leave button */
    HWND leave = create_btn(pw, hi, L"离开",
                            PANEL_W - 54, ROW2_Y, 46, BTN_H, IDC_LEAVE, 0);
    set_ctrl_font(leave, p->hfont);

    /* === Expand area: members (hidden, starts at y=PANEL_H_COL) === */
    int ey = PANEL_H_COL;
    HWND minfo = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | SS_LEFT,
        PAD + 2, ey + 4, PANEL_W - 2 * PAD - 4, 14, pw, (HMENU)IDC_EXPAND_INFO, hi, NULL);
    set_ctrl_font(minfo, p->hfont);

    HWND mlist = CreateWindowExW(0, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
        PAD + 2, ey + 22, PANEL_W - 2 * PAD - 4, 106, pw, (HMENU)IDC_MEMBER_LIST, hi, NULL);
    set_ctrl_font(mlist, p->hfont);

    ShowWindow(minfo, SW_HIDE);
    ShowWindow(mlist, SW_HIDE);
    g_children_mem[0] = minfo;
    g_children_mem[1] = mlist;
    g_n_mem = 2;

    /* === Expand area: join form (hidden) === */
    int jy = PANEL_H_COL;  /* form starts right after collapsed area */
    int edit_w = PANEL_W - 2 * (PAD + 4);
    int half_w = (edit_w - PAD) / 2;  /* 2x half_w + PAD = edit_w */

    /*
     * Join form layout (jy = PANEL_H_COL = 56):
     *   服务器地址    ← jy+0
     *   [  edit   ]   ← jy+24  (gap 10px)
     *   房间名  昵称   ← jy+56
     *   [edit] [edit]  ← jy+72  (gap 2px)
     *   密码 (可选)    ← jy+102
     *   [  edit   ]    ← jy+122 (gap 6px)
     *   [确定] [取消]  ← jy+154
     */
    /*
     * Join form layout (jy = PANEL_H_COL = 56):
     *   服务器地址    ← jy+0
     *   [  edit   ]   ← jy+20  (gap 8px)
     *   房间名  昵称   ← jy+50
     *   [edit] [edit]  ← jy+64  (gap 2px)
     *   密码 (可选)    ← jy+92
     *   [  edit   ]    ← jy+108 (gap 4px)
     *   [确定] [取消]  ← jy+138
     */
    HWND lbl_server = CreateWindowExW(0, L"STATIC", L"服务器地址",
        WS_CHILD, PAD + 4, jy + 0, 80, 12, pw, NULL, hi, NULL);
    set_ctrl_font(lbl_server, p->hfont);

    HWND se = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        PAD + 4, jy + 20, edit_w, 22, pw, (HMENU)IDC_SERVER_EDIT, hi, NULL);
    set_ctrl_font(se, p->hfont);

    HWND lbl_room = CreateWindowExW(0, L"STATIC", L"房间名",
        WS_CHILD, PAD + 4, jy + 52, 50, 12, pw, NULL, hi, NULL);
    set_ctrl_font(lbl_room, p->hfont);

    HWND lbl_nick = CreateWindowExW(0, L"STATIC", L"昵称",
        WS_CHILD, PAD + 4 + half_w + PAD, jy + 52, 50, 12, pw, NULL, hi, NULL);
    set_ctrl_font(lbl_nick, p->hfont);

    HWND re = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        PAD + 4, jy + 66, half_w, 22, pw, (HMENU)IDC_ROOM_EDIT, hi, NULL);
    set_ctrl_font(re, p->hfont);

    HWND ne = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        PAD + 4 + half_w + PAD, jy + 66, half_w, 22, pw, (HMENU)IDC_NICK_EDIT, hi, NULL);
    set_ctrl_font(ne, p->hfont);

    HWND lbl_pass = CreateWindowExW(0, L"STATIC", L"密码 (可选)",
        WS_CHILD, PAD + 4, jy + 96, 80, 12, pw, NULL, hi, NULL);
    set_ctrl_font(lbl_pass, p->hfont);

    HWND pe = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL,
        PAD + 4, jy + 112, edit_w, 22, pw, (HMENU)IDC_PASS_EDIT, hi, NULL);
    set_ctrl_font(pe, p->hfont);

    HWND okb = create_btn(pw, hi, L"确定",
                          PAD + 4, jy + 142, half_w, BTN_H, IDC_JOIN_OK, 0);
    set_ctrl_font(okb, p->hfont);

    HWND nob = create_btn(pw, hi, L"取消",
                          PAD + 4 + half_w + PAD, jy + 142, half_w, BTN_H, IDC_JOIN_CANCEL, 0);
    set_ctrl_font(nob, p->hfont);

    /* Store join children (including labels for show/hide) */
    g_children_join[0] = lbl_server; g_children_join[1] = se;
    g_children_join[2] = lbl_room;   g_children_join[3] = re;
    g_children_join[4] = lbl_nick;   g_children_join[5] = ne;
    g_children_join[6] = lbl_pass;   g_children_join[7] = pe;
    g_children_join[8] = okb;        g_children_join[9] = nob;
    g_n_join = 10;
    show_kids(g_children_join, g_n_join, 0);

    ShowWindow(p->hwnd, SW_SHOW);
    UpdateWindow(p->hwnd);
    return 1;
}

void panel_destroy(panel_t *p) {
    destroy_theme_resources(p);
    if (p->hwnd) DestroyWindow(p->hwnd);
    memset(p, 0, sizeof(*p));
}

void panel_set_muted(panel_t *p, int muted) {
    p->muted = muted;
    HWND m = GetDlgItem(p->hwnd, IDC_MODE_MUTED);
    if (m) {
        SetWindowTextW(m, muted ? L"已静音" : L"静音");
        InvalidateRect(m, NULL, TRUE);
    }
}

void panel_set_input_mode(panel_t *p, int mode) {
    HWND m = GetDlgItem(p->hwnd, IDC_MODE_MUTED);
    HWND t = GetDlgItem(p->hwnd, IDC_MODE_PTT);
    HWND o = GetDlgItem(p->hwnd, IDC_MODE_OPEN);
    if (!m || !t || !o) return;

    if (mode == INPUT_MODE_MUTED) {
        SetWindowTextW(m, L"已静音");
        SetWindowTextW(t, L"说话");
        SetWindowTextW(o, L"自由");
    } else if (mode == INPUT_MODE_PTT) {
        SetWindowTextW(m, L"静音");
        SetWindowTextW(t, L"说话");
        SetWindowTextW(o, L"自由");
    } else {
        SetWindowTextW(m, L"静音");
        SetWindowTextW(t, L"说话");
        SetWindowTextW(o, L"自由");
    }
    /* Highlight active mode button by changing text */
    if (mode == INPUT_MODE_MUTED) SetWindowTextW(m, L"[静音]");
    if (mode == INPUT_MODE_PTT)   SetWindowTextW(t, L"[说话]");
    if (mode == INPUT_MODE_OPEN)  SetWindowTextW(o, L"[自由]");

    InvalidateRect(m, NULL, TRUE);
    InvalidateRect(t, NULL, TRUE);
    InvalidateRect(o, NULL, TRUE);
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
        if (st) SetWindowTextW(st, L" 未连接");
        ShowWindow(jb, SW_SHOW);
        ShowWindow(cb, SW_HIDE);
        ShowWindow(mb, SW_HIDE);
        /* pin button stays visible */
        ShowWindow(mu, SW_HIDE);
        ShowWindow(pt, SW_HIDE);
        ShowWindow(op, SW_HIDE);
        ShowWindow(lv, SW_HIDE);
        ShowWindow(sg, SW_HIDE);
        g_expanded = 0;
        SetWindowPos(p->hwnd, NULL, 0, 0, PANEL_W, PANEL_H_COL, SWP_NOZORDER | SWP_NOMOVE);
    }
    /* Refresh status indicator and background */
    InvalidateRect(p->hwnd, NULL, TRUE);
}

void panel_set_volume(panel_t *p, int peak_pct) {
    p->peak_pct = peak_pct;
    /* Redraw the speaker tag (SS_OWNERDRAW) which draws the volume bar */
    HWND tag = GetDlgItem(p->hwnd, IDC_SPEAKER_TAG);
    if (tag) InvalidateRect(tag, NULL, FALSE);
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
        _snwprintf(cnt, 32, L"%d 人", count);
        SetWindowTextW(btn, cnt);
        InvalidateRect(btn, NULL, TRUE);
    }
    if (inf && p->server[0]) {
        wchar_t wbuf[256];
        _snwprintf(wbuf, 256, L"%hs @ %hs", p->room, p->server);
        SetWindowTextW(inf, wbuf);
    }
}
