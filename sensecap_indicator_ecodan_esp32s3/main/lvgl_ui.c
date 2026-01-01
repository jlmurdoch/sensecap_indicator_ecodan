#include "lvgl_ui.h"

#define SYMBOL_HEX_UTF8_FAUCET_DRIP "\xEE\x80\x86"
#define SYMBOL_HEX_UTF8_TEMP_UP "\xEE\x81\x80"
#define SYMBOL_HEX_UTF8_VIRUS_SLASH "\xEE\x81\xB5"
#define SYMBOL_HEX_UTF8_SUITCASE "\xEF\x83\xB2"
#define SYMBOL_HEX_UTF8_CIRCLE_PAUSE "\xEF\x8A\x8B"
#define SYMBOL_HEX_UTF8_SNOWFLAKE "\xEF\x8B\x9C"

// Chart Popup attributes
#define POPUP_X 0
#define POPUP_Y 0
#define POPUP_WIDTH 300
#define POPUP_HEIGHT 200

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

chart_metadata_t chart_meta[] = {
    { 
        // Scaled up x10 - +45C to -15C
        -150,
        450,
        datapoints_temp,
        "Temperature (°C)",
        { "-15", "0", "15", "30", "45", NULL },
    },
    { 
        // Scaled up x10
        0,
        1000,
        datapoints_humid,
        "Rel. Humidity (%)",
        { "0", "25", "50", "75", "100", NULL },
    },
    { 
        0,
        500,
        datapoints_voc,
        "VOC Index",
        { "0", "100", "200", "300", "400", "500", NULL },
    },
    { 
        0,
        5000,
        datapoints_co2,
        "CO2 (ppm)",
        { "0", "1K", "2K", "3K", "4K", "5K", NULL },
    }
};

void ui_button_event_callback(lv_event_t *event) {
    ui_button_event_user_data_t *user_data = lv_event_get_user_data(event);

    uint8_t button = user_data->button; // What did we click?
    int8_t value = user_data->value; // Up (+1), Down (-1) or value (+12)

    // Send the values over in 32-bit, casting int8_t to uint8_t
    xTaskNotify(uiButtonTaskHandle, button << 8 | (uint8_t)value, eSetValueWithoutOverwrite);
}

