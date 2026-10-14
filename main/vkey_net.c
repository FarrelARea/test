#include "vkey_net.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs_flash.h"

static const char *TAG = "vkey_net";

#define HOSTNAME "vkey"
#define AP_SSID_PREFIX "VKEY-"
#define AP_STA_MAX 4

static vkey_net_ready_cb_t s_ready_cb;
static vkey_net_mode_t s_mode = VKEY_NET_CONNECTING;
static char s_ip[16];
static bool s_got_ip;
static TimerHandle_t s_fallback_timer;

static void start_softap(void);
static void start_dns_server(void);

static void set_ip(const char *ip)
{
    snprintf(s_ip, sizeof(s_ip), "%s", ip);
}

const char *vkey_net_ip(void)
{
    return s_got_ip ? s_ip : NULL;
}

/* ---- minimal DNS responder: answers every A query with our IP ---------- */
static void dns_task(void *arg)
{
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket failed");
        vTaskDelete(NULL);
        return;
    }
    bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));

    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (len < 12) {
            continue;
        }
        /* QR=0 opcode=0 (query), one question, no answers: craft a reply. */
        if ((buf[2] & 0x80) != 0 || (buf[2] & 0x78) != 0) {
            continue; /* not a plain query */
        }
        uint16_t qdcount = (buf[4] << 8) | buf[5];
        if (qdcount != 1) {
            continue;
        }

        /* copy question, track where the query name ends */
        int qname_end = 12;
        while (qname_end < len && buf[qname_end] != 0) {
            qname_end += 1 + buf[qname_end];
        }
        qname_end += 5; /* null label + QTYPE + QCLASS */

        uint8_t *reply = buf;
        int rlen = 0;
        memcpy(reply + rlen, buf + 12, len - 12); /* echo question */
        rlen += len - 12;

        /* answer: pointer to question name, type A, class IN, ttl 300, IP */
        reply[rlen++] = 0xC0;
        reply[rlen++] = 12;
        reply[rlen++] = 0x00;
        reply[rlen++] = 0x01;
        reply[rlen++] = 0x00;
        reply[rlen++] = 0x01;
        reply[rlen++] = 0x00;
        reply[rlen++] = 0x00;
        reply[rlen++] = 0x01;
        reply[rlen++] = 0x2C;
        reply[rlen++] = 0x00;
        reply[rlen++] = 0x04;
        esp_netif_ip_info_t info;
        esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap && esp_netif_get_ip_info(ap, &info) == ESP_OK) {
            memcpy(reply + rlen, &info.ip.addr, 4);
        }
        rlen += 4;

        reply[2] = 0x81; /* response, recursion desired */
        reply[3] = 0x80;
        reply[6] = 0x00;
        reply[7] = 0x01; /* one answer */

        sendto(sock, reply, rlen, 0, (struct sockaddr *)&from, from_len);
    }
}

static void start_dns_server(void)
{
    xTaskCreate(dns_task, "vkey_dns", 4096, NULL, 5, NULL);
}

/* ---- soft-AP fallback -------------------------------------------------- */
static void start_softap(void)
{
    ESP_LOGI(TAG, "falling back to soft-AP");
    s_got_ip = false;

    uint32_t r = esp_random();
    char ssid[32];
    snprintf(ssid, sizeof(ssid), AP_SSID_PREFIX "%04X", (unsigned)(r & 0xFFFF));

    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = (uint8_t)strlen(ssid),
            .max_connection = AP_STA_MAX,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    memcpy(ap_config.ap.ssid, ssid, strlen(ssid));

    esp_netif_t *ap_if = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t info;
    esp_netif_get_ip_info(ap_if, &info);
    set_ip(ip4addr_ntoa(&info.ip));

    start_dns_server();
    ESP_LOGI(TAG, "AP '%s' IP %s (dns captive portal on)", ssid, s_ip);

    s_mode = VKEY_NET_SOFTAP;
    if (s_ready_cb) {
        s_ready_cb(s_mode, s_ip);
    }
}

/* ---- station mode ------------------------------------------------------ */
static void on_sta_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_netif_ip_info_t info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &info);
    set_ip(ip4addr_ntoa(&info.ip));
    ESP_LOGI(TAG, "station got IP %s", s_ip);
    s_got_ip = true;

    if (s_fallback_timer) {
        xTimerStop(s_fallback_timer, 0);
        xTimerDelete(s_fallback_timer, 0);
        s_fallback_timer = NULL;
    }

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(HOSTNAME));
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 Virtual Keyboard"));

    s_mode = VKEY_NET_STATION;
    if (s_ready_cb) {
        s_ready_cb(s_mode, s_ip);
    }
}

static void on_wifi_disconnected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    /* keep retrying station silently; AP fallback timer handles giving up */
    ESP_LOGW(TAG, "wifi disconnected, retrying");
    esp_wifi_connect();
}

static void fallback_timer_cb(TimerHandle_t timer)
{
    if (s_got_ip) {
        return;
    }
    ESP_LOGW(TAG, "no IP within timeout, switching to soft-AP");
    start_softap();
}

void vkey_net_start(vkey_net_ready_cb_t cb)
{
    s_ready_cb = cb;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    const char *ssid = CONFIG_VKEY_WIFI_SSID;
    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "no SSID configured, going straight to soft-AP");
        start_softap();
        return;
    }

    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    (void)sta;
    wifi_config_t sta_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg.capable = true,
            .pmf_cfg.required = false,
        },
    };
    strlcpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, CONFIG_VKEY_WIFI_PASSWORD,
            sizeof(sta_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               on_wifi_disconnected, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_sta_ip, NULL));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    s_fallback_timer = xTimerCreate("vkey_fb", pdMS_TO_TICKS(CONFIG_VKEY_AP_FALLBACK_TIMEOUT_MS),
                                    pdFALSE, NULL, fallback_timer_cb);
    xTimerStart(s_fallback_timer, 0);
}
