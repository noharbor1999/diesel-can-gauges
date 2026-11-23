// =============================================================================
// gauge3_v1 / display.ino  -  ONE sketch for all 3 gauge displays
// Board : Waveshare ESP32-S3-Touch-LCD-2.1 (ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM)
// Panel : 2.1in 480x480 round RGB IPS, ST7701 (3-wire SPI init via TCA9554 IO expander)
// FQBN  : esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc
//
// Pins, ST7701 init sequence, TCA9554 and QMI8658 drivers are the OFFICIAL Waveshare
// demo files (see Display_ST7701.*, TCA9554PWR.*, I2C_Driver.*, Gyro_QMI8658.* headers).
// Touch (CST820) is NOT initialised.  Buzzer = TCA9554 EXIO8 (high = on, per Waveshare
// ESP-IDF demo main/Buzzer/Buzzer.c).
//
// Gauge role: 1 = EGT, 2 = BOOST/EBP, 3 = UTILITY.
//   * compile-time default: -DGAUGE_ROLE_DEFAULT=n  (or edit below)
//   * runtime: type "role 2" on USB serial (115200). Saved in NVS, survives reflash.
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "esp_lcd_panel_rgb.h"
#include "freertos/semphr.h"

#include "I2C_Driver.h"
#include "TCA9554PWR.h"
#include "Display_ST7701.h"
#include "Gyro_QMI8658.h"
#include "g3_packet.h"
#include "ui.h"

#ifndef GAUGE_ROLE_DEFAULT
#define GAUGE_ROLE_DEFAULT 1
#endif
#define FW_VERSION "gauge3_v1 display 1.1"

// IMU axis mapping - UNVERIFIED until the pod is mounted. Use "imu" command to see raw values.
#define IMU_PITCH_SIGN  1.0f
#define IMU_ROLL_SIGN   1.0f
#define BUZZ_ON_MS      200
#define BUZZ_OFF_MS     200
#define FRAME_MIN_MS    33         // cap at ~30 fps

static Preferences prefs;
static UiLocal L;
static G3Packet pkt;                       // latest accepted packet (copy)
static portMUX_TYPE rx_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool rx_new = false;
static G3Packet rx_buf;
static volatile uint32_t rx_ms = 0, rx_count = 0, rx_bad = 0;
static uint32_t last_seq = 0, lost = 0, got = 0;
static bool have_pkt = false;

static uint16_t* fbs[2] = {nullptr, nullptr};
static int back = 1;
static SemaphoreHandle_t vsync_sem;

static bool demo_mode = false;
static int demo_idx = 0;
static uint32_t demo_next = 0;

static float pitch_f = 0, roll_f = 0, pitch0 = 0, roll0 = 0;
static uint8_t last_reset[3], last_zero, last_bright_cnt;
static bool counters_init = false;
static int cur_bright = -1;
static bool buzz_state = false;
static uint32_t hist_next = 0;
static int last_util_page = -1;

// ---------------------------------------------------------------- ESP-NOW
static void on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  if (!g3_check(data, len)) { rx_bad = rx_bad + 1; return; }
  portENTER_CRITICAL(&rx_mux);
  memcpy(&rx_buf, data, sizeof rx_buf);
  rx_new = true;
  portEXIT_CRITICAL(&rx_mux);
  rx_ms = millis(); rx_count = rx_count + 1;
}

static IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t hp = pdFALSE;
  xSemaphoreGiveFromISR(vsync_sem, &hp);
  return hp == pdTRUE;
}

static void buzzer(bool on) {
  if (on == buzz_state) return;
  buzz_state = on;
  Set_EXIO(EXIO_PIN8, on ? High : Low);
}

static void set_toast(const char* s) { strncpy(L.toast, s, sizeof L.toast - 1); L.toast_until = millis() + 1500; }