void ui_chart_event_callback(lv_event_t *event) {
    // Define what chart format / data
    chart_metadata_t *user_data = lv_event_get_user_data(event);

    // Style for the scales
    static lv_style_t indicator_style;
    lv_style_init(&indicator_style);
    lv_style_set_text_font(&indicator_style, &lv_font_montserrat_18);

    // Popup
    lv_obj_t *main_cont = lv_obj_create(lv_screen_active());
    lv_obj_set_size(main_cont, POPUP_WIDTH, POPUP_HEIGHT);
    lv_obj_center(main_cont);
    // Make chart appear for 10secs
    lv_obj_delete_delayed(main_cont, 10000);

    // Container for the chart and axis
    lv_obj_t *wrapper = lv_obj_create(main_cont);
    lv_obj_remove_style_all(wrapper); // remove all other stuff - just make it a container
    lv_obj_set_size(wrapper, lv_pct(100), lv_pct(100)); // Wrapper is all of the box

    /*
     * Chart itself, without X & Y axis
     */
    lv_obj_t *chart = lv_chart_create(wrapper);
    lv_obj_set_style_radius(chart, 0, 0);
    lv_obj_set_pos(chart, POPUP_X + 30, POPUP_Y);
    lv_obj_set_size(chart, POPUP_WIDTH - 65, POPUP_HEIGHT - 50); 
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE); // Lines and data points
    lv_chart_set_point_count(chart, CHART_FIFO_SIZE);

    /*
     * Scale along the bottom
     */
    lv_obj_t * scale_bottom = lv_scale_create(wrapper);
    lv_obj_set_pos(scale_bottom, POPUP_X + 40, POPUP_HEIGHT - 60); 
    lv_obj_set_size(scale_bottom, POPUP_WIDTH - 85, 30);
    lv_obj_add_style(scale_bottom, &indicator_style, LV_PART_INDICATOR);
    lv_scale_set_mode(scale_bottom, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
    lv_scale_set_total_tick_count(scale_bottom, 13);
    lv_scale_set_major_tick_every(scale_bottom, 2);
    static const char *hour[] = {"0", "4", "8", "12", "16", "20", "24", NULL};
    lv_scale_set_text_src(scale_bottom, hour);
    
    /*
     * Scale on the left-hand side
     * We change this for the different FIFOs
     */
    lv_obj_t *scale_left = lv_scale_create(wrapper);
    lv_obj_set_pos(scale_left, POPUP_X + 5, POPUP_Y + 11);
    lv_obj_set_size(scale_left, 35, POPUP_HEIGHT - 70);
    lv_obj_add_style(scale_left, &indicator_style, LV_PART_INDICATOR);
    lv_scale_set_mode(scale_left, LV_SCALE_MODE_VERTICAL_LEFT);
    lv_scale_set_total_tick_count(scale_left, 5);
    lv_scale_set_major_tick_every(scale_left, 1);
    lv_obj_set_style_pad_ver(scale_left, 0, 0);

    // Create a datapoint series
    lv_chart_series_t *series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
    // Units for left scale
    lv_scale_set_text_src(scale_left, user_data->units);
    // Adjust the chart scale to suit dataset
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, user_data->y_min, user_data->y_max);

    // Draw the line series
    for (int x = 0; x < CHART_FIFO_SIZE; x++) {
        lv_chart_set_next_value(chart, series, ((int16_t *)(user_data->data))[x]);
    }

    // Required after direct set
    lv_chart_refresh(chart);

    // Title for the chart
    lv_obj_t *title = lv_label_create(wrapper);
    lv_obj_set_align(title, LV_ALIGN_TOP_MID);
    lv_label_set_text(title, user_data->title);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
}

void ui_main(void) {  
    // Style for top bar
    static lv_style_t style_statusbar;
    lv_style_init(&style_statusbar);
    lv_style_set_bg_color(&style_statusbar, lv_color_make(0x05, 0x2C, 0x5A));

    // Style for each panel
    static lv_style_t style_panel;
    lv_style_init(&style_panel);
    lv_style_set_bg_color(&style_panel, lv_color_make(0x61, 0x61, 0x61));

    // Create container with grid
    static int32_t col_dsc[] = { 114, 64, 113, 113, LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = { 64, 64, 64, 64, 64, 64, LV_GRID_TEMPLATE_LAST};

    // Create grid on the screen
    lv_obj_t *cont = lv_obj_create(lv_screen_active());
    lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);
    lv_obj_set_size(cont, 470, 470);
    lv_obj_center(cont);

    // Main objects to render
    lv_obj_t * obj;
    lv_obj_t * label;

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
     * Ecodan Metrics
     */
    // Target Temperature Set
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

    // Ambient Temperature from Remote Control (RC)
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

    // State Bar
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
    // Temperature
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
    lv_obj_add_event_cb(obj, ui_chart_event_callback, LV_EVENT_PRESSED, &chart_meta[0]);

    // Relative Humidity
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
    lv_obj_add_event_cb(obj, ui_chart_event_callback, LV_EVENT_PRESSED, &chart_meta[1]);

    // tVOC measurament
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
    lv_obj_add_event_cb(obj, ui_chart_event_callback, LV_EVENT_PRESSED, &chart_meta[2]);

    // CO2 measurement
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
    lv_obj_add_event_cb(obj, ui_chart_event_callback, LV_EVENT_PRESSED, &chart_meta[3]);

    /**
     * Buttons
     */
    // Temperature Up
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

    // Temperature Down
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

    // Hotwater On
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

    // Holiday Mode
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