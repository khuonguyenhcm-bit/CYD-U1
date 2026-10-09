#include "printer_autoscan.h"
#include <HTTPClient.h>

static autoscan_found_cb_t s_cb = NULL;
static unsigned long s_connecting_since = 0;
static unsigned long s_last_scan = 0;
static int s_tries_left = AUTOSCAN_MAX_TRIES;
static bool s_was_connecting = false;
static bool s_scan_task_running = false;

void autoscan_init(autoscan_found_cb_t cb) {
  s_cb = cb;
}

// Goi moi vong loop voi trang thai hien tai (true = dang connecting/offline)
void autoscan_notify_state(bool is_connecting) {
  unsigned long now = millis();
  if (is_connecting && !s_was_connecting) {
    // Bat dau mat ket noi
    s_connecting_since = now;
    s_tries_left = AUTOSCAN_MAX_TRIES;
    s_last_scan = 0;
  }
  if (!is_connecting) {
    // Da ket noi lai -> reset
    s_connecting_since = 0;
    s_tries_left = AUTOSCAN_MAX_TRIES;
    s_last_scan = 0;
  }
  s_was_connecting = is_connecting;
}

// Kiem tra IP co phai Moonraker khong (GET /server/info)
static bool is_moonraker(const char* ip_str) {
  WiFiClient client;
  client.setTimeout(AUTOSCAN_TCP_TIMEOUT_MS);
  if (!client.connect(ip_str, AUTOSCAN_PORT)) return false;
  client.print(String("GET /server/info HTTP/1.0\r\nHost: ") + ip_str + "\r\n\r\n");
  unsigned long t0 = millis();
  while (!client.available() && millis() - t0 < AUTOSCAN_TCP_TIMEOUT_MS) delay(5);
  if (!client.available()) { client.stop(); return false; }
  String resp = client.readStringUntil('\n');
  client.stop();
  // Moonraker tra ve JSON co "moonraker_version"
  // Doc them de chac chan
  return resp.indexOf("200") >= 0;
}

// Task quet subnet (chay rieng de khong block UI)
static void scan_task(void* param) {
  s_scan_task_running = true;
  IPAddress local = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  uint32_t net = (uint32_t)local & (uint32_t)mask;

  // Chi ho tro /24 (255.255.255.0) - pho bien nhat
  for (int host = 1; host < 255; host++) {
    uint32_t ip_raw = net | host;
    IPAddress ip(ip_raw);
    if (ip == local) continue;
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    if (is_moonraker(ip_str)) {
      if (s_cb) s_cb(ip_str);
      break;
    }
    // Nhuong CPU cho task khac
    vTaskDelay(1);
  }
  s_scan_task_running = false;
  vTaskDelete(NULL);
}

void autoscan_loop() {
  if (!s_was_connecting) return;
  if (s_scan_task_running) return;
  unsigned long now = millis();

  // Chua du 10 phut connecting thi doi
  if (s_connecting_since == 0) return;
  if (now - s_connecting_since < AUTOSCAN_CONNECT_TIMEOUT_MS) return;

  // Het luot thu -> doi 10 phut roi reset
  if (s_tries_left <= 0) {
    if (now - s_last_scan > AUTOSCAN_RETRY_WAIT_MS) {
      s_tries_left = AUTOSCAN_MAX_TRIES;
      s_connecting_since = now;  // reset dem gio
    }
    return;
  }

  // Chay quet
  s_tries_left--;
  s_last_scan = now;
  xTaskCreate(scan_task, "autoscan", 8192, NULL, 1, NULL);
}

// Nguoi dung bam nut Find -> quet ngay lap tuc, khong doi het 3 phut
void autoscan_trigger_now() {
  if (s_scan_task_running) return;
  s_tries_left = AUTOSCAN_MAX_TRIES;
  s_last_scan = millis();
  s_connecting_since = millis();  // reset dem gio
  xTaskCreate(scan_task, "autoscan", 8192, NULL, 1, NULL);
}