// ---------------------------------------------------------------- serial
static void help() {
  Serial.println(F("\n" FW_VERSION " commands:"));
  Serial.println(F("  help          this list"));
  Serial.println(F("  info          role, link quality, packet counters"));
  Serial.println(F("  role <1|2|3>  set gauge role (1 EGT, 2 BOOST/EBP, 3 UTILITY), saved in NVS"));
  Serial.println(F("  demo [on|off] cycle built-in preview screens without the hub"));
  Serial.println(F("  buzz          sound buzzer 0.3 s"));
  Serial.println(F("  imu           print raw accelerometer and pitch/roll"));
}
static void serial_poll() {
  static char line[64]; static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n' && n < (int)sizeof line - 1) { line[n++] = c; continue; }
    line[n] = 0; n = 0;
    char* cmd = strtok(line, " "); char* arg = strtok(nullptr, " ");
    if (!cmd) continue;
    if (!strcmp(cmd, "help")) help();
    else if (!strcmp(cmd, "role")) {
      int r = arg ? atoi(arg) : 0;
      if (r < 1 || r > 3) { Serial.println(F("usage: role 1|2|3")); continue; }
      L.role = r; prefs.putUChar("role", r); L.hist_n = 0;
      Serial.printf("role = %d (saved)\n", r);
    } else if (!strcmp(cmd, "info")) {
      uint32_t age = millis() - rx_ms;
      Serial.printf("%s  role=%d  link=%s  last_rx=%lu ms ago  rx=%lu bad=%lu lost=%lu  quality=%.1f%%  bright=%d\n",
                    FW_VERSION, L.role, L.link_ok ? "OK" : "NO SIGNAL", (unsigned long)age, (unsigned long)rx_count,
                    (unsigned long)rx_bad, (unsigned long)lost, L.link_quality, cur_bright);
      Serial.printf("MAC %s  ESP-NOW ch %d  net_id 0x%02X  PSRAM free %u\n", WiFi.macAddress().c_str(), G3_WIFI_CHANNEL, G3_NET_ID, (unsigned)ESP.getFreePsram());
    } else if (!strcmp(cmd, "demo")) {
      demo_mode = !(arg && !strcmp(arg, "off")); demo_next = 0;
      Serial.printf("demo %s\n", demo_mode ? "on" : "off");
    } else if (!strcmp(cmd, "buzz")) { buzzer(true); delay(300); buzzer(false); }
    else if (!strcmp(cmd, "imu")) {
      Serial.printf("accel g: x=%.3f y=%.3f z=%.3f  pitch=%.1f roll=%.1f (zero p=%.1f r=%.1f)\n",
                    Accel.x, Accel.y, Accel.z, L.pitch, L.roll, pitch0, roll0);
    } else Serial.println(F("unknown command - type help"));
  }
}

// ---------------------------------------------------------------- IMU
static void imu_update() {
  static uint32_t next = 0;
  if (millis() < next) return;
  next = millis() + 50;
  getAccelerometer();
  float ax = Accel.x, ay = Accel.y, az = Accel.z;
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  L.imu_ok = mag > 0.5f && mag < 1.5f;
  // gravity mostly along the screen Y axis when the pod is upright
  float pr = IMU_PITCH_SIGN * atan2f(az, sqrtf(ax * ax + ay * ay)) * 57.2958f;
  float rr = IMU_ROLL_SIGN  * atan2f(ax, sqrtf(ay * ay + az * az)) * 57.2958f;
  pitch_f += 0.15f * (pr - pitch_f); roll_f += 0.15f * (rr - roll_f);
  L.pitch = pitch_f - pitch0; L.roll = roll_f - roll0;
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  delay(200);
  prefs.begin("g3disp", false);
  L.role = prefs.getUChar("role", GAUGE_ROLE_DEFAULT);
  if (L.role < 1 || L.role > 3) L.role = 1;
  pitch0 = prefs.getFloat("p0", 0); roll0 = prefs.getFloat("r0", 0);

  // --- board bring-up, order as in the Waveshare LVGL_Arduino demo ---
  I2C_Init();
  TCA9554PWR_Init(0x00);           // all EXIO = outputs
  Set_EXIO(EXIO_PIN8, Low);        // buzzer off
  QMI8658_Init();
  LCD_Init();                      // ST7701 reset + init + RGB panel + backlight (touch skipped)
  vsync_sem = xSemaphoreCreateBinary();
  void *f0 = nullptr, *f1 = nullptr;
  esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &f0, &f1);
  fbs[0] = (uint16_t*)f0; fbs[1] = (uint16_t*)f1;
  esp_lcd_rgb_panel_event_callbacks_t cbs = {};
  cbs.on_vsync = on_vsync;
  esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, nullptr);

  // --- ESP-NOW receiver ---
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(G3_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) Serial.println(F("ESP-NOW init FAILED"));
  esp_now_register_recv_cb(on_recv);

  Set_Backlight(60);
  Serial.printf("%s  role=%d  type 'help'\n", FW_VERSION, L.role);
}

