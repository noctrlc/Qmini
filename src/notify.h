#ifndef NOTIFY_H
#define NOTIFY_H

#include <windows.h>

/* Initialize audio device notification monitoring.
   Sets flag to 1 when the default communications device (capture or render) changes. */
int  notify_init(void *enumerator, int *restart_capture, int *restart_playback);
/* Unregister notification client. Must pass the same enumerator. */
void notify_shutdown(void *enumerator, void *client);
/* Get the notification client handle (for shutdown) */
void* notify_get_client(void);

#endif
