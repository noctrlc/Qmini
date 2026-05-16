#include "config.h"
#include <shlobj.h>

static const char *DEFAULT_SERVER = "192.144.133.168:9088";
static const char *DEFAULT_NAME   = "Player";

static void config_set_defaults(config_t *cfg) {
    lstrcpyA(cfg->server_addr, DEFAULT_SERVER);
    lstrcpyA(cfg->nickname, DEFAULT_NAME);
    cfg->ptt_key            = VK_XBUTTON1;
    cfg->mute_key           = VK_F13;
    cfg->enable_fec         = 1;
    cfg->noise_gate_mult    = 30;    /* 3.0x */
    cfg->noise_gate_hold_ms = 200;
    cfg->peer_gain          = 100;   /* 1.0x */
}

static void get_path(char *path, size_t sz) {
    SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path);
    lstrcatA(path, "\\Qmini");
    CreateDirectoryA(path, NULL);
    lstrcatA(path, "\\config.ini");
}

int config_load(config_t *cfg) {
    char path[MAX_PATH];
    config_set_defaults(cfg);
    get_path(path, sizeof(path));

    GetPrivateProfileStringA("qmini", "server", DEFAULT_SERVER, cfg->server_addr, sizeof(cfg->server_addr), path);
    GetPrivateProfileStringA("qmini", "nickname", DEFAULT_NAME, cfg->nickname, sizeof(cfg->nickname), path);
    cfg->ptt_key            = (int)GetPrivateProfileIntA("qmini", "ptt_key", VK_XBUTTON1, path);
    cfg->mute_key           = (int)GetPrivateProfileIntA("qmini", "mute_key", VK_F13, path);
    cfg->enable_fec         = (int)GetPrivateProfileIntA("qmini", "enable_fec", 1, path);
    cfg->noise_gate_mult    = (int)GetPrivateProfileIntA("qmini", "noise_gate_mult", 30, path);
    cfg->noise_gate_hold_ms = (int)GetPrivateProfileIntA("qmini", "noise_gate_hold_ms", 200, path);
    cfg->peer_gain          = (int)GetPrivateProfileIntA("qmini", "peer_gain", 100, path);
    return 1;
}

int config_save(config_t *cfg) {
    char path[MAX_PATH];
    char val[16];
    get_path(path, sizeof(path));
    WritePrivateProfileStringA("qmini", "server", cfg->server_addr, path);
    WritePrivateProfileStringA("qmini", "nickname", cfg->nickname, path);
    wsprintfA(val, "%d", cfg->ptt_key);            WritePrivateProfileStringA("qmini", "ptt_key", val, path);
    wsprintfA(val, "%d", cfg->mute_key);           WritePrivateProfileStringA("qmini", "mute_key", val, path);
    wsprintfA(val, "%d", cfg->enable_fec);         WritePrivateProfileStringA("qmini", "enable_fec", val, path);
    wsprintfA(val, "%d", cfg->noise_gate_mult);    WritePrivateProfileStringA("qmini", "noise_gate_mult", val, path);
    wsprintfA(val, "%d", cfg->noise_gate_hold_ms); WritePrivateProfileStringA("qmini", "noise_gate_hold_ms", val, path);
    wsprintfA(val, "%d", cfg->peer_gain);          WritePrivateProfileStringA("qmini", "peer_gain", val, path);
    return 1;
}
