idf_component_register(SRCS "amap_weather_main.c"
                    PRIV_REQUIRES esp_event esp_http_client esp_netif esp_wifi json mbedtls nvs_flash)
