/**
 * SenseCAP Indicator - Ecodan Monitor - ESP32-S3
 * See README.md and sdkconfig.defaults
 */
#include "sensecap_indicator_ecodan_esp32s3.h"
// Secrets for Wifi, Heating ID, etc - use secrets.h.template to create your own
#include "secrets.h"

// Enumeration of Ecodan remote buttons
enum ecodan_actions { ECODAN_TEMPUP = 1, ECODAN_TEMPDOWN, ECODAN_HOTWATER, ECODAN_HOLIDAY }; 

// Task to process button clicks with SPI + I2C data
TaskHandle_t uiButtonTaskHandle = NULL;

TaskHandle_t edRxTaskHandle = NULL;
TaskHandle_t rp2040TaskHandle = NULL;

_lock_t lvgl_ui_lock;

// WiFi Retry counter
static int s_wifi_retry_num = 0;

// SX1262 Queue definitions 
static QueueHandle_t queueMsg;
volatile int queueLength = 0;

// Task handle to connect IO Expander ISR to IO Expander Task
TaskHandle_t ioExpanderInterruptTaskHandle = NULL;

// FIFO circular buffer for datapoints
int16_t datapoints_temp[CHART_FIFO_SIZE];
int16_t datapoints_humid[CHART_FIFO_SIZE];
int16_t datapoints_voc[CHART_FIFO_SIZE];
int16_t datapoints_co2[CHART_FIFO_SIZE];

// Temperatures to use if nothing available to TX
uint8_t ecodan_amb_temp = 0xA6;
uint8_t ecodan_set_temp = 0xA6;

// Store for all major SenseCAP IO handles
sensecap_io_handle_t sensecap_io = {
    .spi = NULL,
    .ioexp = NULL,
    .i2c = NULL,
    .touch = NULL,
    .panel = NULL
};

/**
 * @brief Non-ISR task to handle interrupts from the IO Expander
 */
void io_expander_isr_task(void *pvParameters) {
    sensecap_io_handle_t *sci_io = (sensecap_io_handle_t *) pvParameters;

    // State 
    uint32_t io_exp_state;
    uint32_t messagesToProcess;

    // Run forever
    for (;;) {
        // Delay for high-priority 
        vTaskDelay(10 / portTICK_PERIOD_MS);
        
        // Pick up notifies from io_expander_isr_handler()
        messagesToProcess = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // If we get a notify
        if (messagesToProcess) {
            // Check to see it's DIO1
            esp_io_expander_get_level(sci_io->ioexp, IOEXP_PORT0_LORA_DIO1, &io_exp_state);
            if ((io_exp_state & IOEXP_PORT0_LORA_DIO1) == IOEXP_PORT0_LORA_DIO1) {  
                // We have an interrupt, lets store it
                uint16_t irqstatus = getIRQStatus(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS);
                
                // Clear the status ASAP to free up DIO1
                clearIRQStatus(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS);

                // Check IRQ to see if RxDone (0x02) and not anything else (i.e. CRC Error (0x40))
                if ((irqstatus == IRQ_RXDONE)) {
                    // Get the data location (offset) and size
                    uint16_t rxbuf = getRxBufferStatus(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS);
                    uint8_t length = (uint8_t)((rxbuf >> 8) & 0xFF);
                    uint8_t offset = (uint8_t)(rxbuf & 0xFF);

                    // If the data is the right length
                    if(length == MAX_PACKET_SIZE) {
                        // Make space for the data
                        uint8_t *data = malloc(length);
                        // Read it off the buffer into the space
                        readBuffer(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, offset, length, data);
                        // Pop the data onto the queue
                        if(xQueueSend(queueMsg, data, 10) != pdPASS) {
                            printf("Unable to send SX1262 buffer\n");
                        }
                        // Increment the queue length
                        queueLength++;
                        // Free up the space used by the data
                        free(data);
                    }
                }
            }
            // Regardless if it's DIO, decrement
            messagesToProcess--;
        }
    }
}

/**
 * @brief Interrupt handler for IO Expander
 * @note SPI + IO Expander work is not ISR-safe, so hand off from ISR to task
 */
static void IRAM_ATTR io_expander_isr_handler(void* arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Notify a non-ISR task to do SPI-based FIFO collection safely
    vTaskNotifyGiveFromISR(ioExpanderInterruptTaskHandle, &xHigherPriorityTaskWoken);
} 

/**
 * @brief ESP IDF Wifi Event Handler
 * @note https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html
 */
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief ESP IDF Wifi Event Handler
 * @note https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html
 */
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            // .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            // .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 WIFI_SSID, WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 WIFI_SSID, WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

/**
 * @brief Callback from LVGL to panel driver: flush/render data to the panel
 */
static void lcd_panel_flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // pass the draw buffer to the driver
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map));
    if ((offsetx1 > offsetx2) || (offsety1 > offsety2)) {
        printf("%3d > %3d, %3d > %3d\n", offsetx1, offsetx2 + 1, offsety1,  offsety2 + 1);
    }
}

