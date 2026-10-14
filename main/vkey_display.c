#include "vkey_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "esp_log.h"

#include "vkey_net.h"
#include "vkey_server.h"

static const char *TAG = "vkey_display";

#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 480
#define SCREEN_BG lv_color_hex(0x111111)
#define COLOR_TEXT lv_color_hex(0xF4EFE3)
#define COLOR_MUTED lv_color_hex(0xA8A090)
#define COLOR_ACCENT lv_color_hex(0xE0532E)

typedef enum {
    ST_CONNECTING, /* wifi/net not ready yet */
    ST_WAIT_PAIR,  /* QR on screen */
    ST_CONNECTED,  /* phone bound, typing screen */
} vkey_display_state_t;

static QueueHandle_t s_evt_queue;
static vkey_display_state_t s_state = ST_CONNECTING;
static vkey_net_mode_t s_net_mode = VKEY_NET_CONNECTING;

static lv_obj_t *s_qr_screen;
static lv_obj_t *s_qr_status;
static lv_obj_t *s_qr_hint;
static lv_obj_t *s_qr_image;
static lv_obj_t *s_type_screen;
static lv_obj_t *s_type_status;
static lv_obj_t *s_type_text;
static lv_obj_t *s_type_scroll;
static char s_text[CONFIG_VKEY_MAX_TEXT_LEN + 1];

/* ---------------------------------------------------------------------- */
static void set_state(vkey_display_state_t st)
{
    s_state = st;
    lv_obj_add_flag(s_qr_screen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_type_screen, LV_OBJ_FLAG_HIDDEN);
    if (st == ST_CONNECTED) {
        lv_obj_clear_flag(s_type_screen, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_qr_screen, LV_OBJ_FLAG_HIDDEN);
    }

    if (st == ST_WAIT_PAIR) {
        lv_label_set_text(s_qr_status, "Scan QR dengan handphone");
        lv_label_set_text(s_qr_hint, "Buka kamera, arahkan ke QR");
    } else if (st == ST_CONNECTING) {
        lv_label_set_text(s_qr_status, "Menghubungkan...");
        lv_label_set_text(s_qr_hint, "");
    }
    if (st == ST_CONNECTED) {
        lv_label_set_text(s_type_status, "Terhubung");
    }
}

static void refresh_qr(void)
{
    const char *url = vkey_pair_url();
    if (!url) {
        return;
    }
    lv_qrcode_update(s_qr_image, url, strlen(url));
    lv_obj_center(s_qr_image);
}

static void on_net_ready(vkey_net_mode_t mode, const char *ip)
{
    if (mode == VKEY_NET_SOFTAP) {
        lv_label_set_text(s_qr_hint, "WiFi: VKEY-xxxx  (tanpa password)");
    } else {
        lv_label_set_text(s_qr_hint, "WiFi: jaringan rumah Anda");
    }
    refresh_qr();
    set_state(ST_WAIT_PAIR);
}

static void render_text(void)
{
    lv_label_set_text(s_type_text, s_text[0] ? s_text : " ");
    lv_obj_update_layout(s_type_text);
    lv_obj_scroll_to_y(s_type_scroll, lv_obj_get_height(s_type_text), LV_ANIM_OFF);
}

/* ---------------------------------------------------------------------- */
static void clear_pressed(lv_event_t *e)
{
    s_text[0] = '\0';
    render_text();
    vkey_push_clear(); /* also empties the phone textarea */
}

