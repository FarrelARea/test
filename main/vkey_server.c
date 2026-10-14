#include "vkey_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "vkey_net.h"

static const char *TAG = "vkey_server";

#define SESSION_HEX_LEN 16

static char s_pair_url[96];
static char s_session[SESSION_HEX_LEN + 1];
static bool s_phone_connected;
static httpd_handle_t s_server;
static int s_phone_sock = -1;
static vkey_evt_cb_t s_evt_cb;
static QueueHandle_t s_evt_queue;

bool vkey_phone_connected(void)
{
    return s_phone_connected;
}

const char *vkey_pair_url(void)
{
    return s_pair_url;
}

void vkey_server_set_cb(vkey_evt_cb_t cb)
{
    s_evt_cb = cb;
}

/* ----------------------------------------------------------------------- */
static void notify(vkey_evt_type_t type, const char *text)
{
    vkey_event_t evt = {.type = type};
    if (text) {
        strlcpy(evt.text, text, sizeof(evt.text));
    }
    if (s_evt_queue) {
        xQueueSend(s_evt_queue, &evt, 0);
    }
}

/* ---- websocket -------------------------------------------------------- */
static esp_err_t ws_handler(httpd_req_t *req)
{
    const char *query = req->uri + strlen("/ws");
    if (*query == '?') {
        query++;
    }

    /* reject handshake unless the session id matches */
    if (strncmp(query, "s=", 2) != 0 || strcmp(query + 2, s_session) != 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "ERR_SESSION", -1);
        ESP_LOGW(TAG, "ws rejected: bad session '%s'", query);
        return ESP_OK;
    }

    /* accept upgrade: httpd performs the RFC6455 handshake on first recv */
    int sock = httpd_req_to_sockfd(req);
    if (sock < 0) {
        ESP_LOGW(TAG, "ws upgrade failed");
        return ESP_OK;
    }

    if (s_phone_connected) {
        /* second phone: handshake, tell it the screen is taken, then drop it */
        char dummy;
        httpd_ws_frame_t f = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)&dummy,
                              .len = sizeof(dummy)};
        httpd_ws_recv_frame(req, &f, 1);
        f.payload = (uint8_t *)"ERR_BUSY";
        f.len = 8;
        httpd_ws_send_frame(req, &f);
        f.type = HTTPD_WS_TYPE_CLOSE;
        httpd_ws_send_frame(req, &f);
        ESP_LOGW(TAG, "second phone rejected");
        return ESP_OK;
    }

    s_phone_connected = true;
    s_phone_sock = sock;
    ESP_LOGI(TAG, "phone connected (session %s)", s_session);
    notify(VKEY_EVT_PHONE_CONNECTED, NULL);

    /* recv loop */
    char rx_buf[CONFIG_VKEY_MAX_TEXT_LEN + 64];
    while (s_phone_sock == sock) {
        httpd_ws_frame_t f = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)rx_buf,
                              .len = sizeof(rx_buf) - 1};
        esp_err_t err = httpd_ws_recv_frame(req, &f, sizeof(rx_buf) - 1);
        if (err != ESP_OK) {
            break;
        }
        if (f.type == HTTPD_WS_TYPE_TEXT && f.len > 0) {
            rx_buf[f.len] = '\0';
            cJSON *root = cJSON_Parse(rx_buf);
            if (root) {
                const cJSON *t = cJSON_GetObjectItem(root, "t");
                if (t && cJSON_IsString(t) && strcmp(t->valuestring, "clear") == 0) {
                    notify(VKEY_EVT_CLEAR, NULL);
                } else if (t && cJSON_IsString(t)) {
                    const cJSON *d = cJSON_GetObjectItem(root, "d");
                    if (d && cJSON_IsString(d)) {
                        notify(VKEY_EVT_TEXT, d->valuestring);
                    }
                }
                cJSON_Delete(root);
            }
        } else if (f.type == HTTPD_WS_TYPE_CLOSE) {
            break;
        }
    }

    if (s_phone_sock == sock) {
        s_phone_sock = -1;
        s_phone_connected = false;
        ESP_LOGW(TAG, "phone disconnected");
        notify(VKEY_EVT_PHONE_DISCONNECTED, NULL);
    }
    return ESP_OK;
}

/* ---- static page ------------------------------------------------------ */
extern const uint8_t vkey_web_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t vkey_web_index_html_end[] asm("_binary_index_html_end");

static esp_err_t index_handler(httpd_req_t *req)
{
    const size_t len = vkey_web_index_html_end - vkey_web_index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)vkey_web_index_html_start, len);
    return ESP_OK;
}

/* ---- captive-portal catch-all: serve the controller page for any path -- */
static esp_err_t catchall_handler(httpd_req_t *req)
{
    return index_handler(req);
}

/* ----------------------------------------------------------------------- */
esp_err_t vkey_server_start(void)
{
    uint32_t r = esp_random();
    snprintf(s_session, sizeof(s_session), "%08x%08x", (unsigned)(r >> 32),
             (unsigned)(r & 0xFFFFFFFF));

    s_evt_queue = xQueueCreate(8, sizeof(vkey_event_t));

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = index_handler};
    httpd_uri_t uri_ws = {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler};
    httpd_uri_t uri_catch = {.uri = "/*", .method = HTTP_GET, .handler = catchall_handler};
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_ws);
    httpd_register_uri_handler(s_server, &uri_catch);

    const char *ip = vkey_net_ip();
    if (!ip) {
        ip = "192.168.4.1";
    }
    snprintf(s_pair_url, sizeof(s_pair_url), "http://%s/?s=%s", ip, s_session);
    ESP_LOGI(TAG, "pairing URL: %s", s_pair_url);

    return ESP_OK;
}

esp_err_t vkey_push_clear(void)
{
    if (!s_phone_connected || s_server == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)"{\"t\":\"clear\"}",
        .len = strlen("{\"t\":\"clear\"}"),
    };
    return httpd_ws_send_frame_async(s_server, s_phone_sock, &frame);
}
