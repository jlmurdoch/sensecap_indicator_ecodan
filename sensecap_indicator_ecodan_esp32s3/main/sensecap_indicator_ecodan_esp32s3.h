#ifndef SENSECAP_INDICATOR_ECODAN_ESP32S3_H
#define SENSECAP_INDICATOR_ECODAN_ESP32S3_H
#pragma once

/**
 * Standard Libraries
 */
#include <stdio.h>
// For usleep()
#include "unistd.h"
// For MAX & MIN
#include <sys/param.h>

// Delays, Tasks, etc
#include <freertos/FreeRTOS.h> // Needed before task.h
#include <freertos/task.h>

// For Wifi
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"

// For getaddrinfo (DNS lookup)
#include "lwip/netdb.h"
// For NTP
#include "esp_netif_sntp.h"

// I2C 
#include "driver/i2c.h"
// UART comms between ESP32-S3 and RP2040
#include "driver/uart.h"
// LCD Backlight
#include "driver/ledc.h"

// For LVGL Ticks
#include "esp_timer.h"

/**
 * ESP32 Managed Component headers
 */
// IO Expander (PCA9535PWR)
#include "esp_io_expander_tca95xx_16bit.h"
// LCD Display (SPI-based initialisation only)
#include "esp_lcd_panel_io_additions.h"
// LCD Display Rendering
#include "esp_lcd_panel_ops.h"
// LCD Display (ST7701S)
#include "esp_lcd_st7701.h"
// Touchscreen (FT6336U)
#include "esp_lcd_touch_ft5x06.h"
// LVGL GUI Engine
#include "lvgl.h"

/**
 * Project specific headers
 */
// SX1262 Radio 
#include "sx1262.h" 
// LVGL UI Design
#include "lvgl_ui.h"

// CPU core affinity
#define PRO_CPU 0
#define APP_CPU 1

/**
 * GPIO
 */

// RGB565
#define LCD_IO_RGB_R5           0
#define LCD_IO_RGB_R4           1
#define LCD_IO_RGB_R3           2
#define LCD_IO_RGB_R2           3
#define LCD_IO_RGB_R1           4
#define LCD_IO_RGB_R0           -1
#define LCD_IO_RGB_G5           5
#define LCD_IO_RGB_G4           6
#define LCD_IO_RGB_G3           7
#define LCD_IO_RGB_G2           8
#define LCD_IO_RGB_G1           9
#define LCD_IO_RGB_G0           10
#define LCD_IO_RGB_B5           11
#define LCD_IO_RGB_B4           12
#define LCD_IO_RGB_B3           13
#define LCD_IO_RGB_B2           14
#define LCD_IO_RGB_B1           15
#define LCD_IO_RGB_B0           -1
#define LCD_IO_RGB_DISP_EN      -1
#define LCD_IO_RGB_HSYNC        16
#define LCD_IO_RGB_VSYNC        17
#define LCD_IO_RGB_DE           18
#define LCD_IO_RGB_PCLK         21
// Backlight on LEDC
#define LCD_BACKLIGHT           45
// Comms from ESP32-S3 to RP2040
#define UART_TX                 19
#define UART_RX                 20
// Button (unused)
#define BUTTON                  38
// I2C for LCD Touch
#define I2C_SDA                 39
#define I2C_SCL                 40
// For LoRa BUSY/DIO1/TCXO + Touch (unused)
#define IOEXP_IRQ               42
// General SPI BUS
#define SPI_SCK                 41
#define SPI_MISO                47
#define SPI_MOSI                48

/**
 * GPIO Expander - values are 32-bit (unsigned long)
 */
#define IOEXP_PORT0_LORA_NSS    (1UL << 0) // Output
#define IOEXP_PORT0_LORA_RST    (1UL << 1) // Output
#define IOEXP_PORT0_LORA_BUSY   (1UL << 2) // Input - Read as needed
#define IOEXP_PORT0_LORA_DIO1   (1UL << 3) // Input - IRQ via GPIO 42