static void handle_packet() {
  if (!rx_new) return;
  portENTER_CRITICAL(&rx_mux);
  memcpy(&pkt, &rx_buf, sizeof pkt); rx_new = false;
  portEXIT_CRITICAL(&rx_mux);
  if (have_pkt) { uint32_t d = pkt.seq - last_seq; if (d > 1 && d < 1000) lost += d - 1; }
  last_seq = pkt.seq; got++; have_pkt = true;
  L.link_quality = 100.f * got / (float)(got + lost);
  if (!counters_init) {
    memcpy(last_reset, pkt.reset_count, 3); last_zero = pkt.imu_zero_count; last_bright_cnt = pkt.bright_count;
    counters_init = true;
  }
  int g = L.role - 1;
  if (pkt.reset_count[g] != last_reset[g]) { set_toast("PEAK RESET"); L.hist_n = 0; }
  memcpy(last_reset, pkt.reset_count, 3);
  if (pkt.bright_count != last_bright_cnt) { char b[24]; snprintf(b, sizeof b, "BRIGHT %u%%", pkt.brightness); set_toast(b); last_bright_cnt = pkt.bright_count; }
  if (pkt.imu_zero_count != last_zero) {
    last_zero = pkt.imu_zero_count;
    if (L.role == 3) { pitch0 = pitch_f; roll0 = roll_f; prefs.putFloat("p0", pitch0); prefs.putFloat("r0", roll0); set_toast("LEVEL SET"); }
  }
  if (pkt.brightness != cur_bright && pkt.brightness >= 1 && pkt.brightness <= 100) { cur_bright = pkt.brightness; Set_Backlight(cur_bright); }
  if (L.role == 3 && pkt.util_page != last_util_page) { last_util_page = pkt.util_page; L.hist_n = 0; }
}

static void render_frame(const G3Packet& p) {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < FRAME_MIN_MS) return;
  last = now;
  L.now_ms = now;
  Canvas cv{fbs[back], 480, 480};
  ui_render(cv, p, L);
  esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 480, 480, fbs[back]);   // fb pointer => buffer swap, no copy
  xSemaphoreTake(vsync_sem, 0);
  xSemaphoreTake(vsync_sem, pdMS_TO_TICKS(40));                         // wait until the swap is live
  back ^= 1;
}

void loop() {
  serial_poll();
  imu_update();
  if (demo_mode) {
    if (millis() >= demo_next) {
      demo_next = millis() + 3000;
      int n = ui_demo_count();
      for (int k = 0; k < n; k++) { demo_idx = (demo_idx + 1) % n; if (ui_demo_role(demo_idx) == L.role || ui_demo_role(demo_idx) == 0) break; }
    }
    static G3Packet dp; UiLocal save = L;
    ui_demo_build(demo_idx, dp, L);
    L.now_ms = millis(); if (L.toast_until) L.toast_until = L.now_ms + 1000;
    if (L.role == 3 && dp.util_page == UP_TILT) { L.pitch = save.pitch; L.roll = save.roll; L.imu_ok = save.imu_ok; }
    render_frame(dp);
    UiLocal keep = L; L = save; (void)keep;
    buzzer(false);
    return;
  }
  handle_packet();
  L.link_ok = have_pkt && (millis() - rx_ms) < G3_LINK_TIMEOUT_MS;
  if (L.link_ok && millis() >= hist_next) {
    hist_next = millis() + 1000;
    bool ok; float v = ui_hist_value(L.role, pkt, &ok);
    if (ok) ui_hist_push(L, v);
  }
  // buzzer: only the gauge that owns the alarm beeps
  uint32_t mask = L.role == 1 ? A_BUZZ_G1 : L.role == 2 ? A_BUZZ_G2 : A_BUZZ_G3;
  bool want = L.link_ok && (pkt.alarms & mask) && !(pkt.flags & F_MUTED);
  buzzer(want && (millis() % (BUZZ_ON_MS + BUZZ_OFF_MS)) < BUZZ_ON_MS);
  render_frame(pkt);
}
