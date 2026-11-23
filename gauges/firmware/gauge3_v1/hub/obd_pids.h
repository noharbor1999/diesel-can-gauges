// obd_pids.h - ALL OBD-II PID definitions used by the hub, in ONE table.
//
// Source: SAE J1979 / J1979-DA Mode 01 as commonly published (Wikipedia "OBD-II PIDs").
// Request:  ID 0x7DF (functional), 11-bit, 500 kbps, data [02 01 PID 00 00 00 00 00]
// Response: ID 0x7E8 (ECM).  Multi-frame (ISO-TP) answers get a Flow Control to 0x7E0.
//
// VERIFY ON THE TRUCK: run "probe" on the hub. It prints which PIDs the 2017 6.7 ECM
// supports and the raw bytes of each, so the scaling below can be checked.
// Multi-sensor PIDs (0x6D 0x70 0x71 0x73 0x87) start with a "support" byte A; bit 0/1
// say which sensor-A fields are present. Layout below = J1979-DA; confirm with raw bytes.
//
// ============================ TODO: CUMMINS MODE 22 ==================================
// If a value is NOT supported in Mode 01 (likely candidates: EBP 0x73, VGT 0x71,
// commanded rail pressure 0x6D), the ECM may still publish it as a manufacturer
// Mode 22 DID. We do NOT know the correct Cummins/FCA Mode 22 DIDs for this ECM and
// we do NOT guess them here. Add them ONLY from a trusted source (for example a
// scan-tool PID list for the 2017 Ram 6.7 CM2350B ECM) in MODE22_TABLE below.
// =====================================================================================
#pragma once
#include <stdint.h>

enum PollClass : uint8_t { POLL_NEVER = 0, POLL_FAST, POLL_SLOW, POLL_ONCE };

struct PidDef {
  uint8_t pid;
  uint8_t bytes;        // data bytes after [41 PID]
  PollClass poll;       // default class (hub picks one source per value after the probe)
  const char* name;
  const char* formula;  // documentation only
  const char* feeds;    // which gauge value it feeds
};

static const PidDef PID_TABLE[] = {
  // pid   bytes poll        name                                  formula                                   feeds
  {0x00, 4, POLL_ONCE, "PIDs supported 01-20",               "bitmap",                                  "-"},
  {0x20, 4, POLL_ONCE, "PIDs supported 21-40",               "bitmap",                                  "-"},
  {0x40, 4, POLL_ONCE, "PIDs supported 41-60",               "bitmap",                                  "-"},
  {0x60, 4, POLL_ONCE, "PIDs supported 61-80",               "bitmap",                                  "-"},
  {0x80, 4, POLL_ONCE, "PIDs supported 81-A0",               "bitmap",                                  "-"},
  {0xA0, 4, POLL_ONCE, "PIDs supported A1-C0",               "bitmap",                                  "-"},
  {0x04, 1, POLL_FAST, "Calculated engine load",             "A*100/255 %",                             "LOAD"},
  {0x0B, 1, POLL_FAST, "Intake manifold abs pressure (MAP)", "A kPa  (MAX 255 kPa = ~22 psi boost!)",   "BOOST (fallback)"},
  {0x0C, 2, POLL_SLOW, "Engine RPM",                         "(256A+B)/4 rpm",                          "RPM"},
  {0x0F, 1, POLL_SLOW, "Intake air temperature",             "A-40 degC",                               "IAT"},
  {0x23, 2, POLL_FAST, "Fuel rail gauge pressure",           "10*(256A+B) kPa",                         "RAIL ACT (fallback 1)"},
  {0x33, 1, POLL_SLOW, "Absolute barometric pressure",       "A kPa",                                   "BARO (for boost/EBP)"},
  {0x42, 2, POLL_SLOW, "Control module voltage",             "(256A+B)/1000 V",                         "BATT"},
  {0x49, 1, POLL_FAST, "Accelerator pedal position D",       "A*100/255 %",                             "PEDAL (fallback)"},
  {0x59, 2, POLL_FAST, "Fuel rail absolute pressure",        "10*(256A+B) kPa (minus baro)",            "RAIL ACT (fallback 2)"},
  {0x5A, 1, POLL_FAST, "Relative accelerator pedal position","A*100/255 %",                             "PEDAL"},
  {0x6D, 11, POLL_FAST,"Fuel pressure control system",       "A=support; BC=cmd A 10kPa; DE=act A 10kPa","RAIL CMD + ACT"},
  {0x70, 10, POLL_FAST,"Boost pressure control",             "A=support; BC=cmd A, DE=sensor A, 0.03125 kPa abs","BOOST (fallback)"},
  {0x71, 6, POLL_FAST, "VGT control",                        "A=support; B=cmd A %, C=act A % (x100/255)","VANE CMD + ACT"},
  {0x73, 5, POLL_FAST, "Exhaust pressure",                   "A=support; BC=bank1 0.01 kPa abs",        "EBP"},
  {0x74, 5, POLL_NEVER,"Turbocharger RPM",                   "A=support; BC=turbo A rpm (not used yet)", "-"},
  {0x75, 7, POLL_NEVER,"Turbocharger A temperatures",        "not used yet",                            "-"},
  {0x87, 5, POLL_FAST, "Intake manifold abs pressure (ext)", "A=support; BC=sensor A 1/32 kPa",         "BOOST (preferred)"},
};
static const int PID_TABLE_N = sizeof(PID_TABLE) / sizeof(PID_TABLE[0]);

// Manufacturer Mode 22 table - INTENTIONALLY EMPTY. See TODO above.
struct Mode22Def { uint16_t did; uint8_t bytes; const char* name; const char* formula; };
static const Mode22Def MODE22_TABLE[] = {
  {0x0000, 0, "TODO - add verified Cummins Mode 22 DIDs here", "-"},
};
