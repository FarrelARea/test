#ifndef VKEY_SERVER_H
#define VKEY_SERVER_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef enum {
    VKEY_EVT_NET_READY, /* network up, pairing URL available */
    VKEY_EVT_PHONE_CONNECTED,
    VKEY_EVT_PHONE_DISCONNECTED,
    VKEY_EVT_TEXT, /* phone sent full text */
    VKEY_EVT_CLEAR,
} vkey_evt_type_t;

typedef struct {
    vkey_evt_type_t type;
    char text[CONFIG_VKEY_MAX_TEXT_LEN + 1];
} vkey_event_t;

typedef void (*vkey_evt_cb_t)(const vkey_event_t *evt);

/** Current pairing URL shown on the display: http://<ip>/?s=<session> */
const char *vkey_pair_url(void);

/** True while a phone is bound to this session. */
bool vkey_phone_connected(void);

/** Push a control message to the phone (server -> phone direction). */
esp_err_t vkey_push_clear(void);

/** Start httpd + WebSocket server. */
esp_err_t vkey_server_start(void);

/** Event sink for UI updates (display task). */
void vkey_server_set_cb(vkey_evt_cb_t cb);

#endif
