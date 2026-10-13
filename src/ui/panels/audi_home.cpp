#include <Arduino.h>
#include "lvgl.h"
#include "../../core/printer_integration.hpp"
#include "../../core/current_printer.h"
#include "printer_anim.h"
#include "anim_new_states.h"
#include "anim_paused.h"
#include "printer_autoscan.h"
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
static lv_obj_t* ah_ctrl_box;
static lv_obj_t* ah_ctrl_val[4];
static lv_obj_t* ah_ctrl_badge_num;
static lv_obj_t* ah_badge_parts[3];
static lv_obj_t* ah_fil_box;
static lv_obj_t* ah_fil_circle[4];
static lv_obj_t* ah_fil_num[4];
static lv_obj_t* ah_fil_type[4];
static lv_obj_t* ah_fil_slash[4];  // gach cheo ve khi slot trong (het nhua)
static int ah_active_dot = -1;
static bool ah_bg_dark = false;
static lv_obj_t* ah_btn_box;
static lv_obj_t* ah_pause_lbl;
static lv_obj_t* ah_find_btn = NULL;
static lv_timer_t* ah_anim_timer = NULL;
static int ah_phase = 0;

// Chuan hoa mau nhua ve bang mau chuan theo sac mau (hue), giong cach
// man hinh zin cua may in hien thi (vd hong magenta -> do, cyan nhat
// giu dung cyan chu khong bi keo ve vang nhu cach tinh theo do sang).
// Chuan hoa mau nhua: so khop mau GAN NHAT voi bang 22 mau goc tren man
// hinh U1 (man "Filament Details"). Bang mau duoc doc truc tiep tu anh chup
// man zin (2026-10-13), hieu chinh can bang trang theo vong mau trang.
// Chuan hon han cach doan theo hue cu (cyan nhat tung bi keo ve vang,
// do hong bi keo ve do tuoi...). Khoang cach mau co trong so (redmean).
struct AhPalColor { unsigned char r, g, b; };
static const AhPalColor AH_U1_PALETTE[] = {
  {0x02, 0x08, 0x12},  // den
  {0xF5, 0xF5, 0xF5},  // trang
  {0xE7, 0xF3, 0xF2},  // xanh nhat
  {0xB4, 0xCC, 0xDF},  // xanh xam nhat
  {0xD4, 0x9A, 0x6B},  // nau
  {0xFF, 0xEF, 0xA4},  // vang nhat
  {0xFF, 0xDA, 0x3F},  // vang gold
  {0xFF, 0xF8, 0x45},  // vang tuoi
  {0xD6, 0xEB, 0xBC},  // xanh mint
  {0x72, 0xE9, 0x51},  // xanh la sang
  {0x57, 0xB6, 0x48},  // xanh la
  {0x6B, 0xE3, 0xF2},  // xanh da troi
  {0x6D, 0xEF, 0xE2},  // cyan
  {0x1F, 0x6A, 0xF7},  // xanh duong
  {0x15, 0x4D, 0xCD},  // xanh duong dam
  {0x5F, 0x65, 0xFA},  // chi
  {0x93, 0x1E, 0xF1},  // tim
  {0xFD, 0x3C, 0x63},  // do hong
  {0xFF, 0x6F, 0x82},  // hong
  {0xF9, 0x48, 0x2D},  // do
  {0xFF, 0xA2, 0x2B},  // cam
  {0xFF, 0x62, 0x20},  // cam do
};
static unsigned int ah_snap_color(unsigned int c) {
  int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  unsigned int best = 0x808080;
  long best_d = 0x7FFFFFFFL;
  for (unsigned i = 0; i < sizeof(AH_U1_PALETTE) / sizeof(AH_U1_PALETTE[0]); i++) {
    int dr = r - AH_U1_PALETTE[i].r;
    int dg = g - AH_U1_PALETTE[i].g;
    int db = b - AH_U1_PALETTE[i].b;
    int rm = (r + AH_U1_PALETTE[i].r) / 2;
    long d = (long)(2 + rm / 128) * dr * dr + 4L * dg * dg +
             (long)(2 + (255 - rm) / 128) * db * db;
    if (d < best_d) {
      best_d = d;
      best = ((unsigned int)AH_U1_PALETTE[i].r << 16) |
             ((unsigned int)AH_U1_PALETTE[i].g << 8) | AH_U1_PALETTE[i].b;
    }
  }
  return best;
}