/**
 * @brief Callback from panel driver to LVGL: flush/render of data is complete
 */
static bool lcd_panel_flush_ready_callback(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx) {
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

/**
 * @brief Callback from LVGL to touch driver: get touch / coordinates
 */
static void lcd_touch_read_callback(lv_indev_t *indev, lv_indev_data_t *data) {
    uint8_t tp_points = 0;
    esp_lcd_touch_data_t tp_data;

    esp_lcd_touch_handle_t tp = lv_indev_get_user_data(indev);
    ESP_ERROR_CHECK(esp_lcd_touch_read_data(tp));
    ESP_ERROR_CHECK(esp_lcd_touch_get_data(tp, tp_data.coords, &tp_points, 1));

    // If touched and more than one point, set state as pressed
    if(tp_points > 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tp_data.coords->x;
        data->point.y = tp_data.coords->y;
        printf("Touch at %ld x %ld\n", data->point.x, data->point.y );
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/**
 * @brief Increment the LVGL tick timer
 */
static void lv_tick_task(void *arg) {
    (void) arg;

    lv_tick_inc(1);
}

/**
 * @brief Task to manage the execution of lv_timer_handler()
 */
static void manage_lv_timer_handler_task(void *arg)
{
    uint32_t task_delay_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_ui_lock);
        task_delay_ms = lv_timer_handler();
        _lock_release(&lvgl_ui_lock);

        // Max time is 500ms
        if (task_delay_ms > 500) {
            task_delay_ms = 500;
        // Min time is 1 or 10 depending on sdkconfig
        } else if (task_delay_ms < (1000 / CONFIG_FREERTOS_HZ)) {
            task_delay_ms = 1000 / CONFIG_FREERTOS_HZ;
        }

        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/**
 * @brief Control the LCD Backlight using PWM
 * @note This uses LEDC (LED Control) from ESP-IDF 
 */
void lcd_backlight_init(gpio_num_t bl_pin){
    // Standard LED ESP IDF 
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_13_BIT,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 5000, 
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = bl_pin,
        .duty           = 8192, // Set duty to 99%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // Turn on the backlight
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

/**
 * @brief Initialise the Sensecap Indicator Display Drivers (Panel + Touch)
 * @note This is a ST7701S Panel (SPI+RGB) with FT5x06 Touch device (I2C)
 * @note Need to use the IO Expander onboard
 */
void sensecap_indicator_lcd_drivers_init(sensecap_io_handle_t *sci_io) {
    /**
     * Touch Device IO
     */

    // Set up the low-level I2C device configuration
    esp_lcd_panel_io_i2c_config_t touch_io_config = {
        .dev_addr = I2C_ADDR_TOUCH, 
        .control_phase_bytes = 1, 
        .dc_bit_offset = 0, 
        .lcd_cmd_bits = 8, 
        .flags = { 
            .disable_control_phase = 1, 
        },
        .scl_speed_hz = 400 * 1000,
    };
    esp_lcd_panel_io_handle_t touch_io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(sci_io->i2c, &touch_io_config, &touch_io_handle));

    // Now add the I2C Touch Device
    esp_lcd_touch_config_t touch_config = {
        .x_max = LCD_RES_H,
        .y_max = LCD_RES_V,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 1, // Reversed on SenseCAP Indicator
            .mirror_y = 1, // Reversed on SenseCAP Indicator
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(touch_io_handle, &touch_config, &sci_io->touch));
    
    /**
     * Panel Device IO
     */
    // IO Expander needed to control panel NSS during SPI setup
    spi_line_config_t line_config = {
        .scl_io_type = IO_TYPE_GPIO,
        .scl_gpio_num = SPI_SCK,
        .sda_io_type = IO_TYPE_GPIO,
        .sda_gpio_num = SPI_MOSI,
        // IO Expander is used for NSS pin
        .cs_io_type = IO_TYPE_EXPANDER, 
        .cs_expander_pin = IOEXP_PORT0_LCD_NSS,
        .io_expander = sci_io->ioexp,
    };

    // Create new 3-wire SPI config for the panel (non-standard SPI)
    esp_lcd_panel_io_3wire_spi_config_t io_3wire_spi_config = {
        .line_config = line_config,
        .expect_clk_speed = PANEL_IO_3WIRE_SPI_CLK_MAX,
        .spi_mode = 0,
        .lcd_cmd_bytes = 1,
        .lcd_param_bytes = 1,
        .flags = {
            .use_dc_bit = 1,
            .dc_zero_on_data = 0,
            .lsb_first = 0,
            .cs_high_active = 0,
            .del_keep_cs_inactive = 1,
        },
    };
    esp_lcd_panel_io_handle_t panel_io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_3wire_spi(&io_3wire_spi_config, &panel_io_handle));

    // RGB Panel Properties (SCREEN_GX)
    esp_lcd_rgb_panel_config_t rgb_config = {
        .data_width = 16, // RGB565 Parallel, 5+6+5 = 16 bits
        .dma_burst_size = 64,
        .num_fbs = 2,
        .clk_src = LCD_CLK_SRC_PLL240M,
        .disp_gpio_num = LCD_IO_RGB_DISP_EN,
        .pclk_gpio_num = LCD_IO_RGB_PCLK,
        .vsync_gpio_num = LCD_IO_RGB_VSYNC,
        .hsync_gpio_num = LCD_IO_RGB_HSYNC,
        .de_gpio_num = LCD_IO_RGB_DE,
        .data_gpio_nums = {
            LCD_IO_RGB_B1,
            LCD_IO_RGB_B2,
            LCD_IO_RGB_B3,
            LCD_IO_RGB_B4,
            LCD_IO_RGB_B5,
            LCD_IO_RGB_G0,
            LCD_IO_RGB_G1,
            LCD_IO_RGB_G2,
            LCD_IO_RGB_G3,
            LCD_IO_RGB_G4,
            LCD_IO_RGB_G5,
            LCD_IO_RGB_R1,
            LCD_IO_RGB_R2,
            LCD_IO_RGB_R3,
            LCD_IO_RGB_R4,
            LCD_IO_RGB_R5,
        },
        .timings = {
            .pclk_hz = 18 * 1000 * 1000,
            .h_res = LCD_RES_H,
            .v_res = LCD_RES_V,
            .hsync_pulse_width = 8,
            .hsync_back_porch = 50,
            .hsync_front_porch = 10,
            .vsync_pulse_width = 8,
            .vsync_back_porch = 20,
            .vsync_front_porch = 10,
            .flags.pclk_active_neg = false,
        },
        .flags = {
            .fb_in_psram = true, // allocate framebuffer from PSRAM
        }
    };

    // Create a vendor-specific panel config subset
    st7701_vendor_config_t st7701_vendor_config = {
        .rgb_config = &rgb_config,
        .flags = { 
            .auto_del_panel_io = 1,
        },
        .init_cmds = sensecap_panel_init_cmds,
        .init_cmds_size = sizeof(sensecap_panel_init_cmds) / sizeof(st7701_lcd_init_cmd_t),
    };

    // Create a main panel config
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1, // Done via IO Expander
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &st7701_vendor_config,
    };
    
    // Create the panel
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7701(panel_io_handle, &panel_config, &sci_io->panel));
    // Initialise the panel - this will also turn it on, so we can delete the SPI IO
    ESP_ERROR_CHECK(esp_lcd_panel_init(sci_io->panel));
}

