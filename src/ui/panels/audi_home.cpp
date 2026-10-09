#include "lvgl.h"
#include "../../core/printer_integration.hpp"
#include "../../core/current_printer.h"
#include "printer_anim.h"
#include "panel.h"
#include <stdlib.h>
#include <string.h>

void files_panel_init(lv_obj_t* panel);

#define C_RED     0xE53935
#define C_BG      0xFFFFFF
#define C_TEXT    0x212121
#define C_GRAY    0x757575
#define C_TRACK   0xEEEEEE

static lv_obj_t* ah_anim_img;
static lv_img_dsc_t ah_img_dsc;
static int ah_anim_frame = 0;
static int ah_anim_kind = -1;

static lv_obj_t* ah_status;
static lv_obj_t* ah_printer_name;
static lv_obj_t* ah_start_btn;
static lv_obj_t* ah_bar;
static lv_obj_t* ah_info;
static lv_obj_t* ah_temp_box;
static lv_obj_t* ah_temp_lbl[6];
static lv_obj_t* ah_temp_dot[6];
static int ah_active_dot = -1;
static lv_obj_t* ah_btn_box;
static lv_obj_t* ah_pause_lbl;
static lv_timer_t* ah_anim_timer = NULL;
static int ah_phase = 0;

static void ah_show_frame(void) {
  const uint16_t* src = NULL;
  if (ah_anim_kind == 0) src = anim_printing[ah_anim_frame];
  else if (ah_anim_kind == 1) src = anim_error[ah_anim_frame];
  else if (ah_anim_kind == 2) src = anim_sleeping[ah_anim_frame];
  if (!src) return;
  ah_img_dsc.data = (uint8_t*)src;
  lv_img_set_src(ah_anim_img, &ah_img_dsc);
}