static void build_qr_screen(void)
{
    s_qr_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_qr_screen, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(s_qr_screen, SCREEN_BG, 0);
    lv_obj_set_style_border_width(s_qr_screen, 0, 0);
    lv_obj_set_style_pad_all(s_qr_screen, 0, 0);

    s_qr_status = lv_label_create(s_qr_screen);
    lv_obj_align(s_qr_status, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_color(s_qr_status, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_qr_status, &lv_font_montserrat_24, 0);
    lv_label_set_text(s_qr_status, "Menghubungkan...");

    s_qr_hint = lv_label_create(s_qr_screen);
    lv_obj_align(s_qr_hint, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_text_color(s_qr_hint, COLOR_MUTED, 0);
    lv_obj_set_style_text_font(s_qr_hint, &lv_font_montserrat_14, 0);

    s_qr_image = lv_qrcode_create(s_qr_screen, 300, COLOR_TEXT, SCREEN_BG);
    lv_obj_center(s_qr_image);
    /* placeholder until the pairing URL exists */
    lv_qrcode_update(s_qr_image, "https://github.com", 20);
    lv_obj_center(s_qr_image);
}

static void build_type_screen(void)
{
    s_type_screen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_type_screen, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(s_type_screen, SCREEN_BG, 0);
    lv_obj_set_style_border_width(s_type_screen, 0, 0);
    lv_obj_set_style_pad_all(s_type_screen, 0, 0);

    s_type_status = lv_label_create(s_type_screen);
    lv_obj_align(s_type_status, LV_ALIGN_TOP_LEFT, 12, 12);
    lv_obj_set_style_text_color(s_type_status, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_type_status, &lv_font_montserrat_14, 0);
    lv_label_set_text(s_type_status, "Terhubung");

    s_type_scroll = lv_obj_create(s_type_screen);
    lv_obj_set_pos(s_type_scroll, 0, 44);
    lv_obj_set_size(s_type_scroll, DISPLAY_WIDTH, DISPLAY_HEIGHT - 44 - 64);
    lv_obj_set_style_bg_color(s_type_scroll, SCREEN_BG, 0);
    lv_obj_set_style_border_width(s_type_scroll, 0, 0);
    lv_obj_set_style_pad_all(s_type_scroll, 10, 0);
    lv_obj_set_scrollbar_mode(s_type_scroll, LV_SCROLLBAR_MODE_AUTO);

    s_type_text = lv_label_create(s_type_scroll);
    lv_label_set_long_mode(s_type_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_type_text, DISPLAY_WIDTH - 20);
    lv_obj_set_style_text_color(s_type_text, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_type_text, &lv_font_montserrat_36, 0);
    lv_label_set_text(s_type_text, " ");

    lv_obj_t *btn = lv_btn_create(s_type_screen);
    lv_obj_set_size(btn, 110, 48);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(btn, COLOR_ACCENT, 0);
    lv_obj_add_event_cb(btn, clear_pressed, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "Clear");
    lv_obj_center(btn_lbl);
}

/* ---------------------------------------------------------------------- */
static void evt_task(lv_timer_t *timer)
{
    vkey_event_t evt;
    while (xQueueReceive(s_evt_queue, &evt, 0) == pdTRUE) {
        switch (evt.type) {
        case VKEY_EVT_NET_READY:
            on_net_ready(s_net_mode, NULL);
            break;
        case VKEY_EVT_PHONE_CONNECTED:
            set_state(ST_CONNECTED);
            render_text();
            break;
        case VKEY_EVT_PHONE_DISCONNECTED:
            set_state(ST_WAIT_PAIR);
            lv_label_set_text(s_qr_status, "HP terputus — scan ulang");
            break;
        case VKEY_EVT_TEXT:
            strlcpy(s_text, evt.text, sizeof(s_text));
            render_text();
            break;
        case VKEY_EVT_CLEAR:
            s_text[0] = '\0';
            render_text();
            break;
        }
    }
}

static void server_evt_bridge(const vkey_event_t *evt)
{
    if (s_evt_queue) {
        vkey_event_t copy = *evt;
        xQueueSend(s_evt_queue, &copy, 0);
    }
}

void vkey_display_init(void)
{
    build_qr_screen();
    build_type_screen();
    set_state(ST_CONNECTING);

    s_evt_queue = xQueueCreate(8, sizeof(vkey_event_t));
    vkey_server_set_cb(server_evt_bridge);
}

void vkey_display_start(void)
{
    lv_timer_create(evt_task, 40, NULL);
}

void vkey_display_net_ready(vkey_net_mode_t mode, const char *ip)
{
    (void)ip;
    s_net_mode = mode;
    vkey_event_t evt = {.type = VKEY_EVT_NET_READY};
    if (s_evt_queue) {
        xQueueSend(s_evt_queue, &evt, 0);
    }
}
