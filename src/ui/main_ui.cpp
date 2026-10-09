#include "main_ui.h"
#include "../core/data_setup.h"
#include "../conf/global_config.h"
#include "../core/screen_driver.h"
#include "../core/printer_integration.hpp"
#include "lvgl.h"
#include "ui_utils.h"
#include "panels/panel.h"
#include "../core/lv_setup.h"
#include "macros.h"

void check_if_screen_needs_to_be_disabled(){
  if (global_config.on_during_print && get_current_printer_data()->state == PrinterState::PrinterStatePrinting){
    screen_timer_wake();
    screen_timer_stop();
  } else {
    screen_timer_start();
  }
}

static void on_data_for_screen(void* s, lv_msg_t* m){
  (void)s; (void)m;
  check_if_screen_needs_to_be_disabled();
}

static void on_popup_message(void * s, lv_msg_t * m)
{
  lv_create_popup_message(get_current_printer_data()->popup_message, get_current_printer()->popup_message_timeout_s * 1000);
}

void main_ui_setup(){
  lv_msg_subscribe(DATA_PRINTER_POPUP, on_popup_message, NULL);
  lv_msg_subscribe(DATA_PRINTER_DATA, on_data_for_screen, NULL);
  audi_home_init();
}
