#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include <stdint.h>

typedef struct {
    char  server_addr[64];
    char  nickname[32];
    int   ptt_key;
    int   mute_key;
    int   enable_fec;
    int   noise_gate_mult;     /* threshold multiplier * 10 (default 30 = 3.0x) */
    int   noise_gate_hold_ms;  /* hold-open time in ms (default 200) */
    int   peer_gain;           /* per-peer volume * 100 (default 100 = 1.0x) */
} config_t;

int  config_load(config_t *cfg);
int  config_save(config_t *cfg);

#endif