/**
 * @brief Initialise LVGL, panel, touchscreen
 * @param i2c_handle Handle for the I2C bus
 * @param io_expander Handle for an IO Expander
 * @return `lv_disp_t` - Handle for the display
 * @note Following the process here: https://docs.lvgl.io/master/integration/overview/connecting_lvgl.html
 */
static lv_disp_t *lcd_display_init(sensecap_io_handle_t *sci_io) {
    /**
     * 1. Initialise LVGL
     */
    _lock_acquire(&lvgl_ui_lock);
    lv_init();
    _lock_release(&lvgl_ui_lock);

    /**
     * 2. Initialise Panel & Touch Drivers
     */
    sensecap_indicator_lcd_drivers_init(sci_io);

    /**
     * 3. Tick Interface
     * @note - Only needs to be a 1ms resolution
     * @note - Easier to set up a periodic timer (Option 2) than use `lv_tick_set_cb` (Option 1)
     */
    esp_timer_handle_t periodic_timer;
    // Callback setup for tick timer
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "timer_lv_tick"
    };
    // Instantiate the tick timer
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    // Trigger tick increment every 1000us / 1ms
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000));

    /**
     * 4. Display Interface
     */
    // Double framebuffers
    void *fb1 = NULL;
    void *fb2 = NULL;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(sci_io->panel, 2, &fb1, &fb2));
    
    // Create display with size
    _lock_acquire(&lvgl_ui_lock);
    lv_display_t *display = lv_display_create(LCD_RES_H, LCD_RES_V);

    // Set display pixel format
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);

    // Set buffers for display
    lv_display_set_buffers(display, fb1, fb2, LCD_RES_H * LCD_RES_V * lv_color_format_get_size(lv_display_get_color_format(display)), LV_DISPLAY_RENDER_MODE_DIRECT);

    // Associate RGB panel with LVGL display for callback
    lv_display_set_user_data(display, sci_io->panel);

    // Create flush callback for lvgl
    lv_display_set_flush_cb(display, lcd_panel_flush_callback); 
    // Create flush callback for panel
    esp_lcd_rgb_panel_event_callbacks_t panel_callbacks = {
        .on_color_trans_done = lcd_panel_flush_ready_callback,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(sci_io->panel, &panel_callbacks, display));

    /**
     * 5. Input-Device Interface (indev)
     */
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display);
    lv_indev_set_user_data(indev, sci_io->touch);
    lv_indev_set_read_cb(indev, lcd_touch_read_callback);

    /**
     * 6. Manage LVGL timers
     */
    // High priority (2) task to call the LVGL timer in a controlled, thread-safe fashion on core 0
    xTaskCreatePinnedToCore(manage_lv_timer_handler_task, "LVGL Timer Handler Task", 10480, NULL, 2, NULL, PRO_CPU);

    /**
     * 7. Set a theme
     */
    lv_theme_t *theme = lv_theme_default_init(display, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_CYAN),
                                            LV_THEME_DEFAULT_DARK, LV_FONT_DEFAULT);

    lv_display_set_theme(display, theme); /* Assign theme to display */

    _lock_release(&lvgl_ui_lock);

    return display;
}

