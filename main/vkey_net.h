#ifndef VKEY_NET_H
#define VKEY_NET_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    VKEY_NET_CONNECTING,
    VKEY_NET_STATION, /* joined router, IP available */
    VKEY_NET_SOFTAP,  /* router unreachable, own AP running */
} vkey_net_mode_t;

typedef void (*vkey_net_ready_cb_t)(vkey_net_mode_t mode, const char *ip);

/**
 * Start WiFi (station mode). If no IP within CONFIG_VKEY_AP_FALLBACK_TIMEOUT_MS
 * falls back to soft-AP ("VKEY-XXXX") with a captive-portal DNS server.
 * Never blocks forever; calls `cb` once with the final mode + device IP.
 */
void vkey_net_start(vkey_net_ready_cb_t cb);

/** Current IP of the active interface (NULL until ready). */
const char *vkey_net_ip(void);

#endif
