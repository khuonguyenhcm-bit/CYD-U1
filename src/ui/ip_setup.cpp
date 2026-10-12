#include "ip_setup.h"
#include "../conf/global_config.h"
#include "lvgl.h"
#include <HTTPClient.h>
#include "core/data_setup.h"
#include "ui_utils.h"
#include "panels/panel.h"
#include "panels/printer_autoscan.h"
#include "macros.h"
#include "../core/lv_setup.h"
#include "serial/serial_console.h"
#include "../core/klipper/klipper_printer_integration.hpp"
#include "../core/bambu/bambu_printer_integration.hpp"
#include "../core/screen_driver.h"
#include "../core/klipper-serial/serial_klipper_printer_integration.hpp"
#include "../core/octoprint/octoprint_printer_integration.hpp"

void show_ip_entry();
void choose_printer_type();

lv_obj_t * main_label;

// --- Auto-scan on the Klipper setup screen ---
// The subnet scan runs in its own FreeRTOS task (printer_autoscan), so the
// found-callback must not touch LVGL from that task: it only stores the IP
// and raises a flag. An LVGL timer polls the flag back in UI context, fills
// in the entry fields and runs the normal connection test.
static lv_obj_t * s_host_entry = NULL;
static lv_obj_t * s_port_entry = NULL;
static lv_obj_t * s_find_btn = NULL;
static lv_obj_t * s_keyboard = NULL;
static lv_timer_t * s_scan_timer = NULL;
static char s_found_ip[16] = "";
static volatile bool s_found_ready = false;
static bool s_scan_seen_running = false;

static void setup_scan_found_cb(const char* ip_str)
{
    strncpy(s_found_ip, ip_str, sizeof(s_found_ip) - 1);
    s_found_ip[sizeof(s_found_ip) - 1] = '\0';
    s_found_ready = true;
}

// The connection test itself. The stock screen only ran this from the
// keyboard's OK key; it is shared here with the Connect button and the
// auto-scan poll below.
static void ip_entry_try_connect()
{
    PrinterType type = global_config.printer_config[global_config.printer_index].printer_type;

    if (type == PrinterType::PrinterTypeKlipper)
    {
        KlipperConnectionStatus klipper_status = connection_test_klipper(&global_config.printer_config[global_config.printer_index]);
        if (klipper_status == KlipperConnectionStatus::ConnectOk)
        {
            global_config.printer_config[global_config.printer_index].setup_complete = true;
            write_global_config();
        }
        else if (klipper_status == KlipperConnectionStatus::ConnectAuthRequired)
        {
            lv_label_set_text(main_label, "Incorrect authorisation");
        }
        else
        {
            lv_label_set_text(main_label, "Failed to connect");
        }
    }
    else if (type == PrinterType::PrinterTypeBambuLocal)
    {
        BambuConnectionStatus bambu_status = connection_test_bambu(&global_config.printer_config[global_config.printer_index]);
        if (bambu_status == BambuConnectionStatus::BambuConnectOk)
        {
            global_config.printer_config[global_config.printer_index].setup_complete = true;
            write_global_config();
        }
        else if (bambu_status == BambuConnectionStatus::BambuConnectSNFail)
        {
            lv_label_set_text(main_label, "Incorrect serial number");
        }
        else
        {
            lv_label_set_text(main_label, "Incorrect IP/Access code");
        }
    }
    else if (type == PrinterType::PrinterTypeOctoprint)
    {
        OctoConnectionStatus octo_status = connection_test_octoprint(&global_config.printer_config[global_config.printer_index]);
        if (octo_status == OctoConnectionStatus::OctoConnectOk)
        {
            global_config.printer_config[global_config.printer_index].setup_complete = true;
            write_global_config();
        }
        else if (octo_status == OctoConnectionStatus::OctoConnectKeyFail)
        {
            lv_label_set_text(main_label, "Incorrect API key");
        }
        else
        {
            lv_label_set_text(main_label, "Failed to connect");
        }
    }
}