/**
 * @brief Simple CRC checker for the internal Ecodan payload, not the overall packet
 * @param buf data buffer to run CRC over
 * @param size length of data buffer
 * @return `uint8_t` checksum 
 */
uint8_t crc8(uint8_t *buf, uint8_t size) {
  uint8_t crc = 0;

  for (int x = 0; x < size; x++)
      crc -= buf[x]; // Keep subtracting and rolling over

  return crc;
}

void sx1262_sensecap_init(sensecap_io_handle_t *sci_io) {
    // Set up the SX1262 packet data
    uint8_t syncword[] = { SYNCWORD };
    uint8_t filter[] = { NODE_ADDRESS, BROADCAST_ADDRESS };
    uint8_t crcdata[] = { CRC_INIT, CRC_POLY };
    
    // Set the power boot for receving
    uint8_t rxgain[] = { RX_GAIN_POWER_BOOST };

    // Configure a generic SPI device
    spi_device_interface_config_t sx1262_cfg = {
        .mode = 0,
        .clock_speed_hz = SPI_MASTER_FREQ_11M,  // Can't use 16MHz - causes SPI issues regardless of DMA setting
        .spics_io_num = -1,                     // NSS / CS line -- TODO EXPANDER
        .queue_size = 1,                        // Mandatory to have queue size
        .command_bits = 8,                      // All commands are 8 bits in length
        .address_bits = 0,                      // Only used for read / write operations on registers and buffers
        .dummy_bits = 0,                        // Only used on write
        .flags = SPI_DEVICE_HALFDUPLEX          // The radio needs half-duplex for dummy bits and NOPs
    };

    // Attach the SX1262 to the SPI bus
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST_ID, &sx1262_cfg, &sci_io->spi));

    /************************************
     * RADIO RESET VIA IO EXPANDER
     ************************************/
    // Power-on - RST needs to be pulled-up!
    ESP_ERROR_CHECK(esp_io_expander_set_level(sci_io->ioexp, IOEXP_PORT0_LORA_RST, 1));
    
    // Wait for 100us
    usleep(100);
    
    // Pull down reset
    ESP_ERROR_CHECK(esp_io_expander_set_level(sci_io->ioexp, IOEXP_PORT0_LORA_RST, 0));

    // Wait for 100us (See 8.1)
    usleep(100);
    
    // Release reset
    ESP_ERROR_CHECK(esp_io_expander_set_level(sci_io->ioexp, IOEXP_PORT0_LORA_RST, 1));

    // Check the BUSY status now - it does take some time to be ready
    uint32_t pin_state = 0XFFFFFFFF; // Assume busy
    printf("Resetting SX1262.");
    while ((pin_state & IOEXP_PORT0_LORA_BUSY) != 0)
    {
        usleep(1);
        printf(".");
        ESP_ERROR_CHECK(esp_io_expander_get_level(sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, &pin_state));
    }
    printf(" Done\n");

    /************************************
     * RADIO CONFIGURATION
     * (Section 14.2 & 14.3 in datasheet)
     ************************************/
    // 0x80: SetStandby - Go to STDBY_RC for configuration
    setStandby(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, STDBY_RC);
    getStatus(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, true);

    // 0x07: ClearDeviceErrors, used to wipe clean issues caused by TCXO not being ready on cold start
    clearDeviceErrors(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS);

    // 0x02: Clear any IRQ flags from setup
    clearIRQStatus(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS);

    // 0x8A: SetPacketType is the mode 0=GFSK, 1=LoRa. Has to be the FIRST configuration command.
    setPacketType(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, PACKET_TYPE_GFSK);

    // 0x86: SetRfFrequency, used to set the frequency
    setRfFrequency(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, LORA_RFFREQ);

    // 0x95: (TX) SetPaConfig configure the power amplifier (See datasheet)
    setPaConfig(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, PA_SX1262_22DBM);
    
    // 0x8E: (TX) SetTxParams to set the power at 22dB at 40us ramp up
    setTxParams(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 22, SET_RAMP_40U);

    // 0x8F: SetBufferBaseAddress for pointer locations in FIFO for Tx and Rx
    setBufferBaseAddress(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 0x00, 0x00);

    // 0x8B: SetModulationParams to set the bitrate, bandwidth, shaping and frequency deviation.
    // Note: Must be executed some time after SetPacketType() but at some time before SetPacketParams()
    setModulationParams(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, LORA_BITRATE, PULSESHAPE_BT_1_0, RX_BW_11700, LORA_FREQDEV);

    // 0x8C: SetPacketParams, set all the options for the packets incoming / outgoing. 
    // Note: Must be executed at some point after SetPacketParams(), not before
    setPacketParams(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, PREAMBLE_TX_BITS, PREAMBLE_RX_BITS, sizeof(syncword) * 8, ADDRESS_FILTER_NODE_BROADCAST, LENGTH_VARIABLE, MAX_PACKET_SIZE, CRC_2_BYTE, WHITENING_OFF);

     // 0x0D: WriteRegister for CRC, SyncWord, Node/Broadcast Address and RX gain
    writeRegister(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 0x06BC, sizeof(crcdata), crcdata);
    writeRegister(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 0x06C0, sizeof(syncword), syncword);
    writeRegister(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 0x06CD, sizeof(filter), filter);
    writeRegister(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 0x08AC, sizeof(rxgain), rxgain);

    // 0x1D: (TX) ReadRegister: Look at Over-Current Protection (OCP) to see if it has changed for SX1262
    uint8_t ocpvalue;
    readRegister(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 0x08E7, sizeof(ocpvalue), &ocpvalue);
    printf("Register: [0x08E7: OCP Configuration]: 0x%x (%0.f mA)\n\n", ocpvalue, ocpvalue * 2.5);

    // 0x08: SetDioIrqParams for Timeouts, CRC Errors and RXDone/TXDone for IRQ, with just CRC Error | RXDONE for DIO1, but not DIO2 or DIO3
    setDioIrqParams(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, IRQ_TIMEOUT | IRQ_CRCERR | IRQ_RXDONE | IRQ_TXDONE, IRQ_CRCERR | IRQ_RXDONE, 0, 0);

    /************************************
     * Seeed Studio Wio-SX1262 settings
     ************************************/
    // 0x9D: SetDIO2AsRfSwitchCtrl as true means DIO2 controls the RF switch for TX, RX, etc
    setDio2AsRfSwitchCtrl(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, true);

    // 0x97: SetDIO3AsTCXOCtrl controls the TCXO. Set to @ 2.4V (0x06) for SenseCap Indicator, 5ms (320 ticks) delay
    setDio3AsTCXOCtrl(sci_io->spi, sci_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, TCXOVOLTAGE_2_4V, 320);
}

