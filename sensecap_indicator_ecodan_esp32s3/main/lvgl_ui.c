#include "lvgl_ui.h"

#define SYMBOL_HEX_UTF8_FAUCET_DRIP "\xEE\x80\x86"
#define SYMBOL_HEX_UTF8_TEMP_UP "\xEE\x81\x80"
#define SYMBOL_HEX_UTF8_VIRUS_SLASH "\xEE\x81\xB5"
#define SYMBOL_HEX_UTF8_SUITCASE "\xEF\x83\xB2"
#define SYMBOL_HEX_UTF8_CIRCLE_PAUSE "\xEF\x8A\x8B"
#define SYMBOL_HEX_UTF8_SNOWFLAKE "\xEF\x8B\x9C"

// Symbols from Font Awesome
extern lv_font_t custom_font_heating;

// To send button clicks to main program radio
extern TaskHandle_t uiButtonTaskHandle;

// LVGL storage for UI pieces
lv_subject_t ui_clock_subj;

lv_subject_t ui_ed_temp_amb_subj;
lv_subject_t ui_ed_temp_set_subj;
lv_subject_t ui_ed_state_icon_subj;
lv_subject_t ui_ed_state_desc_subj;

lv_subject_t ui_sensor_rh_subj;
lv_subject_t ui_sensor_temp_subj;
lv_subject_t ui_sensor_voc_subj;
lv_subject_t ui_sensor_co2_subj;

char ui_clock_text[6];
char ui_ed_temp_amb_text[6];
char ui_ed_temp_set_text[6];
char ui_ed_state_icon_text[4];
char ui_ed_state_desc_text[20];

char ui_sensor_temp_text[6];
int32_t ui_sensor_rh_value;
int32_t ui_sensor_co2_value;
int32_t ui_sensor_voc_value;

ui_button_event_user_data_t ui_button_tempup_data = {
    .button = 1,
    .value = 1 // possible hold to increment, then send
};

ui_button_event_user_data_t ui_button_tempdown_data = {
    .button = 2,
    .value = -1 // possible hold to decrement, then send
};

ui_button_event_user_data_t ui_button_hotwater_data = {
    .button = 3,
    .value = 1 // Engaged / Disengaged
};

ui_button_event_user_data_t ui_button_holiday_data = {
    .button = 4,
    .value = 1 // Engaged / Disengaged
};

static void ui_button_event_callback(lv_event_t *event) {
    ui_button_event_user_data_t *user_data = lv_event_get_user_data(event);

    uint8_t button = user_data->button; // What did we click?
    int8_t value = user_data->value; // Up (+1), Down (-1) or value (+12)

    // Send the values over in 32-bit, casting int8_t to uint8_t
    xTaskNotify(uiButtonTaskHandle, button << 8 | (uint8_t)value, eSetValueWithoutOverwrite);
}

