#ifndef LVGL_UI_H
#define LVGL_UI_H
#pragma once

#include "lvgl.h"
#include <stdio.h> // for printf()
#include <freertos/FreeRTOS.h> // Needed before task.h
#include <freertos/task.h>
#include <sys/time.h>
#include "esp_timer.h"

#define CHART_FIFO_SIZE (86400 / 300)

extern _lock_t lvgl_ui_lock;

// Storage for button metadata in button callbacks
typedef struct {
    uint8_t button;
    int8_t value;
} ui_button_event_user_data_t;

// Storage for chart metadata in chart callbacks
typedef struct {
    int32_t y_min;
    int32_t y_max;
    void *data;
    const char title[18];
    const char *units[8];
} chart_metadata_t;

// FIFO circular buffer for datapoints
extern int16_t datapoints_temp[CHART_FIFO_SIZE];
extern int16_t datapoints_humid[CHART_FIFO_SIZE];
extern int16_t datapoints_voc[CHART_FIFO_SIZE];
extern int16_t datapoints_co2[CHART_FIFO_SIZE];

void ui_update_sensors(uint8_t buf[34]);

void ui_button_event_callback(lv_event_t *event);

void ui_start_clock(void);

void ui_main(void);

#endif