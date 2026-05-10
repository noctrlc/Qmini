#include "dialog.h"
#include <windows.h>
#include <stdlib.h>

typedef struct {
    char *server;
    int   server_max;
    char *room;
    int   room_max;
    char *nickname;
    int   nickname_max;
} join_ctx_t;

static INT_PTR CALLBACK join_dlg_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        join_ctx_t *ctx = (join_ctx_t*)lParam;
        SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)ctx);
        SetWindowTextW(hDlg, L"加入房间");
        SetDlgItemTextW(hDlg, IDC_SERVER_LABEL, L"服务器:");
        SetDlgItemTextW(hDlg, IDC_ROOM_LABEL, L"房间名:");
        SetDlgItemTextW(hDlg, IDC_NICKNAME_LABEL, L"昵称:");
        SetDlgItemTextW(hDlg, IDOK, L"确定");
        SetDlgItemTextW(hDlg, IDCANCEL, L"取消");
        SetDlgItemTextA(hDlg, IDC_SERVER, ctx->server);
        SetDlgItemTextA(hDlg, IDC_ROOM, ctx->room);
        SetDlgItemTextA(hDlg, IDC_NICKNAME, ctx->nickname);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            join_ctx_t *ctx = (join_ctx_t*)GetWindowLongPtr(hDlg, DWLP_USER);
            GetDlgItemTextA(hDlg, IDC_SERVER, ctx->server, ctx->server_max);
            GetDlgItemTextA(hDlg, IDC_ROOM, ctx->room, ctx->room_max);
            GetDlgItemTextA(hDlg, IDC_NICKNAME, ctx->nickname, ctx->nickname_max);
            EndDialog(hDlg, 1);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int join_dialog_show(HINSTANCE inst, HWND parent,
                     char *server, int server_max,
                     char *room, int room_max,
                     char *nickname, int nickname_max) {
    join_ctx_t ctx;
    ctx.server      = server;
    ctx.server_max  = server_max;
    ctx.room        = room;
    ctx.room_max    = room_max;
    ctx.nickname    = nickname;
    ctx.nickname_max = nickname_max;

    return (int)DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_JOIN_DIALOG),
                                parent, join_dlg_proc, (LPARAM)&ctx);
}
