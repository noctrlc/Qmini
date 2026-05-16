#ifndef PANEL_H
#define PANEL_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Command IDs */
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

#define TRAY_CMD_JOIN_ROOM     PANEL_CMD_JOIN_ROOM
#define TRAY_CMD_LEAVE_ROOM    PANEL_CMD_LEAVE_ROOM
#define TRAY_CMD_TOGGLE_MUTE   PANEL_CMD_TOGGLE_MUTE
#define TRAY_CMD_MODE_MUTED    PANEL_CMD_MODE_MUTED
#define TRAY_CMD_MODE_PTT      PANEL_CMD_MODE_PTT
#define TRAY_CMD_MODE_OPEN     PANEL_CMD_MODE_OPEN
#define TRAY_CMD_TEST_AUDIO    PANEL_CMD_TEST_AUDIO
#define TRAY_CMD_AUDIO_DEVICES PANEL_CMD_AUDIO_DEVICES
#define TRAY_CMD_EXIT          PANEL_CMD_EXIT

#define INPUT_MODE_MUTED 0
#define INPUT_MODE_PTT   1
#define INPUT_MODE_OPEN  2

#define IDC_STATUS_GROUP    2000
#define IDC_SERVER_LBL      2001
#define IDC_SERVER_VAL      2002
#define IDC_ROOM_LBL        2003
#define IDC_ROOM_VAL        2004
#define IDC_MEMBERS_LBL     2005
#define IDC_MIC_GROUP       2006
#define IDC_MIC_BAR         2007
#define IDC_MIC_PCT         2008
#define IDC_MEMBERS_GROUP   2009
#define IDC_MEMBER_LIST     2010
#define IDC_BRAND_LABEL     2011

typedef struct {
    HWND     hwnd;
    HINSTANCE inst;
    HFONT    hfont;
    HFONT    hfont_brand;
    HBRUSH   hbrush_mic_bg;
    HBRUSH   hbrush_mic_on;
    int      muted;
    int      peak_pct;
    int      member_count;
    char     server[64];
    char     room[32];
} panel_t;

int  panel_create(panel_t *p, HINSTANCE inst);
void panel_destroy(panel_t *p);
void panel_set_muted(panel_t *p, int muted);
void panel_set_input_mode(panel_t *p, int mode);
void panel_set_connection(panel_t *p, const char *server, const char *room);
void panel_set_volume(panel_t *p, int peak_pct);
void panel_set_members(panel_t *p, const char *names[], int count);
LRESULT CALLBACK panel_wndproc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

/* Direct2D panel: read join params from inline EDIT controls */
const char* panel_get_join_server(void);
const char* panel_get_join_room(void);
const char* panel_get_join_nick(void);
const char* panel_get_join_pass(void);

static void panel_apply_theme(HWND hwnd) { (void)hwnd; }

#ifdef __cplusplus
}
#endif

#endif
