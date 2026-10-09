#pragma once
#include <Arduino.h>
#include <WiFi.h>

// Tu dong quet mang tim Moonraker (port 7125) khi mat ket noi lau
// Logic: connecting > 10 phut -> quet 2 lan -> doi 10 phut -> lap lai

#define AUTOSCAN_PORT 7125
#define AUTOSCAN_CONNECT_TIMEOUT_MS (3UL * 60UL * 1000UL)  // 3 phut
#define AUTOSCAN_RETRY_WAIT_MS (3UL * 60UL * 1000UL)  // 3 phut
#define AUTOSCAN_MAX_TRIES 2
#define AUTOSCAN_TCP_TIMEOUT_MS 300  // timeout moi IP khi quet

// Callback khi tim thay printer: nhan IP dang string
typedef void (*autoscan_found_cb_t)(const char* ip_str);

void autoscan_init(autoscan_found_cb_t cb);
void autoscan_notify_state(bool is_connecting);
void autoscan_loop();
void autoscan_trigger_now();  // Nguoi dung bam nut Find -> quet ngay