void ui_main(void) {  
    static lv_style_t style_statusbar;
    lv_style_init(&style_statusbar);
    lv_style_set_bg_color(&style_statusbar, lv_color_make(0x05, 0x2C, 0x5A));

    static lv_style_t style_panel;
    lv_style_init(&style_panel);
    lv_style_set_bg_color(&style_panel, lv_color_make(0x61, 0x61, 0x61));

    // Create container with grid
    static int32_t col_dsc[] = { 114, 64, 113, 113, LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = { 64, 64, 64, 64, 64, 64, LV_GRID_TEMPLATE_LAST};

    lv_obj_t * cont = lv_obj_create(lv_screen_active());
    lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);
    lv_obj_set_size(cont, 470, 470);
    lv_obj_center(cont);

    lv_obj_t * label;
    lv_obj_t * obj;

    /**
     * Top Bar
     */
    obj = lv_obj_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 0, 4,
                         LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_add_style(obj, &style_statusbar, 0);
    label = lv_label_create(obj);
    lv_subject_init_string(&ui_clock_subj, ui_clock_text, NULL, sizeof(ui_clock_text), "00:00");
    lv_label_bind_text(label, &ui_clock_subj, NULL);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);

    /**
     * Eco Dan
     */

    
    obj = lv_obj_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 1, 2);
    lv_obj_add_style(obj, &style_panel, 0);
    label = lv_label_create(obj);
    lv_label_set_text(label, "Target");
    label = lv_label_create(obj);
    lv_obj_center(label);
    lv_subject_init_string(&ui_ed_temp_set_subj, ui_ed_temp_set_text, NULL, sizeof(ui_ed_temp_set_text), "0.0");
    lv_label_bind_text(label, &ui_ed_temp_set_subj, NULL);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_34, 0);

    obj = lv_obj_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 0, 1,
                         LV_GRID_ALIGN_STRETCH, 3, 2);
    lv_obj_add_style(obj, &style_panel, 0);
    label = lv_label_create(obj);
    lv_label_set_text(label, "RC");
    label = lv_label_create(obj);
    lv_obj_center(label);
    lv_subject_init_string(&ui_ed_temp_amb_subj, ui_ed_temp_amb_text, NULL, sizeof(ui_ed_temp_amb_text), "0.0");
    lv_label_bind_text(label, &ui_ed_temp_amb_subj, NULL);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_34, 0);

    obj = lv_obj_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 0, 2,
                         LV_GRID_ALIGN_STRETCH, 5, 1);
    lv_obj_add_style(obj, &style_panel, 0);
    label = lv_label_create(obj);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_subject_init_string(&ui_ed_state_desc_subj, ui_ed_state_desc_text, NULL, sizeof(ui_ed_state_desc_text), "Idle");
    lv_label_bind_text(label, &ui_ed_state_desc_subj, NULL);
    label = lv_label_create(obj);
    lv_obj_set_style_text_font(label, &custom_font_heating, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(label, SYMBOL_HEX_UTF8_CIRCLE_PAUSE);
    lv_subject_init_string(&ui_ed_state_icon_subj, ui_ed_state_icon_text, NULL, sizeof(ui_ed_state_icon_text), SYMBOL_HEX_UTF8_CIRCLE_PAUSE);
    lv_label_bind_text(label, &ui_ed_state_icon_subj, NULL);

    /**
     * Sensors
     */

    
    obj = lv_obj_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 2, 1,
                         LV_GRID_ALIGN_STRETCH, 1, 2);
    lv_obj_add_style(obj, &style_panel, 0);
    label = lv_label_create(obj);
    lv_label_set_text(label, "Temp");
    label = lv_label_create(obj);
    lv_obj_center(label);
    lv_subject_init_string(&ui_sensor_temp_subj, ui_sensor_temp_text, NULL, sizeof(ui_sensor_temp_text), "0.0");
    lv_label_bind_text(label, &ui_sensor_temp_subj, NULL);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_34, 0);

    obj = lv_obj_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 3, 1,
                         LV_GRID_ALIGN_STRETCH, 1, 2);
    lv_obj_add_style(obj, &style_panel, 0);
    label = lv_label_create(obj);
    lv_label_set_text(label, "R.Hum");
    label = lv_label_create(obj);
    lv_obj_center(label);
    lv_subject_init_int(&ui_sensor_rh_subj, 50);
    lv_label_bind_text(label, &ui_sensor_rh_subj, "%d%%");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_34, 0);

    obj = lv_obj_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 2, 1,
                         LV_GRID_ALIGN_STRETCH, 3, 2);
    lv_obj_add_style(obj, &style_panel, 0);
    label = lv_label_create(obj);
    lv_label_set_text(label, "tVOC");
    label = lv_label_create(obj);
    lv_obj_center(label);
    lv_subject_init_int(&ui_sensor_voc_subj, 100);
    lv_label_bind_text(label, &ui_sensor_voc_subj, "%d");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_34, 0);

    obj = lv_obj_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 3, 1,
                         LV_GRID_ALIGN_STRETCH, 3, 2);
    lv_obj_add_style(obj, &style_panel, 0);
    label = lv_label_create(obj);
    lv_label_set_text(label, "CO2");
    label = lv_label_create(obj);
    lv_obj_center(label);
    lv_subject_init_int(&ui_sensor_co2_subj, 500);
    lv_label_bind_text(label, &ui_sensor_co2_subj, "%d");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_34, 0);

    /**
     * Buttons
     */
    obj = lv_button_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_STRETCH, 1, 1);
    label = lv_label_create(obj);
    lv_obj_center(label);
    lv_label_set_text(label, LV_SYMBOL_UP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_user_data(obj, &ui_button_tempup_data);
    lv_obj_add_event_cb(obj, ui_button_event_callback, LV_EVENT_CLICKED, &ui_button_tempup_data);

    obj = lv_button_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_STRETCH, 2, 1);
    label = lv_label_create(obj);
    lv_obj_center(label);
    lv_label_set_text(label, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_user_data(obj, &ui_button_tempdown_data);
    lv_obj_add_event_cb(obj, ui_button_event_callback, LV_EVENT_CLICKED, &ui_button_tempdown_data);

    obj = lv_button_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_STRETCH, 3, 1);
    label = lv_label_create(obj);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &custom_font_heating, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(label, SYMBOL_HEX_UTF8_FAUCET_DRIP);
    lv_obj_set_user_data(obj, &ui_button_hotwater_data);
    lv_obj_add_event_cb(obj, ui_button_event_callback, LV_EVENT_CLICKED, &ui_button_hotwater_data);

    obj = lv_button_create(cont);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_STRETCH, 4, 1);
    label = lv_label_create(obj);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &custom_font_heating, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(label, SYMBOL_HEX_UTF8_SUITCASE);
    lv_obj_set_user_data(obj, &ui_button_holiday_data);
    lv_obj_add_event_cb(obj, ui_button_event_callback, LV_EVENT_CLICKED, &ui_button_holiday_data);
}