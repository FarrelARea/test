#ifndef VKEY_DISPLAY_H
#define VKEY_DISPLAY_H

#include "esp_err.h"
#include "vkey_net.h"

/**
 * Called by qmsd_gui_init() before the LVGL task starts.
 * Must only create LVGL objects; never block.
 */
void vkey_display_init(void);

/**
 * Called once the network is ready (any mode). Fills the QR with the
 * pairing URL and switches the screen state machine on.
 */
void vkey_display_start(void);

/**
 * Thread-safe: queues a NET_READY event for the LVGL task.
 * Called from the network task; may be NULL-argued.
 */
void vkey_display_net_ready(vkey_net_mode_t mode, const char *ip);

#endif