// Tapping a button defocuses the active text entry, and the stock focus
// handler hides the keyboard on defocus. Bring it back after button taps
// so manual entry stays possible while a scan runs.
static void ip_entry_show_keyboard()
{
    if (s_keyboard == NULL || s_host_entry == NULL)
    {
        return;
    }
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_USER_1);
    lv_keyboard_set_textarea(s_keyboard, s_host_entry);
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void btn_connect(lv_event_t * e)
{
    (void)e;
    ip_entry_try_connect();
    if (global_config.printer_config[global_config.printer_index].setup_complete)
    {
        lv_label_set_text(main_label, "Connected!");
    }
    ip_entry_show_keyboard();
}

static void scan_timer_stop()
{
    if (s_scan_timer != NULL)
    {
        lv_timer_del(s_scan_timer);
        s_scan_timer = NULL;
    }
    if (s_find_btn != NULL)
    {
        lv_obj_clear_state(s_find_btn, LV_STATE_DISABLED);
    }
}

static void scan_poll_timer_cb(lv_timer_t * timer)
{
    (void)timer;

    if (s_found_ready)
    {
        s_found_ready = false;
        scan_timer_stop();

        PrinterConfiguration * cfg = &global_config.printer_config[global_config.printer_index];
        strncpy(cfg->printer_host, s_found_ip, sizeof(cfg->printer_host) - 1);
        cfg->printer_host[sizeof(cfg->printer_host) - 1] = '\0';
        cfg->ip_configured = true;
        cfg->klipper_port = AUTOSCAN_PORT;

        if (s_host_entry != NULL)
        {
            lv_textarea_set_text(s_host_entry, s_found_ip);
        }
        if (s_port_entry != NULL)
        {
            lv_textarea_set_text(s_port_entry, "7125");
        }

        char buff[48];
        snprintf(buff, sizeof(buff), "Found printer: %s", s_found_ip);
        lv_label_set_text(main_label, buff);

        ip_entry_try_connect();
        if (cfg->setup_complete)
        {
            lv_label_set_text(main_label, "Connected!");
        }
        ip_entry_show_keyboard();
        return;
    }

    if (autoscan_is_scanning())
    {
        s_scan_seen_running = true;
    }
    else if (s_scan_seen_running)
    {
        // Scan task finished without finding a printer
        scan_timer_stop();
        lv_label_set_text(main_label, "Printer not found - enter IP manually");
        ip_entry_show_keyboard();
    }
}

static void btn_find_printer(lv_event_t * e)
{
    (void)e;
    s_found_ready = false;
    s_found_ip[0] = '\0';
    s_scan_seen_running = false;

    if (s_scan_timer != NULL)
    {
        lv_timer_del(s_scan_timer);
        s_scan_timer = NULL;
    }

    autoscan_init(setup_scan_found_cb);
    autoscan_trigger_now();

    lv_label_set_text(main_label, "Scanning for printer...");
    if (s_find_btn != NULL)
    {
        lv_obj_add_state(s_find_btn, LV_STATE_DISABLED);
    }
    s_scan_timer = lv_timer_create(scan_poll_timer_cb, 500, NULL);
    ip_entry_show_keyboard();
}

// Fires when the setup screen is torn down: stop the poll timer and drop the
// object pointers so nothing references deleted LVGL objects.
static void ip_entry_deleted_cb(lv_event_t * e)
{
    (void)e;
    if (s_scan_timer != NULL)
    {
        lv_timer_del(s_scan_timer);
        s_scan_timer = NULL;
    }
    s_host_entry = NULL;
    s_port_entry = NULL;
    s_find_btn = NULL;
    s_keyboard = NULL;
}

/* Create a custom keyboard to allow hostnames or ip addresses (a-z, 0 - 9, and -) */
static const char * kb_map[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_OK, "\n",
    LV_SYMBOL_LEFT, "z", "x", "c", "v", "b", "n", "m", ".", "-", LV_SYMBOL_RIGHT, NULL
};