void sx1262_rx(sensecap_io_handle_t sci_io) {
    // 0x02: Clear any IRQ flags
    clearIRQStatus(sci_io.spi, sci_io.ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS);

    // 0x82: SetRx in continuous mode (0xFFFFFF)
    setRx(sci_io.spi, sci_io.ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 0xFFFFFF);
    getStatus(sci_io.spi, sci_io.ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, true);
}

/**
 * @brief Transmit requisite radio data on a button press
 */
static void ecodan_tx_event_task(void *pvParameters) {

    sensecap_io_handle_t *sensecap_io = (sensecap_io_handle_t *) pvParameters;
    uint32_t buttonContext;

    // Run forever
    for (;;) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        // If we get a notify
        if (xTaskNotifyWait(0, ULONG_MAX, &buttonContext, 10) == pdPASS) {
            printf("Button %d: %d\n", (uint8_t)((buttonContext >> 8) & 0xFF),  (int8_t)buttonContext & 0xFF);
            ecodan_msg_t msg = { 0 };
            ecodan_frame_t action_frame = {
                .id_msb = ECODAN_RC_ID_MSB,
                .id_lsb = ECODAN_RC_ID_LSB,
                .pkt_dst = 0,
                .pkt_src = 6,
                .pkt_len = sizeof(ecodan_pkt_t),
                .pkt = {
                    .gap = 0xFC,
                    .msg = msg,
                    .msg_crc = 0x00
                }
            };

            switch (buttonContext >> 8)
            {
            /**
             * Temperature Set
             * 44 04 03 10 22 A8 A7 02 06 - Up 
             * 44 04 03 10 22 A6 A7 02 06 - Down
             * ^^          ^^ ^^ ^^
             */
            case ECODAN_TEMPUP:
                printf("Temp Up Button - ");
                action_frame.pkt.msg.type = 0x44; // Temp Change
                action_frame.pkt.msg.unka = 0x04;
                action_frame.pkt.msg.unkb = 0x03;
                action_frame.pkt.msg.data_len = sizeof(action_frame.pkt.msg.data);
                action_frame.pkt.msg.data[0] = 0x22; // Temp Change
                action_frame.pkt.msg.data[1] = ++ecodan_set_temp; // 
                action_frame.pkt.msg.data[2] = ecodan_amb_temp; // Current temperature from RC
                action_frame.pkt.msg.data[3] = 0x02;
                action_frame.pkt.msg.data[4] = 0x06;
                action_frame.pkt.msg_crc = crc8((uint8_t *)&action_frame.pkt.msg, sizeof(action_frame.pkt.msg));
                break;
            case ECODAN_TEMPDOWN:
                printf("Temp Down Button - ");
                action_frame.pkt.msg.type = 0x44; // Temp Change
                action_frame.pkt.msg.unka = 0x04;
                action_frame.pkt.msg.unkb = 0x03;
                action_frame.pkt.msg.data_len = sizeof(action_frame.pkt.msg.data);
                action_frame.pkt.msg.data[0] = 0x22; // Temp Change
                action_frame.pkt.msg.data[1] = --ecodan_set_temp; // 
                action_frame.pkt.msg.data[2] = ecodan_amb_temp; // Current temperature from RC
                action_frame.pkt.msg.data[3] = 0x02;
                action_frame.pkt.msg.data[4] = 0x06;
                action_frame.pkt.msg_crc = crc8((uint8_t *)&action_frame.pkt.msg, sizeof(action_frame.pkt.msg));
                break;
            /**
             * Hot Water Boost
             * 45 04 03 10 24 01 06
             * ^^          ^^ ^^
             */
            case ECODAN_HOTWATER:
                printf("Hot Water Button - ");
                action_frame.pkt.msg.type = 0x45; // Hot Water
                action_frame.pkt.msg.unka = 0x04;
                action_frame.pkt.msg.unkb = 0x03;
                action_frame.pkt.msg.data_len = sizeof(action_frame.pkt.msg.data);
                action_frame.pkt.msg.data[0] = 0x24; // Hot Water
                action_frame.pkt.msg.data[1] = 0x01; // Set on
                action_frame.pkt.msg.data[2] = 0x06;
                action_frame.pkt.msg_crc = crc8((uint8_t *)&action_frame.pkt.msg, sizeof(action_frame.pkt.msg));
                break;
            /**
             * Holiday Mode 
             * 46 04 03 10 25 01 03 06 <-- 01 hours = 01 + 02 (* 30 mins)
             * 46 04 03 10 25 01 15 06 <-- 10 hours = 01 + 20 (* 30 mins)
             * 46 04 03 10 25 00 01 06 <-- Cancel
             * ^^          ^^ ^^ ^^   
             */
            case ECODAN_HOLIDAY:
                printf("Holiday Button - ");
                action_frame.pkt.msg.type = 0x46; // Holiday
                action_frame.pkt.msg.unka = 0x04;
                action_frame.pkt.msg.unkb = 0x03;
                action_frame.pkt.msg.data_len = sizeof(action_frame.pkt.msg.data);
                action_frame.pkt.msg.data[0] = 0x25; // Holiday
                action_frame.pkt.msg.data[1] = 0x01; // Set on
                action_frame.pkt.msg.data[2] = 0x03; // 01 hours = 01 + 02 (* 30 mins)
                action_frame.pkt.msg.data[3] = 0x06;
                action_frame.pkt.msg_crc = crc8((uint8_t *)&action_frame.pkt.msg, sizeof(action_frame.pkt.msg));
                break;
            
            default:
                printf("Unknown Button - ");
                continue;
            }

            for (int x = 0; x < sizeof(action_frame); x++) {
                printf(" %02x", ((uint8_t *)&action_frame)[x]);
            }
            printf("\n");

            // 0x0E: (TX) WriteBuffer at 0x00 with entire frame
            writeBuffer(sensecap_io->spi, sensecap_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 0x00, sizeof(action_frame), (uint8_t *)&action_frame);

            // 0x02: Clear any IRQ flags
            clearIRQStatus(sensecap_io->spi, sensecap_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS);

            // 0x83: SetTx to send one packet and stop (0x000000)
            setTx(sensecap_io->spi, sensecap_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 0x0FFFFF);
            getStatus(sensecap_io->spi, sensecap_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, true);

            // Wait until Tx mode is exited
            while ((getStatus(sensecap_io->spi, sensecap_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, false) & 0x70) == STATUS_TX) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
            getStatus(sensecap_io->spi, sensecap_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, true);

            // 0x12: Get the IRQ status to see if either Timeout (0x0200) or TXDone (0x0001) 
            uint16_t irqstatus = getIRQStatus(sensecap_io->spi, sensecap_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS);
            if (irqstatus & IRQ_TXDONE) {
                printf("Tx Done\n");
            } else {
                printf("Tx Timeout\n");
            }

            // 0x02: Clear any IRQ flags
            clearIRQStatus(sensecap_io->spi, sensecap_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS);

            // 0x82: SetRx in continuous mode (0xFFFFFF)
            setRx(sensecap_io->spi, sensecap_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, 0xFFFFFF);
            getStatus(sensecap_io->spi, sensecap_io->ioexp, IOEXP_PORT0_LORA_BUSY, IOEXP_PORT0_LORA_NSS, true);
        }
    }
}

