idf_component_register(
    SRCS "main.c" "vkey_net.c" "vkey_server.c" "vkey_display.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/index.html"
    REQUIRES esp_wifi esp_netif esp_event esp_http_server aw9523 mbedtls mdns esp_timer
             qmsd_board json nvs_flash
)
