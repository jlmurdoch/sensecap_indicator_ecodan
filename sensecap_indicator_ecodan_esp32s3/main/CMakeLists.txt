idf_component_register(SRCS "custom_font_heating.c" "sensecap_indicator_ecodan_esp32s3.c" "lvgl_ui.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_uart esp_wifi nvs_flash esp_lcd esp_driver_i2c esp_driver_ledc esp_timer sx1262)
