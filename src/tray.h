#ifndef TRAY_H
#define TRAY_H

#include <windows.h>

/* Command IDs are defined in panel.h; tray.h reuses them.
   When panel.h is not included, define fallbacks here. */
#ifndef _PANEL_CMD_DEFINED
#define _PANEL_CMD_DEFINED
typedef enum {
    PANEL_CMD_JOIN_ROOM = 1,
    PANEL_CMD_LEAVE_ROOM,
    PANEL_CMD_TOGGLE_MUTE,
    PANEL_CMD_MODE_MUTED,
    PANEL_CMD_MODE_PTT,
    PANEL_CMD_MODE_OPEN,
    PANEL_CMD_TEST_AUDIO,
    PANEL_CMD_AUDIO_DEVICES,
    PANEL_CMD_EXIT,
} panel_cmd_t;
#endif

/* TRAY_CMD_* aliases (use PANEL_CMD_* if panel.h already defined them) */
#ifndef TRAY_CMD_JOIN_ROOM
#define TRAY_CMD_JOIN_ROOM     PANEL_CMD_JOIN_ROOM
#define TRAY_CMD_LEAVE_ROOM    PANEL_CMD_LEAVE_ROOM
#define TRAY_CMD_TOGGLE_MUTE   PANEL_CMD_TOGGLE_MUTE
#define TRAY_CMD_MODE_MUTED    PANEL_CMD_MODE_MUTED
#define TRAY_CMD_MODE_PTT      PANEL_CMD_MODE_PTT
#define TRAY_CMD_MODE_OPEN     PANEL_CMD_MODE_OPEN
#define TRAY_CMD_TEST_AUDIO    PANEL_CMD_TEST_AUDIO
#define TRAY_CMD_AUDIO_DEVICES PANEL_CMD_AUDIO_DEVICES
#define TRAY_CMD_EXIT          PANEL_CMD_EXIT
#endif

#ifndef INPUT_MODE_MUTED
#define INPUT_MODE_MUTED 0
#define INPUT_MODE_PTT   1
#define INPUT_MODE_OPEN  2
#endif

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