uint8_t ecodan_validate (ecodan_frame_t frame) {   
    // Check to see if frame CRC is bad
    if (frame.pkt.msg_crc != crc8((uint8_t *)&frame.pkt.msg, sizeof(frame.pkt.msg))) {
        return 0;
    }

    // Print the frame for discovery needs
    for (int x=0; x < MAX_PACKET_SIZE; x++) {
        printf("%02x ", ((uint8_t *)&frame)[x]);
    }
    printf("\n");

    // Check to see if the frame is in scope
    if (((frame.id_msb == ECODAN_FTC_ID_MSB) && (frame.id_lsb == ECODAN_FTC_ID_LSB)) ||
        ((frame.id_msb == ECODAN_RC_ID_MSB) && (frame.id_lsb == ECODAN_RC_ID_LSB))) {
        return 1;
    }

    return 0;
}

void ecodan_process(ecodan_frame_t frame) {
    char buf[6];
    _lock_acquire(&lvgl_ui_lock);
    switch (frame.pkt.msg.type)
    {
    case 0x43:
        // Record the Ambient Temp for TX usage
        ecodan_amb_temp = frame.pkt.msg.data[1];
        
        // Update the LVGL Ambient Temp label
        lv_snprintf(buf, sizeof(buf), "%d.%d", (frame.pkt.msg.data[1] - 128) / 2, (frame.pkt.msg.data[1] % 2) * 5);
        lv_subject_copy_string(&ui_ed_temp_amb_subj, buf);
        
        break;
    /**
     * Ecodan FTC Reporting Set Temperature and Functional State
     * 63 04 03 10 -- Standard reporting in
     * 00 - Standard reporting in
     * 01 - on?
     * 02 - state
     * a6 - set temp
     * 02 - 0) normal 1) other 2) comp. curve
     * 00
     * 01 - Holiday mode
     * 05 - ? - seen 0x04 elsewhere
     * 03 - Holiday length
     * 06 - ? - Zone / RC Control ?
     * 06 - Zone / RC Control ?
     * ff - ? - seen 0xfd elsewhere
     * 00 
     * bd 
     * 00 
     * 00
     */ 
    case 0x63:
        // Record the Set Temp for TX usage
        ecodan_set_temp = frame.pkt.msg.data[3];
        // Update the LVGL Set Temp label
        lv_snprintf(buf, sizeof(buf), "%d.%d", (frame.pkt.msg.data[3] - 128) / 2, (frame.pkt.msg.data[3] % 2) * 5);
        lv_subject_copy_string(&ui_ed_temp_set_subj, buf);
        // State reporting of heating
        switch (frame.pkt.msg.data[2])
        {
        case 0x01: // Hot water?
            lv_subject_copy_string(&ui_ed_state_icon_subj, "\xEE\x80\x86");
            lv_subject_copy_string(&ui_ed_state_desc_subj, "Hot Water");
            break;
        
        case 0x02: // Heating
            lv_subject_copy_string(&ui_ed_state_icon_subj, "\xEE\x81\x80");
            lv_subject_copy_string(&ui_ed_state_desc_subj, "Heating");
            break;

        case 0x05: // Ice protect
            lv_subject_copy_string(&ui_ed_state_icon_subj, "\xEF\x8B\x9C");
            lv_subject_copy_string(&ui_ed_state_desc_subj, "Anti-Ice");
            break;

        case 0x06: // Ice protect
            lv_subject_copy_string(&ui_ed_state_icon_subj, "\xEF\x8B\x9C");
            lv_subject_copy_string(&ui_ed_state_desc_subj, "Legionella");
            break;

        default: // Idle
            lv_subject_copy_string(&ui_ed_state_icon_subj, "\xEF\x8A\x8B");
            lv_subject_copy_string(&ui_ed_state_desc_subj, "Idle");
            break;
        }
        break;  
    default:
        break;
    }
    _lock_release(&lvgl_ui_lock);
}