static void ah_anim_cb(lv_timer_t* t) {
  (void)t;
  PrinterState st = get_current_printer_data()->state;
  int kind;
  if (st == PrinterState::PrinterStatePrinting ||
      st == PrinterState::PrinterStatePaused) kind = 0;
  else if (st == PrinterState::PrinterStateError) kind = 1;
  else kind = 2;
  if (kind != ah_anim_kind) {
    ah_anim_kind = kind;
    ah_anim_frame = 0;
    if (kind == 2) {
      lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
      lv_obj_set_style_text_color(ah_status, lv_color_hex(0xB0B0B0), 0);
      lv_obj_set_style_text_color(ah_printer_name, lv_color_hex(0xFFFFFF), 0);
    } else {
      lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_text_color(ah_status, lv_color_hex(0x757575), 0);
      lv_obj_set_style_text_color(ah_printer_name, lv_color_hex(0x212121), 0);
    }
  }
  if (ah_anim_img) {
    ah_show_frame();
    int nframes = (kind == 0) ? ANIM_PRINTING_FRAMES :
                  (kind == 1) ? ANIM_ERROR_FRAMES : ANIM_SLEEPING_FRAMES;
    ah_anim_frame = (ah_anim_frame + 1) % nframes;
  }
  if (st == PrinterState::PrinterStatePrinting ||
      st == PrinterState::PrinterStatePaused) {
    ah_phase++;
    bool dot_on = (ah_phase % 5) < 3;
    for (int i = 0; i < 6; i++) {
      if (i == ah_active_dot) {
        if (dot_on) lv_obj_clear_flag(ah_temp_dot[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(ah_temp_dot[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_clear_flag(ah_temp_dot[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  const char* txt = "";
  switch (st) {
    case PrinterState::PrinterStatePrinting: txt = "Printing"; break;
    case PrinterState::PrinterStatePaused: txt = "Paused"; break;
    case PrinterState::PrinterStateIdle: txt = "Ready"; break;
    case PrinterState::PrinterStateOffline: txt = "Connecting..."; break;
    case PrinterState::PrinterStateError: txt = "Printer Error"; break;
    default: break;
  }
  if (ah_status) lv_label_set_text(ah_status, txt);
}

static void ah_on_pause(lv_event_t* e) {
  (void)e;
  PrinterState st = get_current_printer_data()->state;
  if (st == PrinterState::PrinterStatePrinting)
    current_printer_execute_feature(PrinterFeatures::PrinterFeaturePause);
  else if (st == PrinterState::PrinterStatePaused)
    current_printer_execute_feature(PrinterFeatures::PrinterFeatureResume);
}

static void ah_on_stop(lv_event_t* e) {
  (void)e;
  current_printer_execute_feature(PrinterFeatures::PrinterFeatureStop);
}

static void ah_show_home(void);
static void ah_on_start(lv_event_t* e);

static void ah_on_start(lv_event_t* e) {
  (void)e;
  ah_anim_img = NULL;
  lv_obj_t* panel = lv_obj_create(lv_scr_act());
  lv_obj_set_size(panel, 240, 320);
  lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_t* title = lv_label_create(panel);
  lv_label_set_text(title, "Choose file");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_style_text_color(title, lv_color_hex(0x000000), 0);
  lv_obj_t* list_holder = lv_obj_create(panel);
  lv_obj_set_size(list_holder, 230, 220);
  lv_obj_align(list_holder, LV_ALIGN_TOP_MID, 0, 36);
  lv_obj_set_style_bg_opa(list_holder, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list_holder, 0, 0);
  lv_obj_set_style_pad_all(list_holder, 0, 0);
  files_panel_init(list_holder);
  lv_obj_t* bb = lv_btn_create(panel);
  lv_obj_set_size(bb, 120, 38);
  lv_obj_align(bb, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_color(bb, lv_color_hex(0xE53935), 0);
  lv_obj_add_event_cb(bb, [](lv_event_t* e) {
    lv_obj_t* p = (lv_obj_t*)lv_event_get_user_data(e);
    lv_obj_del(p);
    ah_show_home();
  }, LV_EVENT_CLICKED, panel);
  lv_obj_t* bl = lv_label_create(bb);
  lv_label_set_text(bl, "Back");
  lv_obj_set_style_text_color(bl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(bl);
}

static void ah_refresh(void) {
  PrinterData* d = get_current_printer_data();
  if (!d) return;
  PrinterState st = d->state;
  bool printing = (st == PrinterState::PrinterStatePrinting ||
                   st == PrinterState::PrinterStatePaused);
  bool idle = (st == PrinterState::PrinterStateIdle);
  if (ah_start_btn) {
    if (idle) lv_obj_clear_flag(ah_start_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ah_start_btn, LV_OBJ_FLAG_HIDDEN);
  }
  if (ah_bar) {
    if (printing) {
      lv_obj_clear_flag(ah_bar, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ah_info, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ah_temp_box, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ah_btn_box, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ah_bar, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ah_info, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ah_temp_box, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ah_btn_box, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (printing && ah_bar) {
    int pct = (int)(d->print_progress * 100.0f);
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    lv_bar_set_value(ah_bar, pct, LV_ANIM_OFF);
    int secs = (int)d->remaining_time_s;
    char tb[20];
    if (secs >= 3600) snprintf(tb, sizeof(tb), "%dh%02dm", secs/3600, (secs%3600)/60);
    else if (secs >= 60) snprintf(tb, sizeof(tb), "%dm", secs/60);
    else if (secs > 0) snprintf(tb, sizeof(tb), "%ds", secs);
    else snprintf(tb, sizeof(tb), "--");
    char ib[48];
    if (d->total_layers > 0)
      snprintf(ib, sizeof(ib), "L%d/%d %s", d->current_layer, d->total_layers, tb);
    else snprintf(ib, sizeof(ib), "%s left", tb);
    lv_label_set_text(ah_info, ib);
    const int idx[6] = {1,2,3,4,0,9};
    const char* nm[6] = {"T1","T2","T3","T4","Bed","Chm"};
    for (int i = 0; i < 6; i++) {
      char tbuf[16];
      snprintf(tbuf, sizeof(tbuf), "%s %.0f", nm[i], d->temperatures[idx[i]]);
      lv_label_set_text(ah_temp_lbl[i], tbuf);
    }
    ah_active_dot = -1;
    float max_t = 50.0f;
    for (int i = 0; i < 4; i++) {
      if (d->target_temperatures[i+1] > max_t) {
        max_t = d->target_temperatures[i+1];
        ah_active_dot = i;
      }
    }
    lv_label_set_text(ah_pause_lbl,
      st == PrinterState::PrinterStatePrinting ? "Pause" : "Resume");
  }
}

static void ah_on_msg(void* s, lv_msg_t* m) {
  (void)s; (void)m;
  if (ah_anim_img) ah_refresh();
}

static void ah_show_home(void) {
  lv_obj_clean(lv_scr_act());
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(C_BG), 0);
  ah_img_dsc.header.always_zero = 0;
  ah_img_dsc.header.w = ANIM_SIZE;
  ah_img_dsc.header.h = ANIM_SIZE;
  ah_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  ah_img_dsc.data_size = ANIM_SIZE * ANIM_SIZE * 2;
  ah_img_dsc.data = NULL;
  ah_anim_img = lv_img_create(lv_scr_act());
  lv_obj_align(ah_anim_img, LV_ALIGN_TOP_MID, 0, 4);
  lv_img_set_src(ah_anim_img, &ah_img_dsc);
  ah_anim_kind = -1;
  ah_anim_frame = 0;
  ah_printer_name = lv_label_create(lv_scr_act());
 {
  const char* pname = get_current_printer()->printer_config->printer_name;
  if (!pname || pname[0] == '\0') pname = get_current_printer()->printer_config->printer_host;
  if (!pname || pname[0] == '\0') pname = "Snapmaker U1";
  lv_label_set_text(ah_printer_name, pname);
}

  lv_obj_align(ah_printer_name, LV_ALIGN_TOP_MID, 0, 86);
  lv_obj_set_style_text_color(ah_printer_name, lv_color_hex(C_TEXT), 0);
  ah_status = lv_label_create(lv_scr_act());
  lv_obj_align(ah_status, LV_ALIGN_TOP_MID, 0, 112);
  lv_obj_set_style_text_color(ah_status, lv_color_hex(C_GRAY), 0);
  ah_start_btn = lv_btn_create(lv_scr_act());
  lv_obj_set_size(ah_start_btn, 200, 50);
  lv_obj_align(ah_start_btn, LV_ALIGN_TOP_MID, 0, 141);
  lv_obj_set_style_bg_color(ah_start_btn, lv_color_hex(C_RED), 0);
  lv_obj_set_style_radius(ah_start_btn, 10, 0);
  lv_obj_add_event_cb(ah_start_btn, ah_on_start, LV_EVENT_CLICKED, NULL);
  lv_obj_t* stxt = lv_label_create(ah_start_btn);
  lv_label_set_text(stxt, "START");
  lv_obj_set_style_text_color(stxt, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(stxt);
  lv_obj_add_flag(ah_start_btn, LV_OBJ_FLAG_HIDDEN);
  ah_bar = lv_bar_create(lv_scr_act());
  lv_obj_set_size(ah_bar, 200, 12);
  lv_obj_align(ah_bar, LV_ALIGN_TOP_MID, 0, 138);
  lv_obj_set_style_radius(ah_bar, 6, 0);
  lv_obj_set_style_bg_color(ah_bar, lv_color_hex(C_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ah_bar, lv_color_hex(C_RED), LV_PART_INDICATOR);
  lv_obj_set_style_radius(ah_bar, 6, LV_PART_INDICATOR);
  lv_bar_set_range(ah_bar, 0, 100);
  lv_bar_set_value(ah_bar, 0, LV_ANIM_OFF);
  ah_info = lv_label_create(lv_scr_act());
  lv_obj_align(ah_info, LV_ALIGN_TOP_MID, 0, 156);
  lv_obj_set_style_text_color(ah_info, lv_color_hex(C_TEXT), 0);
  ah_temp_box = lv_obj_create(lv_scr_act());
  lv_obj_set_size(ah_temp_box, 220, 44);
  lv_obj_align(ah_temp_box, LV_ALIGN_TOP_MID, 0, 180);
  lv_obj_set_style_bg_opa(ah_temp_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ah_temp_box, 0, 0);
  lv_obj_set_style_pad_all(ah_temp_box, 0, 0);
  lv_obj_clear_flag(ah_temp_box, LV_OBJ_FLAG_SCROLLABLE);
  const uint32_t dot_c[6] = {0xD32F2F,0xE53935,0xEF5350,0xE57373,0x78909C,0xB0BEC5};
  for (int i = 0; i < 6; i++) {
    int col = i % 3, row = i / 3;
    int x = 8 + col * 72, y = 6 + row * 22;
    ah_temp_dot[i] = lv_obj_create(ah_temp_box);
    lv_obj_set_size(ah_temp_dot[i], 8, 8);
    lv_obj_set_pos(ah_temp_dot[i], x, y + 4);
    lv_obj_set_style_radius(ah_temp_dot[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ah_temp_dot[i], lv_color_hex(dot_c[i]), 0);
    lv_obj_set_style_border_width(ah_temp_dot[i], 0, 0);
    lv_obj_clear_flag(ah_temp_dot[i], LV_OBJ_FLAG_SCROLLABLE);
    ah_temp_lbl[i] = lv_label_create(ah_temp_box);
    lv_obj_set_pos(ah_temp_lbl[i], x + 12, y);
    lv_obj_set_style_text_color(ah_temp_lbl[i], lv_color_hex(C_TEXT), 0);
    lv_label_set_text(ah_temp_lbl[i], "--");
  }
  ah_btn_box = lv_obj_create(lv_scr_act());
  lv_obj_set_size(ah_btn_box, 220, 40);
  lv_obj_align(ah_btn_box, LV_ALIGN_TOP_MID, 0, 229);
  lv_obj_set_style_bg_opa(ah_btn_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ah_btn_box, 0, 0);
  lv_obj_set_style_pad_all(ah_btn_box, 0, 0);
  lv_obj_clear_flag(ah_btn_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* pb = lv_btn_create(ah_btn_box);
  lv_obj_set_size(pb, 105, 36);
  lv_obj_set_pos(pb, 0, 2);
  lv_obj_set_style_bg_color(pb, lv_color_hex(C_RED), 0);
  lv_obj_add_event_cb(pb, ah_on_pause, LV_EVENT_CLICKED, NULL);
  ah_pause_lbl = lv_label_create(pb);
  lv_label_set_text(ah_pause_lbl, "Pause");
  lv_obj_set_style_text_color(ah_pause_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(ah_pause_lbl);
  lv_obj_t* sb = lv_btn_create(ah_btn_box);
  lv_obj_set_size(sb, 105, 36);
  lv_obj_set_pos(sb, 115, 2);
  lv_obj_set_style_bg_color(sb, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(sb, lv_color_hex(C_RED), 0);
  lv_obj_set_style_border_width(sb, 2, 0);
  lv_obj_add_event_cb(sb, ah_on_stop, LV_EVENT_CLICKED, NULL);
  lv_obj_t* sl = lv_label_create(sb);
  lv_label_set_text(sl, "Stop");
  lv_obj_set_style_text_color(sl, lv_color_hex(C_RED), 0);
  lv_obj_center(sl);
  ah_refresh();
}

void audi_home_init(void) {
  ah_show_home();
  lv_msg_subscribe(DATA_PRINTER_DATA, ah_on_msg, NULL);
  if (ah_anim_timer) lv_timer_del(ah_anim_timer);
  ah_anim_timer = lv_timer_create(ah_anim_cb, 650, NULL);
}
