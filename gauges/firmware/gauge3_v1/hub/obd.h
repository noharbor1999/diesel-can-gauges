// obd.h - OBD-II Mode 01 poller on ESP32-S3 TWAI (CAN) + SN65HVD230, 500 kbps, 11-bit.
#pragma once
#include <stdint.h>
#include <Print.h>

enum RawId : uint8_t {
  R_MAP_KPA = 0, R_BARO_KPA, R_EXH_KPA, R_FRP_ACT_KPA, R_FRP_CMD_KPA,
  R_VGT_CMD, R_VGT_ACT, R_LOAD, R_PEDAL, R_IAT_C, R_VOLT, R_RPM,
  R_EB_HP,        // CAN-C broadcast 0x30A byte 7 x 1.32 = exhaust-brake HP (0 = brake not working)
  R_COUNT
};

void obd_begin(int tx_pin, int rx_pin, Print* log);
bool obd_value(RawId id, float* out, uint32_t max_age_ms = 1500);
bool obd_can_ok();             // ECM answered in the last 2 s
bool obd_discovered();         // supported-PID bitmaps read
bool obd_supported(uint8_t pid);
bool obd_ebp_supported();
void obd_request_probe();      // run the full probe in the OBD task and print a report
bool obd_probe_busy();
uint32_t obd_rx_frames();
uint32_t obd_tx_errors();
