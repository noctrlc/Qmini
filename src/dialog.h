#ifndef DIALOG_H
#define DIALOG_H

#define IDD_JOIN_DIALOG 100
#define IDC_SERVER     1001
#define IDC_ROOM       1002
#define IDC_NICKNAME   1003
#define IDC_SERVER_LABEL  1004
#define IDC_ROOM_LABEL    1005
#define IDC_NICKNAME_LABEL 1006

#ifndef RC_INVOKED
#include <windows.h>
int join_dialog_show(HINSTANCE inst, HWND parent,
                     char *server, int server_max,
                     char *room, int room_max,
                     char *nickname, int nickname_max);
#endif

#endif
