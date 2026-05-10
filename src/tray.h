#ifndef TRAY_H
#define TRAY_H

#include <windows.h>

typedef enum {
    TRAY_CMD_JOIN_ROOM = 1,
    TRAY_CMD_LEAVE_ROOM,
    TRAY_CMD_TOGGLE_MUTE,
    TRAY_CMD_MODE_MUTED,
    TRAY_CMD_MODE_PTT,
    TRAY_CMD_MODE_OPEN,
    TRAY_CMD_TEST_AUDIO,
    TRAY_CMD_AUDIO_DEVICES,
    TRAY_CMD_EXIT,
} tray_cmd_t;

#define INPUT_MODE_MUTED 0
#define INPUT_MODE_PTT   1
#define INPUT_MODE_OPEN  2

typedef struct {
    HWND     hwnd;
    HMENU    menu;
    HMENU    mode_menu;
    HMENU    member_menu;    /* submenu for room members */
    HICON    icon;
    HICON    icon_active;
    int      muted;
    int      active;
    int      peak_pct;
    int      member_count;   /* number of members in room */
    HINSTANCE inst;
    UINT     wm_trayicon;
    char     server[64];
    char     room[32];
} tray_t;

int  tray_create(tray_t *t, HINSTANCE inst);
void tray_destroy(tray_t *t);
void tray_set_muted(tray_t *t, int muted);
void tray_set_input_mode(tray_t *t, int mode);
void tray_set_connection(tray_t *t, const char *server, const char *room);
void tray_set_volume(tray_t *t, int peak_pct);
void tray_set_members(tray_t *t, const char *names[], int count);
LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

#endif
