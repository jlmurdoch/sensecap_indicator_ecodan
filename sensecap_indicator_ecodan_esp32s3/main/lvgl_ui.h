#ifndef LVGL_UI_H
#define LVGL_UI_H
#pragma once

#include "lvgl.h"
#include <stdio.h> // for printf()
#include <freertos/FreeRTOS.h> // Needed before task.h
#include <freertos/task.h>

typedef struct {
    uint8_t button;
    int8_t value;
} ui_button_event_user_data_t;

static void ui_button_event_callback(lv_event_t *event);

void ui_main(void);

#endif