#define IOEXP_PORT0_LCD_NSS     (1UL << 4) // Output
#define IOEXP_PORT0_LCD_RST     (1UL << 5) // Output
#define IOEXP_PORT0_TOUCH_IRQ   (1UL << 6) // Output (forced) - Redundant with lvgl
#define IOEXP_PORT0_TOUCH_RST   (1UL << 7) // Output

#define IOEXP_PORT1_RP2040_RST  (1UL << 8)  // Output
#define IOEXP_PORT1_BMP_PWR     (1UL << 10) // N/C
#define IOEXP_PORT1_LORA_TXCO   (1UL << 11) // INPUT 

/** 
 * I2C Device Addresses
 */
#define I2C_ADDR_IOEXP          0x20
#define I2C_ADDR_TOUCH          0x48

/**
 * LCD Panel Attributes
 */
#define LCD_RES_H               480
#define LCD_RES_V               480

/**
 * SX1262 Definitions
 */
// SPI to use
#define SPI_HOST_ID SPI2_HOST

// Base frequency of radio for TX / RX
#define LORA_RFFREQ 868299000

// See 6.2.1 for modulation
#define LORA_BITRATE 9600
#define LORA_FREQDEV 4800

// Optimises the buffer and reception, as this example uses dynamic length
#define MAX_PACKET_SIZE 0x1B

// TX preamble in bits
#define PREAMBLE_TX_BITS 96

// RX preamble is predefined sizes - datasheet recommends 8bit or 16 bit
#define PREAMBLE_RX_BITS PREAMBLE_RX_16_BITS

// Two-byte syncword with some preamble padding (See 6.2.2.1 in Datasheet)
#define SYNCWORD 0x55, 0x55, 0x2D, 0xD4 

// Addresses
#define NODE_ADDRESS 0x8B 
#define BROADCAST_ADDRESS 0x0B

// CRC Calculation - CRC-16 UMTS
#define CRC_INIT 0x00, 0x00
#define CRC_POLY 0x80, 0x05

// WiFi definitions
#define WIFI_MAX_RETRY     5
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// Wifi connection signals this FreeRTOS event group
static EventGroupHandle_t s_wifi_event_group;

// Log tagging
static const char *TAG = "SenseCAP";

/**
 * LVGL Variables
 */
// LVGL is not thread safe, so we need a lock
extern _lock_t lvgl_ui_lock;

// FIFO circular buffer for datapoints
extern int16_t datapoints_temp[CHART_FIFO_SIZE];
extern int16_t datapoints_humid[CHART_FIFO_SIZE];
extern int16_t datapoints_voc[CHART_FIFO_SIZE];
extern int16_t datapoints_co2[CHART_FIFO_SIZE];

// LVGL UI subjects that can be updated
extern lv_subject_t ui_clock_subj;
extern lv_subject_t ui_ed_temp_amb_subj;
extern lv_subject_t ui_ed_temp_set_subj;
extern lv_subject_t ui_ed_state_icon_subj;
extern lv_subject_t ui_ed_state_desc_subj;
extern lv_subject_t ui_sensor_rh_subj;
extern lv_subject_t ui_sensor_temp_subj;
extern lv_subject_t ui_sensor_voc_subj;
extern lv_subject_t ui_sensor_co2_subj;

// Ecodan message
typedef struct {
    // Message Header
    uint8_t type;       // 0x44 - Set Command
    uint8_t unka;       // 0x04 - ??
    uint8_t unkb;       // 0x03 - ??
    uint8_t data_len;   // 0x10 - 16 bytes
    // Message Data
    uint8_t data[16];    // 0x22, 0xA6, 0xA5, 0x02, 0x06, 0x00, ...
} ecodan_msg_t;