static const lv_btnmatrix_ctrl_t kb_ctrl[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, LV_KEYBOARD_CTRL_BTN_FLAGS | 6,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, LV_KEYBOARD_CTRL_BTN_FLAGS | 5,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 6, 4, 4, 4, 4, 4, 4, 4, 4, 4, LV_KEYBOARD_CTRL_BTN_FLAGS | 6
};

static const char * hex_numpad_map[] = {
    "1", "2", "3", "F", LV_SYMBOL_BACKSPACE, "\n",
    "4", "5", "6", "E", LV_SYMBOL_OK, "\n",
    "7", "8", "9", "D", LV_SYMBOL_LEFT, "\n",
    "0", "A", "B", "C", LV_SYMBOL_RIGHT, NULL
};

static const lv_btnmatrix_ctrl_t hex_numpad_ctrl[] = {
    1, 1, 1, 1, LV_KEYBOARD_CTRL_BTN_FLAGS | 1,
    1, 1, 1, 1, LV_KEYBOARD_CTRL_BTN_FLAGS | 1,
    1, 1, 1, 1, LV_KEYBOARD_CTRL_BTN_FLAGS | 1,
    1, 1, 1, 1, LV_KEYBOARD_CTRL_BTN_FLAGS | 1,
};

static void btn_switch_printer(lv_event_t *e){
    lv_obj_t *btn = lv_event_get_target(e);
    PrinterConfiguration * config = (PrinterConfiguration*)lv_event_get_user_data(e);
    int index = config - global_config.printer_config;

    global_config_set_printer(index);
    set_color_scheme();
    set_invert_display();
    lv_obj_del(lv_obj_get_parent(lv_obj_get_parent(btn)));
}

long last_request = 0;

void serial_check_connection()
{
    if ((millis() - last_request) < 5000)
    {
        return;
    }

    auto result = connection_test_serial_klipper(&global_config.printer_config[global_config.printer_index]);
    last_request = millis();

    if (result == KlipperConnectionStatus::ConnectOk)
    {
        global_config.printer_config[global_config.printer_index].setup_complete = true;
        strcpy(global_config.printer_config[global_config.printer_index].printer_host, "Serial");
        write_global_config();
    }
}

void switch_printer_init() {
    // Use the real display resolution: the CYD_SCREEN_*_PX constants hold
    // landscape values (320x240) but the panel runs portrait 240x320.
    const lv_coord_t scr_w = lv_disp_get_hor_res(NULL);
    const lv_coord_t scr_h = lv_disp_get_ver_res(NULL);

    lv_obj_t * parent = lv_create_empty_panel(lv_scr_act());
    lv_obj_set_style_bg_opa(parent, LV_OPA_100, 0); 
    lv_obj_align(parent, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(parent, scr_w, scr_h);
    lv_layout_flex_column(parent);

    lv_obj_set_size(lv_create_empty_panel(parent), 0, 0);

    auto width = scr_w - CYD_SCREEN_GAP_PX * 2;

    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, CYD_SCREEN_MIN_BUTTON_HEIGHT_PX);
    lv_obj_add_event_cb(btn, destroy_event_user_data, LV_EVENT_CLICKED, parent);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_CLOSE " Close");
    lv_obj_center(label);

    for (int i = 0; i < PRINTER_CONFIG_COUNT; i++){
        PrinterConfiguration * config = &global_config.printer_config[i];
        const char* printer_name = (config->printer_name[0] == 0) ? config->printer_host : config->printer_name;

        if (i == global_config.printer_index && config->setup_complete)
        {
            lv_create_custom_menu_label(printer_name, parent, "Active");
            continue;
        }

        if (config->setup_complete) {
            lv_create_custom_menu_button(printer_name, parent, btn_switch_printer, "Switch", config);
        }
    }
}

static void show_switch_printer_screen(lv_event_t * e){
    switch_printer_init();
}

static void host_update(lv_event_t * e)
{
    lv_obj_t * ta = lv_event_get_target(e);
    const char* text = lv_textarea_get_text(ta);
    strcpy(global_config.printer_config[global_config.printer_index].printer_host, text);
    global_config.printer_config[global_config.printer_index].ip_configured = text[0] != '\0';
}