static void ah_show_frame(void) {
  const uint16_t* src = NULL;
  if (ah_anim_kind == 0) src = anim_printing[ah_anim_frame];
  else if (ah_anim_kind == 1) src = anim_error[ah_anim_frame];
  else if (ah_anim_kind == 2) src = anim_sleeping[ah_anim_frame];
  else if (ah_anim_kind == 3) src = anim_preparing[ah_anim_frame];
  else if (ah_anim_kind == 4) src = anim_complete[ah_anim_frame];
  else if (ah_anim_kind == 5) src = anim_paused[ah_anim_frame];
  if (!src) return;
  ah_img_dsc.data = (uint8_t*)src;
  lv_img_set_src(ah_anim_img, &ah_img_dsc);
}

static void ah_anim_cb(lv_timer_t* t) {
  (void)t;
  PrinterState st = get_current_printer_data()->state;
  PrinterData* pd = get_current_printer_data();

  // Phat hien dang chuan bi: dang in nhung progress ~0 va nhiet chua dat target
  bool is_preparing = false;
  if (st == PrinterState::PrinterStatePrinting) {
    float prog = pd->print_progress;
    if (prog < 0.01) {
      for (int i = 0; i < 4; i++) {
        if (pd->target_temperatures[i+1] > 50 &&
            pd->temperatures[i+1] < pd->target_temperatures[i+1] - 5) {
          is_preparing = true; break;
        }
      }
      if (!is_preparing && pd->target_temperatures[0] > 50 &&
          pd->temperatures[0] < pd->target_temperatures[0] - 5)
        is_preparing = true;
    }
  }

  // Phat hien vua in xong: chuyen tu Printing/Paused sang Idle
  static PrinterState prev_st = PrinterState::PrinterStateOffline;
  static bool show_complete = false;
  static unsigned long complete_start_ms = 0;
  if ((prev_st == PrinterState::PrinterStatePrinting ||
       prev_st == PrinterState::PrinterStatePaused) &&
      st == PrinterState::PrinterStateIdle) {
    show_complete = true;
  }
  if (st != PrinterState::PrinterStateIdle) {
    show_complete = false;
    complete_start_ms = 0;
  }
  // Sau 15 phut o man hinh complete thi ve idle
  if (show_complete) {
    if (complete_start_ms == 0) complete_start_ms = millis();
    else if (millis() - complete_start_ms > 15UL * 60UL * 1000UL) {
      show_complete = false;
      complete_start_ms = 0;
    }
  }
  prev_st = st;

  // Chon GIF: 0=in, 1=loi, 2=ngu, 3=chuan bi, 4=hoan thanh, 5=tam dung
  int kind;
  if (show_complete) kind = 4;
  else if (is_preparing) kind = 3;
  else if (st == PrinterState::PrinterStatePaused) kind = 5;
  else if (st == PrinterState::PrinterStatePrinting) kind = 0;
  else if (st == PrinterState::PrinterStateError) kind = 1;
  else kind = 2;

  if (kind != ah_anim_kind) {
    ah_anim_kind = kind;
    ah_anim_frame = 0;
    if (kind == 2) {
      ah_bg_dark = true;
      lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
      lv_obj_set_style_text_color(ah_status, lv_color_hex(0xB0B0B0), 0);
      lv_obj_set_style_text_color(ah_printer_name, lv_color_hex(0xFFFFFF), 0);
    } else {
      ah_bg_dark = false;
      lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_text_color(ah_status, lv_color_hex(0x757575), 0);
      lv_obj_set_style_text_color(ah_printer_name, lv_color_hex(0x212121), 0);
    }
  }
  if (ah_anim_img) {
    ah_show_frame();
    int nframes = (kind == 0) ? ANIM_PRINTING_FRAMES :
                  (kind == 1) ? ANIM_ERROR_FRAMES :
                  (kind == 3) ? ANIM_PREPARING_FRAMES :
                  (kind == 4) ? ANIM_COMPLETE_FRAMES :
                  (kind == 5) ? ANIM_PAUSED_FRAMES : ANIM_SLEEPING_FRAMES;
    ah_anim_frame = (ah_anim_frame + 1) % nframes;
  }

  // Cap nhat trang thai text
  const char* txt = "";
  switch (st) {
    case PrinterState::PrinterStatePrinting: txt = "Printing"; break;
    case PrinterState::PrinterStatePaused: txt = "Paused"; break;
    case PrinterState::PrinterStateIdle: txt = "Ready"; break;
    case PrinterState::PrinterStateOffline: txt = "Connecting..."; break;
    case PrinterState::PrinterStateError: txt = "Printer Error"; break;
    default: break;
  }
  if (kind == 3) txt = "Preparing...";
  else if (kind == 4) txt = "Print Complete!";
  lv_label_set_text(ah_status, txt);

  // Auto-scan printer khi mat ket noi
  bool is_connecting = (st == PrinterState::PrinterStateOffline);
  autoscan_notify_state(is_connecting);
  autoscan_loop();
}