void uart_init(void) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    uart_set_pin(UART_NUM_2, UART_TX, UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void ecodan_rx_queue_task(void *pvParameters) {
    // Data structure for a received transmission
    ecodan_frame_t frame;
    
    for (;;) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        if (queueLength > 0) {
            // Collect data off the queue
            xQueueReceive(queueMsg, (uint8_t *)&frame, portMAX_DELAY);
            // Decrement the counter
            queueLength--;
            // Validate and process any received frames
            if (ecodan_validate(frame)) {
                ecodan_process(frame);
            } 
        }
    }
}

void rp2040_buffer_task(void *pvParameters) {
    uint8_t uart_buf[34];
    int uart_len;
    
    for (;;) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        ESP_ERROR_CHECK_WITHOUT_ABORT(uart_get_buffered_data_len(UART_NUM_2, (size_t*)&uart_len));
        if (uart_len == 34) {
            // Read in the UART bytes
            uart_read_bytes(UART_NUM_2, uart_buf, uart_len, 100 / portTICK_PERIOD_MS);
            ui_update_sensors(uart_buf);
        } else if (uart_len > 34) {
            // Flush out any UART noise
            uart_flush(UART_NUM_2);
        }
    }
}

void i2c_init(sensecap_io_handle_t *sci_io) {
    const i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL,
        .sda_io_num = I2C_SDA,
    };
    i2c_new_master_bus(&i2c_bus_config, &sci_io->i2c);
}

void wifi_init(void) {
    /************************************
     * WiFi Setup
     ************************************/
    // NVS flash is used when using wifi_init_sta()
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialise WiFi
    wifi_init_sta();
}