// Ecodan packet
typedef struct {
    uint8_t gap;        // 0xFC - not part of CRC
    ecodan_msg_t msg;   // 
    uint8_t msg_crc;    // 0x30
} ecodan_pkt_t;

// Ecodan RC Frame
typedef struct {
    uint8_t id_msb;     // 0x7A - Address MSB
    uint8_t id_lsb;     // 0x77 - Address LSB
    uint8_t pkt_dst;    // 0x00 - Device 0 (FTC)
    uint8_t pkt_src;    // 0x06 - Device 6 (RC)
    uint8_t pkt_len;    // 0x16 - 22 bytes
    ecodan_pkt_t pkt; 
} ecodan_frame_t;

// Struct to contain all major sensecap IO handles of the sensecap
typedef struct {
    spi_device_handle_t spi;
    i2c_master_bus_handle_t i2c;
    esp_lcd_panel_handle_t panel;
    esp_lcd_touch_handle_t touch;
    esp_io_expander_handle_t ioexp;
} sensecap_io_handle_t;

/**< Sensecap Indicator ST7701S initialisation commands */
static const st7701_lcd_init_cmd_t sensecap_panel_init_cmds[] = {
    // BK 0
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0}, /// BK0 select
    {0xC0, (uint8_t []){0x3B, 0x00}, 2, 0}, // Display line setting
    {0xC1, (uint8_t []){0x0D, 0x02}, 2, 0}, // Porch vertical
    {0xC2, (uint8_t []){0x31, 0x05}, 2, 0}, // Inversion / Frame Rate
    {0xC7, (uint8_t []){0x04}, 1, 0}, // X direction
    {0xCD, (uint8_t []){0x08}, 1, 0}, // Color Control
    {0xB0, (uint8_t []){0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08, 0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18}, 16, 0}, // +V, gamma
    {0xB1, (uint8_t []){0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08, 0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18}, 16, 0}, // -V, gamma

    // BK 1
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0}, // BK1 Select
    {0xB0, (uint8_t []){0x60}, 1, 0}, // VRH
    {0xB1, (uint8_t []){0x32}, 1, 0}, // VCOM
    {0xB2, (uint8_t []){0x07}, 1, 0}, // Gate High Voltage
    {0xB3, (uint8_t []){0x80}, 1, 0}, // Test command
    {0xB5, (uint8_t []){0x49}, 1, 0}, // Gate Low Voltage
    {0xB7, (uint8_t []){0x85}, 1, 0}, // Power Control 1: Bias
    {0xB8, (uint8_t []){0x21}, 1, 0}, // Power Control 2: Voltage
    {0xC1, (uint8_t []){0x78}, 1, 0}, //  GND > VDD timing
    {0xC2, (uint8_t []){0x78}, 1, 100}, // VDD > 2*VDD timing
    {0xE0, (uint8_t []){0x00, 0x1B, 0x02}, 3, 0}, 
    {0xE1, (uint8_t []){0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t []){0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t []){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE4, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t []){0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0, 0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0}, 16, 0},
    {0xE6, (uint8_t []){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0xE7, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t []){0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0, 0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0}, 16, 0},
    {0xEB, (uint8_t []){0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40}, 7, 0},
    {0xEC, (uint8_t []){0x3C, 0x00}, 2, 0},
    {0xED, (uint8_t []){0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA}, 16, 0},
    {0x36, (uint8_t []){0x10}, 1, 0}, // Display data access control

    // BK 3
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0}, 
    {0xE5, (uint8_t []){0xE4}, 1, 0},

    // BK 0 + Disable BK of Command2
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x3A, (uint8_t []){0x60}, 1, 0}, // Interface Pixel Format (16/18/24)
    {0x21, (uint8_t []){0x00}, 0, 120}, // Display Inversion On
    {0x11, (uint8_t []){0x00}, 0, 120}, // Sleep Out

    {0x29, (uint8_t []){0x00}, 0, 0}, // Display On
};

#endif