static void port_update(lv_event_t * e)
{
    lv_obj_t * ta = lv_event_get_target(e);
    const char* text = lv_textarea_get_text(ta);
    if (text[0] != '\0')
    {
        global_config.printer_config[global_config.printer_index].klipper_port = atoi(text);
    }
}

static void auth_update(lv_event_t * e)
{
    lv_obj_t * ta = lv_event_get_target(e);
    const char* text = lv_textarea_get_text(ta);
    strcpy(global_config.printer_config[global_config.printer_index].printer_auth, text);
    global_config.printer_config[global_config.printer_index].auth_configured = text[0] != '\0';
}

static void return_to_choose_printer_type(lv_event_t * e)
{
    choose_printer_type();
}

static void keyboard_event_ip_entry(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    lv_obj_t * kb = (lv_obj_t *)lv_event_get_user_data(e);

    if ((code == LV_EVENT_FOCUSED || code == LV_EVENT_DEFOCUSED) && ta != NULL)
    {
        // make sure we alter the keymap before taking actions that might
        // destroy the keyboard
        if (lv_obj_has_flag(ta, LV_OBJ_FLAG_USER_1))
        {
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_1);
        }
        else if (lv_obj_has_flag(ta, LV_OBJ_FLAG_USER_2))
        {
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_USER_2);
        }
        else if (lv_obj_has_flag(ta, LV_OBJ_FLAG_USER_3))
        {
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
        }
        else
        {
            lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
        }
    }

    if(code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    else if(code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    else if (code == LV_EVENT_READY) 
    {
        ip_entry_try_connect();
    }
}

void show_ip_entry()
{
    lv_obj_clean(lv_scr_act());

    // Use the real display resolution: the CYD_SCREEN_*_PX constants hold
    // landscape values (320x240) but the panel runs portrait 240x320, so
    // sizing this screen from the constants pushes the keyboard and the
    // entry rows off the edge of the screen.
    const lv_coord_t scr_w = lv_disp_get_hor_res(NULL);
    const lv_coord_t scr_h = lv_disp_get_ver_res(NULL);

    lv_obj_t * root = lv_create_empty_panel(lv_scr_act());
    lv_obj_set_size(root, scr_w, scr_h);
    lv_layout_flex_column(root);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root, ip_entry_deleted_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t * top_root = lv_create_empty_panel(root);
    lv_obj_set_width(top_root, scr_w);
    lv_layout_flex_column(top_root);
    lv_obj_set_flex_grow(top_root, 1);
    lv_obj_set_style_pad_all(top_root, CYD_SCREEN_GAP_PX, 0);
    lv_obj_clear_flag(top_root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * button_row = lv_create_empty_panel(top_root);
    lv_obj_set_size(button_row, scr_w - CYD_SCREEN_GAP_PX * 2, LV_SIZE_CONTENT);
    lv_layout_flex_row(button_row);

    lv_obj_t * button_back = lv_btn_create(button_row);
    lv_obj_set_height(button_back, CYD_SCREEN_MIN_BUTTON_HEIGHT_PX / 2);
    lv_obj_set_flex_grow(button_back, 1);
    lv_obj_add_event_cb(button_back, return_to_choose_printer_type, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label = lv_label_create(button_back);
    lv_label_set_text(label, LV_SYMBOL_LEFT);
    lv_obj_center(label);

    main_label = lv_label_create(button_row);

    if (global_config.multi_printer_mode)
    {
        lv_obj_t * button_switch_printer = lv_btn_create(button_row);
        lv_obj_set_height(button_switch_printer, CYD_SCREEN_MIN_BUTTON_HEIGHT_PX / 2);
        lv_obj_set_flex_grow(button_switch_printer, 1);
        lv_obj_add_event_cb(button_switch_printer, show_switch_printer_screen, LV_EVENT_CLICKED, NULL);

        label = lv_label_create(button_switch_printer);
        lv_label_set_text(label, LV_SYMBOL_HOME);
        lv_obj_center(label);
    }

    lv_obj_t * ip_row = lv_create_empty_panel(top_root);
    lv_obj_set_size(ip_row, scr_w - CYD_SCREEN_GAP_PX * 2, LV_SIZE_CONTENT);
    lv_layout_flex_row(ip_row);

    lv_obj_t * host_entry = lv_textarea_create(ip_row);
    lv_textarea_set_one_line(host_entry, true);
    lv_obj_add_flag(host_entry, LV_OBJ_FLAG_USER_1);
    lv_textarea_set_max_length(host_entry, 64);
    lv_obj_set_flex_grow(host_entry, 2);

    lv_obj_t * port_entry = lv_textarea_create(ip_row);
    lv_textarea_set_one_line(port_entry, true);
    lv_obj_set_flex_grow(port_entry, 1);

    lv_obj_t * auth_row = lv_create_empty_panel(top_root);
    lv_obj_set_size(auth_row, scr_w - CYD_SCREEN_GAP_PX * 2, LV_SIZE_CONTENT);
    lv_layout_flex_row(auth_row);

    lv_obj_t * auth_entry = lv_textarea_create(auth_row);
    lv_textarea_set_one_line(auth_entry, true);
    lv_obj_add_flag(auth_entry, LV_OBJ_FLAG_USER_2);
    lv_obj_set_flex_grow(auth_entry, 1);
    lv_textarea_set_max_length(auth_entry, 32);

    s_host_entry = host_entry;
    s_port_entry = port_entry;

    // Action row: Find Printer (Klipper only - scans the subnet for
    // Moonraker instead of typing the IP) next to an explicit Connect
    // button for manual entry. The stock screen had no Connect button:
    // the connection test only ran from the keyboard's OK key.
    if (global_config.printer_config[global_config.printer_index].printer_type != PrinterType::PrinterTypeKlipperSerial)
    {
        lv_obj_t * action_row = lv_create_empty_panel(top_root);
        lv_obj_set_size(action_row, scr_w - CYD_SCREEN_GAP_PX * 2, LV_SIZE_CONTENT);
        lv_layout_flex_row(action_row);

        if (global_config.printer_config[global_config.printer_index].printer_type == PrinterType::PrinterTypeKlipper)
        {
            s_find_btn = lv_btn_create(action_row);
            lv_obj_set_flex_grow(s_find_btn, 1);
            lv_obj_set_height(s_find_btn, CYD_SCREEN_MIN_BUTTON_HEIGHT_PX / 2);
            lv_obj_add_event_cb(s_find_btn, btn_find_printer, LV_EVENT_CLICKED, NULL);

            lv_obj_t * find_label = lv_label_create(s_find_btn);
            lv_label_set_text(find_label, LV_SYMBOL_WIFI " Find");
            lv_obj_center(find_label);
        }

        lv_obj_t * connect_btn = lv_btn_create(action_row);
        lv_obj_set_flex_grow(connect_btn, 1);
        lv_obj_set_height(connect_btn, CYD_SCREEN_MIN_BUTTON_HEIGHT_PX / 2);
        lv_obj_add_event_cb(connect_btn, btn_connect, LV_EVENT_CLICKED, NULL);

        lv_obj_t * connect_label = lv_label_create(connect_btn);
        lv_label_set_text(connect_label, "Connect");
        lv_obj_center(connect_label);
    }

    if (global_config.printer_config[global_config.printer_index].ip_configured)
    {
        char buff[10] = {0};
        sprintf(buff, "%d", global_config.printer_config[global_config.printer_index].klipper_port);
        lv_textarea_set_text(host_entry, global_config.printer_config[global_config.printer_index].printer_host);
        lv_textarea_set_text(port_entry, buff);
    }
    else 
    {
        lv_textarea_set_text(host_entry, "");

        if (global_config.printer_config[global_config.printer_index].printer_type == PrinterType::PrinterTypeBambuLocal)
        {
            lv_textarea_set_text(port_entry, "");
            global_config.printer_config[global_config.printer_index].klipper_port = 0;
        }
        else if (global_config.printer_config[global_config.printer_index].printer_type == PrinterType::PrinterTypeOctoprint)
        {
            lv_textarea_set_text(port_entry, "80");
            global_config.printer_config[global_config.printer_index].klipper_port = 80;
        }
        else
        {
            // Klipper talks to Moonraker directly on 7125
            lv_textarea_set_text(port_entry, "7125");
            global_config.printer_config[global_config.printer_index].klipper_port = 7125;
        }
        
        global_config.printer_config[global_config.printer_index].printer_host[0] = '\0';
        
    }

    if (global_config.printer_config[global_config.printer_index].auth_configured)
    {
        lv_textarea_set_text(auth_entry, global_config.printer_config[global_config.printer_index].printer_auth);
    }
    else
    {
        lv_textarea_set_text(auth_entry, "");
        global_config.printer_config[global_config.printer_index].printer_auth[0] = '\0';
    }

    lv_obj_add_event_cb(host_entry, host_update, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(port_entry, port_update, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(auth_entry, auth_update, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * keyboard = lv_keyboard_create(root);
    s_keyboard = keyboard;
    lv_obj_set_width(keyboard, scr_w);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1, kb_map, kb_ctrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_2, hex_numpad_map, hex_numpad_ctrl);
    lv_obj_add_event_cb(host_entry, keyboard_event_ip_entry, LV_EVENT_ALL, keyboard);
    lv_obj_add_event_cb(port_entry, keyboard_event_ip_entry, LV_EVENT_ALL, keyboard);
    lv_obj_add_event_cb(auth_entry, keyboard_event_ip_entry, LV_EVENT_ALL, keyboard);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_1);
    lv_keyboard_set_textarea(keyboard, host_entry);

    switch (global_config.printer_config[global_config.printer_index].printer_type)
    {
        case PrinterType::PrinterTypeKlipper:
            lv_label_set_text(main_label, "Klipper Setup");
            lv_textarea_set_max_length(port_entry, 5);
            lv_textarea_set_placeholder_text(host_entry, "Klipper host");
            lv_textarea_set_placeholder_text(port_entry, "Port");
            lv_textarea_set_placeholder_text(auth_entry, "Autorisation key (optional)");
            break;
        case PrinterType::PrinterTypeBambuLocal:
            lv_label_set_text(main_label, "Bambu (Local) Setup");
            lv_obj_set_flex_grow(port_entry, 4);
            lv_obj_set_flex_grow(host_entry, 6);
            lv_textarea_set_max_length(port_entry, 8);
            // Access codes contain letters, so the port field must not use
            // the default numeric keyboard: give it the full keyboard map
            lv_obj_add_flag(port_entry, LV_OBJ_FLAG_USER_1);
            lv_textarea_set_placeholder_text(host_entry, "Printer IP");
            lv_textarea_set_placeholder_text(port_entry, "Access code");
            lv_textarea_set_placeholder_text(auth_entry, "Printer serial number");
            lv_obj_clear_flag(auth_entry, LV_OBJ_FLAG_USER_2);
            lv_obj_add_flag(auth_entry, LV_OBJ_FLAG_USER_3);
            break;
        case PrinterType::PrinterTypeOctoprint:
            lv_obj_clear_flag(auth_entry, LV_OBJ_FLAG_USER_2);
            lv_obj_add_flag(auth_entry, LV_OBJ_FLAG_USER_3);
            lv_textarea_set_max_length(auth_entry, 48);
            lv_label_set_text(main_label, "Octoprint Setup");
            lv_textarea_set_max_length(port_entry, 5);
            lv_textarea_set_placeholder_text(host_entry, "Octoprint Host");
            lv_textarea_set_placeholder_text(port_entry, "Port");
            lv_textarea_set_placeholder_text(auth_entry, "API key");
            break;
        case PrinterType::PrinterTypeKlipperSerial:
            lv_label_set_text(main_label, "Klipper (Serial) Setup");
            lv_obj_del(ip_row);
            lv_obj_del(auth_row);
            lv_obj_del(keyboard);

            lv_obj_t * bottom_root = lv_create_empty_panel(top_root);
            lv_obj_set_width(bottom_root, scr_w);
            lv_obj_set_flex_grow(bottom_root, 1);
            
            label = lv_label_create(bottom_root);
            lv_obj_center(label);
            lv_label_set_text(label, "Connect CYD-Klipper to a host\nrunning the CYD-Klipper server");
            break;
    }
}

static void printer_type_klipper(lv_event_t * e)
{
    global_config.printer_config[global_config.printer_index].printer_type = PrinterType::PrinterTypeKlipper;
    show_ip_entry();
}

static void printer_type_bambu_local(lv_event_t * e)
{
    global_config.printer_config[global_config.printer_index].printer_type = PrinterType::PrinterTypeBambuLocal;
    show_ip_entry();
}

static void printer_type_serial_klipper(lv_event_t * e)
{
    global_config.printer_config[global_config.printer_index].printer_type = PrinterType::PrinterTypeKlipperSerial;
    show_ip_entry();
}

static void printer_type_octoprint(lv_event_t * e)
{
    global_config.printer_config[global_config.printer_index].printer_type = PrinterType::PrinterTypeOctoprint;
    show_ip_entry();
}

static void return_to_wifi_configuration(lv_event_t * e)
{
    global_config.wifi_configuration_skipped = false;
    write_global_config();
    ESP.restart();
}

static inline void create_printer_type_button(lv_obj_t * root, const char * label, lv_event_cb_t event, bool require_wifi = true)
{
    if (require_wifi && !global_config.wifi_configured)
    {
        return;
    }

    lv_obj_t * btn = lv_btn_create(root);
    lv_obj_set_size(btn, lv_disp_get_hor_res(NULL) - CYD_SCREEN_GAP_PX * 2, CYD_SCREEN_MIN_BUTTON_HEIGHT_PX);
    lv_obj_add_event_cb(btn, event, LV_EVENT_CLICKED, NULL);

    lv_obj_t * label_obj = lv_label_create(btn);
    lv_label_set_text(label_obj, label);
    lv_obj_center(label_obj);
}

void choose_printer_type()
{
    lv_obj_t * btn;
    lv_obj_clean(lv_scr_act());
    global_config.printer_config[global_config.printer_index].ip_configured = false;
    global_config.printer_config[global_config.printer_index].auth_configured = false;

    lv_obj_t * root = lv_create_empty_panel(lv_scr_act());
    lv_obj_set_size(root, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_layout_flex_column(root);
    lv_obj_set_flex_grow(root, 1);
    lv_obj_set_style_pad_all(root, CYD_SCREEN_GAP_PX, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * label = lv_label_create(root);
    lv_label_set_text(label, "Choose printer type");

    create_printer_type_button(root, "Klipper (Wifi)", printer_type_klipper);
    create_printer_type_button(root, "Klipper (Serial/USB)", printer_type_serial_klipper, false);
    create_printer_type_button(root, "Bambu (Wifi, Local) [BETA]", printer_type_bambu_local);
    create_printer_type_button(root, "Octoprint (Wifi) [BETA]", printer_type_octoprint);
    
    if (global_config.wifi_configuration_skipped)
    {
        create_printer_type_button(root, "Return to WiFi configuration", return_to_wifi_configuration, false);
    }
}

void ip_init(){
    if (!global_config.printer_config[global_config.printer_index].setup_complete)
    {
        if (global_config.printer_config[global_config.printer_index].printer_type == PrinterType::PrinterTypeNone)
        {
            choose_printer_type();
        }
        else
        {
            show_ip_entry();
        }
    }
    
    while (!global_config.printer_config[global_config.printer_index].setup_complete)
    {
        if (global_config.printer_config[global_config.printer_index].printer_type == PrinterType::PrinterTypeKlipperSerial)
        {
            serial_check_connection();
        }

        lv_handler();
        serial_console::run();
    }
}