void ioexp_init(sensecap_io_handle_t *sci_io) {
    // Initialise the IO Expander
    ESP_ERROR_CHECK(esp_io_expander_new_i2c_tca95xx_16bit(sci_io->i2c, I2C_ADDR_IOEXP, &sci_io->ioexp));

    // Turn on the LoRa, LCD, Touch
    ESP_ERROR_CHECK(esp_io_expander_set_dir(sci_io->ioexp, IOEXP_PORT0_LORA_NSS | IOEXP_PORT0_LORA_RST | IOEXP_PORT0_LCD_RST | IOEXP_PORT0_TOUCH_RST | IOEXP_PORT1_RP2040_RST |  IOEXP_PORT1_BMP_PWR | IOEXP_PORT0_TOUCH_IRQ, IO_EXPANDER_OUTPUT));
    ESP_ERROR_CHECK(esp_io_expander_set_level(sci_io->ioexp, IOEXP_PORT0_LORA_NSS | IOEXP_PORT0_LORA_RST | IOEXP_PORT0_LCD_RST | IOEXP_PORT0_TOUCH_RST | IOEXP_PORT1_RP2040_RST | IOEXP_PORT1_BMP_PWR, 1));

    // LoRa - SPI BUSY Indicator
    ESP_ERROR_CHECK(esp_io_expander_set_dir(sci_io->ioexp, IOEXP_PORT0_LORA_BUSY | IOEXP_PORT0_LORA_DIO1 | IOEXP_PORT1_LORA_TXCO, IO_EXPANDER_INPUT));

    // GPIO setup for mapping IOEXP_PORT0_LORA_DIO1 to IOEXP_IRQ
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << IOEXP_IRQ), // Interrupt from IO Expander
        .intr_type = GPIO_INTR_NEGEDGE, // Negative Edge, as IO Expander IRQ is pulled up / open-drain
        .mode = GPIO_MODE_INPUT, // IO Expander IRQ is an input
        .pull_down_en = 0, // No pull down
        .pull_up_en = 1 // Pull up
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

void spi_init(void) {
    // Main SPI bus setup now LCD work is complete
    spi_bus_config_t spi_bus_cfg = {
        .miso_io_num = SPI_MISO,
        .mosi_io_num = SPI_MOSI,
        .sclk_io_num = SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_ID, &spi_bus_cfg, SPI_DMA_CH_AUTO));
}

void ntp_init(void) {
    // Set up SNTP
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(30000)) != ESP_OK) {
        printf("No SNTP within 30 secs\n");
    }
}

void app_main(void)
{
    vTaskDelay(10000 / portTICK_PERIOD_MS);

    // UART - Comms to RP2040
    uart_init();
    // I2C Bus - IO Expander + Touch Screen
    i2c_init(&sensecap_io);
    // IO Expander - LCD, Touch, SX1262, RP2040
    ioexp_init(&sensecap_io);
    // LCD Display - has to be done before any other SPI device
    lcd_display_init(&sensecap_io);
    lcd_backlight_init(LCD_BACKLIGHT);

    // Create clean SPI for SX1262 
    spi_init();
    // SX1262 - Has specific needs: IO Expander GPIO, TXCO settings
    sx1262_sensecap_init(&sensecap_io);
    // SX1262 - RX message queue
    queueMsg = xQueueCreate(10, MAX_PACKET_SIZE);

    // Wireless
    wifi_init();
    // NTP for UI clock
    ntp_init();
 
    // UI Initialisation
    _lock_acquire(&lvgl_ui_lock);
    ui_main();
    _lock_release(&lvgl_ui_lock);

    /************************************
     * Task Setup 
     * Bind these to the second core
     * - HIGH: Interrupts from IO Expander
     * - HIGH: TX Events (button pushes)
     * - LOW: RX Queue (from Ecodan)
     * - LOW: UART Comms (from RP2040)
     ************************************/
    // Handling RX event interrupts via IO Expander
    xTaskCreatePinnedToCore(io_expander_isr_task, "IOExp Int Task", 2048, &sensecap_io, 2, &ioExpanderInterruptTaskHandle, APP_CPU);

    // Handling TX event callbacks via UI
    xTaskCreatePinnedToCore(ecodan_tx_event_task, "EDTX Event Task", 2048, &sensecap_io, 2, &uiButtonTaskHandle, APP_CPU);

    // Handling RX queue of data
    xTaskCreatePinnedToCore(ecodan_rx_queue_task, "EDRX Queue Task", 2048, NULL, tskIDLE_PRIORITY, &edRxTaskHandle, APP_CPU);

    // UART events that impact display
    xTaskCreatePinnedToCore(rp2040_buffer_task, "UART Comms Task", 3072, NULL, tskIDLE_PRIORITY, &rp2040TaskHandle, APP_CPU);

    /************************************
     * IO Expander ISR Setup
     ************************************/
    // Set up interrupt service
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // ISR Handler for all IO Expander Interrupts
    ESP_ERROR_CHECK(gpio_isr_handler_add(IOEXP_IRQ, io_expander_isr_handler, NULL));

    /************************************
     * SX1262 Rx Mode - Default
     ************************************/
    sx1262_rx(sensecap_io);
}