static lv_obj_t* ah_confirm_box = NULL;

static void ah_close_confirm(lv_event_t* e) {
  (void)e;
  if (ah_confirm_box) { lv_obj_del(ah_confirm_box); ah_confirm_box = NULL; }
}

static void ah_show_confirm(const char* msg, void (*on_yes)(lv_event_t*)) {
  if (ah_confirm_box) lv_obj_del(ah_confirm_box);
  ah_confirm_box = lv_obj_create(lv_scr_act());
  lv_obj_set_size(ah_confirm_box, 200, 140);
  lv_obj_center(ah_confirm_box);
  lv_obj_set_style_bg_color(ah_confirm_box, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(ah_confirm_box, 12, 0);
  lv_obj_t* lbl = lv_label_create(ah_confirm_box);
  lv_label_set_text(lbl, msg);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t* btn_yes = lv_btn_create(ah_confirm_box);
  lv_obj_set_size(btn_yes, 80, 40);
  lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_set_style_bg_color(btn_yes, lv_color_hex(0x4CAF50), 0);
  lv_obj_t* l1 = lv_label_create(btn_yes);
  lv_label_set_text(l1, "Yes"); lv_obj_center(l1);
  lv_obj_add_event_cb(btn_yes, on_yes, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(btn_yes, ah_close_confirm, LV_EVENT_CLICKED, NULL);
  lv_obj_t* btn_no = lv_btn_create(ah_confirm_box);
  lv_obj_set_size(btn_no, 80, 40);
  lv_obj_align(btn_no, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x9E9E9E), 0);
  lv_obj_t* l2 = lv_label_create(btn_no);
  lv_label_set_text(l2, "Cancel"); lv_obj_center(l2);
  lv_obj_add_event_cb(btn_no, ah_close_confirm, LV_EVENT_CLICKED, NULL);
}

static void ah_do_pause(lv_event_t* e) {
  (void)e;
  PrinterState st = get_current_printer_data()->state;
  if (st == PrinterState::PrinterStatePrinting)
    current_printer_execute_feature(PrinterFeatures::PrinterFeaturePause);
  else if (st == PrinterState::PrinterStatePaused)
    current_printer_execute_feature(PrinterFeatures::PrinterFeatureResume);
}

static void ah_do_stop(lv_event_t* e) {
  (void)e;
  current_printer_execute_feature(PrinterFeatures::PrinterFeatureStop);
}

static void ah_on_pause(lv_event_t* e) {
  (void)e;
  PrinterState st = get_current_printer_data()->state;
  const char* msg = (st == PrinterState::PrinterStatePaused)
    ? "Resume print?" : "Pause print?";
  ah_show_confirm(msg, ah_do_pause);
}

static void ah_on_stop(lv_event_t* e) {
  (void)e;
  ah_show_confirm("Stop print?", ah_do_stop);
}

static void ah_show_home(void);
static void ah_on_start(lv_event_t* e);
static void ah_on_find(lv_event_t* e);

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
  // Danh sach file cua stock dung mau chu theo theme (trang) nen tren nen
  // trang se khong thay ten file: ep chu trong danh sach sang mau den.
  // List cung dung kich thuoc ngang (320) nen bi cuon trai/phai: ep ve
  // dung khung doc 230x220 va chi cho cuon doc.
  lv_obj_set_style_text_color(list_holder, lv_color_hex(0x000000), 0);
  lv_obj_t* lst = lv_obj_get_child(list_holder, 0);
  if (lst) {
    if (lv_obj_get_child_cnt(lst) > 0) {
      lv_obj_set_size(lst, 230, 220);
      lv_obj_set_scroll_dir(lst, LV_DIR_VER);
    }
    uint32_t n = lv_obj_get_child_cnt(lst);
    for (uint32_t i = 0; i < n; i++) {
      lv_obj_t* row = lv_obj_get_child(lst, i);
      lv_obj_set_style_text_color(row, lv_color_hex(0x000000), 0);
      uint32_t m = lv_obj_get_child_cnt(row);
      for (uint32_t j = 0; j < m; j++)
        lv_obj_set_style_text_color(lv_obj_get_child(row, j),
                                    lv_color_hex(0x000000), 0);
    }
  }
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
  if (ah_find_btn) {
    bool offline = (st == PrinterState::PrinterStateOffline);
    if (offline) lv_obj_clear_flag(ah_find_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ah_find_btn, LV_OBJ_FLAG_HIDDEN);
  }
  if (ah_bar) {
    if (printing) {
      lv_obj_clear_flag(ah_bar, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ah_info, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ah_ctrl_box, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ah_btn_box, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ah_bar, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ah_info, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ah_ctrl_box, LV_OBJ_FLAG_HIDDEN);
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
    // Extruder dang chay: dau phun co target cao nhat.
    // Neu khong dau nao dang gia nhiet thi lay dau nong nhat.
    ah_active_dot = -1;
    float max_t = 50.0f;
    for (int i = 0; i < 4; i++) {
      if (d->target_temperatures[i+1] > max_t) {
        max_t = d->target_temperatures[i+1];
        ah_active_dot = i;
      }
    }
    if (ah_active_dot < 0) {
      float best = -1.0f;
      for (int i = 0; i < 4; i++) {
        if (d->temperatures[i+1] > best) {
          best = d->temperatures[i+1];
          ah_active_dot = i;
        }
      }
    }
    // Hang Control 4 cot: [extruder dang in] [bed] [buong] [nhua da in - gram]
    char vbuf[16];
    if (ah_ctrl_badge_num) {
      snprintf(vbuf, sizeof(vbuf), "%d", ah_active_dot + 1);
      lv_label_set_text(ah_ctrl_badge_num, vbuf);
      // Icon dau phun to mau theo mau nhua (da chuan hoa) cua slot dang
      // in, khop voi vong tron o hang filament phia duoi
      unsigned int fc = ah_snap_color(d->filament_colors[ah_active_dot]);
      int lum = (int)(((fc >> 16) & 0xFF) * 299 + ((fc >> 8) & 0xFF) * 587 +
                      (fc & 0xFF) * 114) / 1000;
      for (int i = 0; i < 3; i++) {
        if (!ah_badge_parts[i]) continue;
        lv_obj_set_style_bg_color(ah_badge_parts[i], lv_color_hex(fc), 0);
        // Mau sang (trang, vang nhat...) thi them vien den de khoi bien
        // mat tren nen trang; mau dam thi khong can vien
        lv_obj_set_style_border_color(ah_badge_parts[i], lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_border_width(ah_badge_parts[i],
            lum > 150 ? (i == 0 ? 2 : 1) : 0, 0);
      }
      lv_obj_set_style_text_color(ah_ctrl_badge_num,
          lv_color_hex(lum > 150 ? 0x212121 : 0xFFFFFF), 0);
    }
    if (ah_ctrl_val[0]) {
      snprintf(vbuf, sizeof(vbuf), "%.0f°C", d->temperatures[ah_active_dot + 1]);
      lv_label_set_text(ah_ctrl_val[0], vbuf);
    }
    if (ah_ctrl_val[1]) {
      snprintf(vbuf, sizeof(vbuf), "%.0f°C", d->temperatures[0]);
      lv_label_set_text(ah_ctrl_val[1], vbuf);
    }
    if (ah_ctrl_val[2]) {
      snprintf(vbuf, sizeof(vbuf), "%.0f°C", d->temperatures[9]);
      lv_label_set_text(ah_ctrl_val[2], vbuf);
    }
    if (ah_ctrl_val[3]) {
      // Klipper chi cho mm; quy doi gram theo PLA 1.75mm (~2.98 g/m)
      float grams = d->filament_used_mm / 1000.0f * 2.98f;
      snprintf(vbuf, sizeof(vbuf), "%.0fg", grams);
      lv_label_set_text(ah_ctrl_val[3], vbuf);
    }
    lv_label_set_text(ah_pause_lbl,
      st == PrinterState::PrinterStatePrinting ? "Pause" : "Resume");
  }

  // Hang filament: chi hien khi may co du lieu filament (Snapmaker U1)
  // va khong o trang thai offline (tranh chong len nut Find Printer)
  if (ah_fil_box) {
    bool show_fil = d->filament_data_valid &&
                    st != PrinterState::PrinterStateOffline;
    if (show_fil) lv_obj_clear_flag(ah_fil_box, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ah_fil_box, LV_OBJ_FLAG_HIDDEN);
    if (show_fil) {
      // - Khong co du lieu: vong xam + "--" (nhu cu)
      // - Slot trong (het nhua): vong trong suot + gach cheo + "Empty"
      // - Co nhua: vong mau (so khop palette U1) + so slot + loai nhua
      for (int i = 0; i < 4; i++) {
        if (d->filament_types[i][0] == '\0') {
          lv_obj_set_style_bg_opa(ah_fil_circle[i], LV_OPA_COVER, 0);
          lv_obj_set_style_bg_color(ah_fil_circle[i], lv_color_hex(0xE0E0E0), 0);
          lv_obj_set_style_border_color(ah_fil_circle[i], lv_color_hex(C_TEXT), 0);
          lv_obj_set_style_text_color(ah_fil_num[i], lv_color_hex(0x9E9E9E), 0);
          lv_obj_clear_flag(ah_fil_num[i], LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(ah_fil_slash[i], LV_OBJ_FLAG_HIDDEN);
          lv_obj_set_style_text_color(ah_fil_type[i], lv_color_hex(0x9E9E9E), 0);
          lv_label_set_text(ah_fil_type[i], "--");
        } else if (!d->filament_exist[i]) {
          lv_obj_set_style_bg_opa(ah_fil_circle[i], LV_OPA_TRANSP, 0);
          lv_obj_set_style_border_color(ah_fil_circle[i], lv_color_hex(0x9E9E9E), 0);
          lv_obj_add_flag(ah_fil_num[i], LV_OBJ_FLAG_HIDDEN);
          lv_obj_clear_flag(ah_fil_slash[i], LV_OBJ_FLAG_HIDDEN);
          lv_obj_set_style_text_color(ah_fil_type[i], lv_color_hex(0x9E9E9E), 0);
          lv_label_set_text(ah_fil_type[i], "Empty");
        } else {
          unsigned int c = ah_snap_color(d->filament_colors[i]);
          lv_obj_set_style_bg_opa(ah_fil_circle[i], LV_OPA_COVER, 0);
          lv_obj_set_style_bg_color(ah_fil_circle[i], lv_color_hex(c), 0);
          lv_obj_set_style_border_color(ah_fil_circle[i], lv_color_hex(C_TEXT), 0);
          // Nen sang thi so mau den, nen toi thi so mau trang cho de doc
          int lum = (int)(((c >> 16) & 0xFF) * 299 + ((c >> 8) & 0xFF) * 587 +
                          (c & 0xFF) * 114) / 1000;
          lv_obj_set_style_text_color(ah_fil_num[i],
              lv_color_hex(lum > 150 ? 0x212121 : 0xFFFFFF), 0);
          lv_obj_clear_flag(ah_fil_num[i], LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(ah_fil_slash[i], LV_OBJ_FLAG_HIDDEN);
          // Chu loai nhua: mau chu giao dien binh thuong (den tren nen
          // sang, trang tren nen den) - khong theo mau nhua
          lv_obj_set_style_text_color(ah_fil_type[i],
              lv_color_hex(ah_bg_dark ? 0xFFFFFF : C_TEXT), 0);
          lv_label_set_text(ah_fil_type[i], d->filament_types[i]);
        }
      }
    }
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
  lv_img_set_zoom(ah_anim_img, 282);
  lv_img_set_src(ah_anim_img, &ah_img_dsc);
  ah_anim_kind = -1;
  ah_anim_frame = 0;
  ah_printer_name = lv_label_create(lv_scr_act());
  const char* pname = "Printer";
  if (get_current_printer() && get_current_printer()->printer_config)
    pname = get_current_printer()->printer_config->printer_name;
  if (!pname || !pname[0]) pname = "Printer";
  lv_label_set_text(ah_printer_name, pname);
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
  // Nut Find Printer (hien khi offline)
  ah_find_btn = lv_btn_create(lv_scr_act());
  lv_obj_set_size(ah_find_btn, 200, 50);
  lv_obj_align(ah_find_btn, LV_ALIGN_TOP_MID, 0, 200);
  lv_obj_set_style_bg_color(ah_find_btn, lv_color_hex(0x2196F3), 0);
  lv_obj_set_style_radius(ah_find_btn, 10, 0);
  lv_obj_add_event_cb(ah_find_btn, ah_on_find, LV_EVENT_CLICKED, NULL);
  lv_obj_t* ftxt = lv_label_create(ah_find_btn);
  lv_label_set_text(ftxt, "Find Printer");
  lv_obj_set_style_text_color(ftxt, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(ftxt);
  lv_obj_add_flag(ah_find_btn, LV_OBJ_FLAG_HIDDEN);
  ah_bar = lv_bar_create(lv_scr_act());
  lv_obj_set_size(ah_bar, 200, 12);
  lv_obj_align(ah_bar, LV_ALIGN_TOP_MID, 0, 132);
  lv_obj_set_style_radius(ah_bar, 6, 0);
  lv_obj_set_style_bg_color(ah_bar, lv_color_hex(C_TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ah_bar, lv_color_hex(C_RED), LV_PART_INDICATOR);
  lv_obj_set_style_radius(ah_bar, 6, LV_PART_INDICATOR);
  lv_bar_set_range(ah_bar, 0, 100);
  lv_bar_set_value(ah_bar, 0, LV_ANIM_OFF);
  ah_info = lv_label_create(lv_scr_act());
  lv_obj_align(ah_info, LV_ALIGN_TOP_MID, 0, 148);
  lv_obj_set_style_text_color(ah_info, lv_color_hex(C_TEXT), 0);
  // Hang Control 4 cot (chi hien khi dang in / tam dung):
  // [huy hieu so extruder] [ban nhiet] [buong in] [cuon nhua - gram]
  ah_ctrl_box = lv_obj_create(lv_scr_act());
  lv_obj_set_size(ah_ctrl_box, 220, 50);
  lv_obj_align(ah_ctrl_box, LV_ALIGN_TOP_MID, 0, 164);
  lv_obj_set_style_bg_opa(ah_ctrl_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ah_ctrl_box, 0, 0);
  lv_obj_set_style_pad_all(ah_ctrl_box, 0, 0);
  lv_obj_clear_flag(ah_ctrl_box, LV_OBJ_FLAG_SCROLLABLE);

  // Cot 1: icon dau phun (khoi + mui nhon dan xuong), so extruder dang
  // chay nam trong khoi, khoi to mau theo mau nhua cua slot dang in
  lv_obj_t* badge = lv_obj_create(ah_ctrl_box);
  lv_obj_set_size(badge, 22, 18);
  lv_obj_set_pos(badge, 18, 0);
  lv_obj_set_style_radius(badge, 5, 0);
  lv_obj_set_style_bg_color(badge, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_border_width(badge, 0, 0);
  lv_obj_set_style_pad_all(badge, 0, 0);
  lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  ah_badge_parts[0] = badge;
  ah_ctrl_badge_num = lv_label_create(badge);
  lv_label_set_text(ah_ctrl_badge_num, "1");
  lv_obj_set_style_text_color(ah_ctrl_badge_num, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(ah_ctrl_badge_num);
  // Mui nhon cua dau phun: 2 bac nho dan o duoi khoi
  lv_obj_t* tip1 = lv_obj_create(ah_ctrl_box);
  lv_obj_set_size(tip1, 12, 4);
  lv_obj_set_pos(tip1, 23, 18);
  lv_obj_set_style_bg_color(tip1, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_border_width(tip1, 0, 0);
  lv_obj_set_style_pad_all(tip1, 0, 0);
  lv_obj_clear_flag(tip1, LV_OBJ_FLAG_SCROLLABLE);
  ah_badge_parts[1] = tip1;
  lv_obj_t* tip2 = lv_obj_create(ah_ctrl_box);
  lv_obj_set_size(tip2, 6, 4);
  lv_obj_set_pos(tip2, 26, 22);
  lv_obj_set_style_bg_color(tip2, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_border_width(tip2, 0, 0);
  lv_obj_set_style_pad_all(tip2, 0, 0);
  lv_obj_clear_flag(tip2, LV_OBJ_FLAG_SCROLLABLE);
  ah_badge_parts[2] = tip2;

  // Cot 2: icon ban nhiet (tam phang + 3 vach nhiet phia tren)
  lv_obj_t* bed = lv_obj_create(ah_ctrl_box);
  lv_obj_set_size(bed, 28, 9);
  lv_obj_set_pos(bed, 70, 17);
  lv_obj_set_style_radius(bed, 3, 0);
  lv_obj_set_style_bg_opa(bed, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(bed, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_border_width(bed, 2, 0);
  lv_obj_set_style_pad_all(bed, 0, 0);
  lv_obj_clear_flag(bed, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < 3; i++) {
    lv_obj_t* w = lv_obj_create(ah_ctrl_box);
    lv_obj_set_size(w, 2, 7);
    lv_obj_set_pos(w, 74 + i * 8, 6);
    lv_obj_set_style_bg_color(w, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_border_width(w, 0, 0);
    lv_obj_set_style_pad_all(w, 0, 0);
    lv_obj_clear_flag(w, LV_OBJ_FLAG_SCROLLABLE);
  }

  // Cot 3: icon buong in (hop vuong rong)
  lv_obj_t* chm = lv_obj_create(ah_ctrl_box);
  lv_obj_set_size(chm, 22, 22);
  lv_obj_set_pos(chm, 128, 3);
  lv_obj_set_style_radius(chm, 3, 0);
  lv_obj_set_style_bg_opa(chm, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(chm, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_border_width(chm, 2, 0);
  lv_obj_set_style_pad_all(chm, 0, 0);
  lv_obj_clear_flag(chm, LV_OBJ_FLAG_SCROLLABLE);

  // Cot 4: icon cuon nhua (vong tron ngoai + truc giua)
  lv_obj_t* spool = lv_obj_create(ah_ctrl_box);
  lv_obj_set_size(spool, 24, 24);
  lv_obj_set_pos(spool, 182, 2);
  lv_obj_set_style_radius(spool, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(spool, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(spool, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_border_width(spool, 2, 0);
  lv_obj_set_style_pad_all(spool, 0, 0);
  lv_obj_clear_flag(spool, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* hub = lv_obj_create(ah_ctrl_box);
  lv_obj_set_size(hub, 8, 8);
  lv_obj_set_pos(hub, 190, 10);
  lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(hub, lv_color_hex(C_TEXT), 0);
  lv_obj_set_style_border_width(hub, 0, 0);
  lv_obj_set_style_pad_all(hub, 0, 0);
  lv_obj_clear_flag(hub, LV_OBJ_FLAG_SCROLLABLE);

  // Hang so nam duoi cac icon
  for (int i = 0; i < 4; i++) {
    ah_ctrl_val[i] = lv_label_create(ah_ctrl_box);
    lv_obj_set_width(ah_ctrl_val[i], 55);
    lv_obj_set_style_text_align(ah_ctrl_val[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ah_ctrl_val[i], 2 + i * 55, 30);
    lv_obj_set_style_text_color(ah_ctrl_val[i], lv_color_hex(C_TEXT), 0);
    lv_label_set_text(ah_ctrl_val[i], "--");
  }
  ah_btn_box = lv_obj_create(lv_scr_act());
  lv_obj_set_size(ah_btn_box, 220, 40);
  lv_obj_align(ah_btn_box, LV_ALIGN_TOP_MID, 0, 272);
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
  // Hang filament 4 slot: vong tron mau + so slot o giua + loai nhua o duoi
  // (chi hien khi may co du lieu filament - xem ah_refresh)
  ah_fil_box = lv_obj_create(lv_scr_act());
  lv_obj_set_size(ah_fil_box, 220, 46);
  lv_obj_align(ah_fil_box, LV_ALIGN_TOP_MID, 0, 218);
  lv_obj_set_style_bg_opa(ah_fil_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ah_fil_box, 0, 0);
  lv_obj_set_style_pad_all(ah_fil_box, 0, 0);
  lv_obj_clear_flag(ah_fil_box, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < 4; i++) {
    ah_fil_circle[i] = lv_obj_create(ah_fil_box);
    lv_obj_set_size(ah_fil_circle[i], 26, 26);
    lv_obj_set_pos(ah_fil_circle[i], 16 + i * 55, 2);
    lv_obj_set_style_radius(ah_fil_circle[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ah_fil_circle[i], lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_color(ah_fil_circle[i], lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_border_width(ah_fil_circle[i], 2, 0);
    lv_obj_set_style_pad_all(ah_fil_circle[i], 0, 0);
    lv_obj_clear_flag(ah_fil_circle[i], LV_OBJ_FLAG_SCROLLABLE);
    ah_fil_num[i] = lv_label_create(ah_fil_circle[i]);
    char nbuf[4];
    snprintf(nbuf, sizeof(nbuf), "%d", i + 1);
    lv_label_set_text(ah_fil_num[i], nbuf);
    lv_obj_set_style_text_color(ah_fil_num[i], lv_color_hex(C_TEXT), 0);
    lv_obj_center(ah_fil_num[i]);
    // Gach cheo nam trong vong tron, chi hien khi slot trong (het nhua)
    ah_fil_slash[i] = lv_line_create(ah_fil_circle[i]);
    lv_obj_set_size(ah_fil_slash[i], 18, 18);
    lv_obj_center(ah_fil_slash[i]);
    static const lv_point_t slash_pts[2] = {{2, 2}, {16, 16}};
    lv_line_set_points(ah_fil_slash[i], slash_pts, 2);
    lv_obj_set_style_line_color(ah_fil_slash[i], lv_color_hex(0x9E9E9E), 0);
    lv_obj_set_style_line_width(ah_fil_slash[i], 2, 0);
    lv_obj_add_flag(ah_fil_slash[i], LV_OBJ_FLAG_HIDDEN);
    ah_fil_type[i] = lv_label_create(ah_fil_box);
    lv_obj_set_width(ah_fil_type[i], 55);
    lv_obj_set_style_text_align(ah_fil_type[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ah_fil_type[i], 2 + i * 55, 31);
    lv_obj_set_style_text_color(ah_fil_type[i], lv_color_hex(C_TEXT), 0);
    lv_label_set_text(ah_fil_type[i], "--");
  }
  ah_refresh();
}

static void ah_on_find(lv_event_t* e) {
  (void)e;
  autoscan_trigger_now();
}

void audi_home_init(void) {
  ah_show_home();
  lv_msg_subscribe(DATA_PRINTER_DATA, ah_on_msg, NULL);
  if (ah_anim_timer) lv_timer_del(ah_anim_timer);
  ah_anim_timer = lv_timer_create(ah_anim_cb, 650, NULL);
  autoscan_init([](const char* ip_str) {
    if (!get_current_printer() || !get_current_printer()->printer_config) return;
    PrinterConfiguration* cfg = get_current_printer()->printer_config;
    strncpy(cfg->printer_host, ip_str, sizeof(cfg->printer_host) - 1);
    cfg->printer_host[sizeof(cfg->printer_host) - 1] = '\0';
    cfg->ip_configured = true;
  });
}
