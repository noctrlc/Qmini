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
} config_t;

int  config_load(config_t *cfg);
int  config_save(config_t *cfg);

#endif
