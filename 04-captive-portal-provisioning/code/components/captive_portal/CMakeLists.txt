idf_component_register(
    SRCS "src/portal_config.c" "src/portal_ap.c" "src/portal_wifi.c" "src/portal_env.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES nvs_flash esp_wifi esp_http_server esp_netif lwip json
)
