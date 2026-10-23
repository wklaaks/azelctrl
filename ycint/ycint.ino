/*********************************************************************
  Yaesu Rotator Controller - Menu System + WiFi Management
  
  Buttons:
    D8 (Btn1) - active HIGH
    D5 (Btn2) - active LOW
  
  I2C: SDA=D6 (GPIO12), SCL=D7 (GPIO13)
  ADS1115: Ch0 = Azimuth pot, Ch1 = Elevation pot
  
  MENU SYSTEM BUTTON BEHAVIOR:
    Btn1 short-press  = navigate cursor down (wraps to top)
    Btn1 long-press   = select current item
    Btn2              = go back to parent menu (or restart root menu)
    
  ROTATOR CONTROL BUTTON BEHAVIOR (preserved from original):
    Btn1 short-press  = cycle through directions
    Btn2 press        = START movement in selected direction
    Btn2 release      = STOP movement
    
  CALIBRATION BUTTON BEHAVIOR (preserved from original):
    Btn1              = exit calibration
    Btn2 press        = start scan / stop scan
    Btn2 long-press   = reverse direction during scanning
    
  WiFi modes:
    STA = connect to home WiFi (uses saved credentials)
    AP  = broadcast AZELCTRL for direct field access
*********************************************************************/

#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ADS1X15.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <cerrno>
#include <time.h>

// ==================== SPIFFS HTML ====================
// The ESP serves the HTML from SPIFFS (filesystem) if available,
// otherwise falls back to the compiled-in copy in PROGMEM.
// To upload: gzip -9 -c index.html > index.html.gz
//            curl -F "file=@index.html.gz" http://<ESP_IP>/upload
// World map coastline data is served separately from SPIFFS (/world_coastline.js)
// so the Map tab can show a warning if it's missing.
const char* spiffsHtmlFile = "/index.html.gz";
const char* spiffsCoastlineFile = "/world_coastline.js.gz";

// Compiled-in HTML fallback (gzip-compressed) — stored in flash via ICACHE_RODATA_ATTR
#include "index_gz.h"
// The xxd-generated array is named index_html_gz; wrap it for PROGMEM access
#define index_gz_size sizeof(index_html_gz)

// ==================== EEPROM LAYOUT ====================
// All settings in one contiguous block. Using 512 bytes = one full flash sector
// so ESP8266's EEPROM library loads everything into RAM on begin(). Every commit
// writes the complete sector atomically — no cross-contamination between saves.
#define EEPROM_SIZE        512

// Sector 0: WiFi credentials (bytes 0-66)
#define EEPROM_SSID_LEN    0
#define EEPROM_SSID_DATA   1
#define EEPROM_PASS_LEN    33
#define EEPROM_PASS_DATA   34
#define EEPROM_MAGIC_BYTE  66
#define EEPROM_MAGIC       0xAA

// Sector 1: Calibration data (bytes 67-83)
#define CAL_EEPROM_AZ_MIN  67
#define CAL_EEPROM_AZ_MAX  71
#define CAL_EEPROM_EL_MIN  75
#define CAL_EEPROM_EL_MAX  79
#define CAL_EEPROM_MAGIC   83
#define CAL_EEPROM_MAGIC_VAL 0xCA

// Sector 2: WiFi mode (bytes 84-85)
#define WIFI_MODE_EEPROM   84
#define WIFI_MODE_MAGIC    85
#define WIFI_MODE_MAGIC_VAL 0x4D

// Sector 3: Network settings (bytes 86-87)
#define EEPROM_NET_HAMLIB  86
#define EEPROM_NET_MAGIC   87
#define EEPROM_NET_MAGIC_VAL 0x4E

// Sector 4: Rotator model (bytes 88-89)
#define ROT_EEPROM_MODEL   88
#define ROT_EEPROM_MAGIC   89
#define ROT_EEPROM_MAGIC_VAL 0x5A

// ==================== FIRMWARE VERSION ====================
#define FIRMWARE_VERSION "1.5.10"
#define FIRMWARE_DATE __DATE__

// ==================== NETWORK PROTOCOL SERVER ====================
const int ROTCTL_PORT = 4533;  // Hamlib default port for rotctld
WiFiServer rotctlServer(ROTCTL_PORT);
WiFiClient rotctlClient;
bool rotctlConnected = false;
bool rotctlServerStarted = false;  // Guard against double init

// ==================== COMPILE-TIME DEBUG FLAGS ====================
// Set to 1 to enable debug output (prefixed with 'DBG:' for easy filtering)
// Set to 0 to disable all debug output (zero overhead)
#define DEBUG_ENABLED 0
#define DEBUG_PROTOCOL 1  // Debug protocol parsing details
#define DEBUG_MOVEMENT 1  // Debug movement commands

// Debug helper macros - output prefixed with 'DBG:' so Python tests can filter
#if DEBUG_ENABLED
  #define DBG_PRINT(msg) Serial.print("DBG:" msg)
  #define DBG_PRINTLN(msg) do { Serial.print("DBG:"); Serial.println(msg); } while(0)
  #define DBG_PRINT_VAL(val) Serial.print("DBG:" #val "="); Serial.print(val); Serial.println("")
#else
  #define DBG_PRINT(msg)
  #define DBG_PRINTLN(msg)
  #define DBG_PRINT_VAL(val)
#endif

// ==================== PROTOCOL CONFIGURATION ====================
// Enable GS-232A/B and Easycom protocol support
#define PROTOCOL_GS232A 1
#define PROTOCOL_GS232B 1
#define PROTOCOL_EASYCOM 1

// Baud rate for protocol serial communication (USB)
#define PROTOCOL_BAUD 115200

// Protocol command buffer size
#define PROTOCOL_BUF_SIZE 64

// ==================== PIN DEFINITIONS ====================
const int AZ_ENABLE  = 4;   // GPIO4
const int AZ_DIR_CW  = 0;   // GPIO0
const int EL_ENABLE  = 5;   // GPIO5
const int EL_DIR_UP  = 2;   // GPIO2

const int SDA_PIN = 12;     // GPIO12

const int SCL_PIN = 13;     // GPIO13

const int BTN1 = 15;         // GPIO15 - active HIGH
const int BTN2 = 14;         // GPIO14 - active LOW
// ==================== BUTTON TIMING ====================
const unsigned long BTN_DEBOUNCE_MS = 200;   // Debounce between presses
const unsigned long BTN_LONG_MS = 800;       // Long press threshold for menu select
const unsigned long BTN2_LONG_MS = 500;      // Btn2 long press for calibration reverse

// ==================== WiFi AP SETTINGS ====================
char apSsid[17];  // "AZELCTRL_" (9) + 4 hex digits + null = 14 max
const char* AP_IP   = "192.168.4.1";
char apPassword[9] = "yaesu123";  // 8 chars minimum for WPA2 + null terminator

// AP Password EEPROM storage (bytes 91-99: 8 chars + magic)
#define AP_PASS_EEPROM_START 91
#define AP_PASS_EEPROM_MAGIC 99
#define AP_PASS_EEPROM_MAGIC_VAL 0x41  // 'A'

void initApSsid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "AZELCTRL_%04X", (uint16_t)((mac[4] << 8) | mac[5]));
  Serial.println("AP SSID: " + String(apSsid));
}

// ==================== OBJECTS ====================
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_ADS1115 ads;
ESP8266WebServer server(80);
DNSServer dnsServer;

// ==================== STATE ====================
float currentAz = 0.0, currentEl = 0.0;
float azVoltage = 0.0, elVoltage = 0.0;  // Raw voltages from readPositions()
String currentMove = "IDLE";
bool adsPresent = false;
bool moving = false;

// WiFi mode: STA (connect to home), AP (field mode), or DISABLED
enum DeviceWifiMode { MODE_STA, MODE_AP, MODE_DISABLED };
DeviceWifiMode deviceWifiMode = MODE_DISABLED;

// ==================== GS-232A/B & EASYCOM PROTOCOL ====================
// Protocol command buffer
char protocolBuf[PROTOCOL_BUF_SIZE];
int protocolBufIdx = 0;

// Protocol state
bool protocolActive = false;
unsigned long protocolLastCmd = 0;
const unsigned long PROTOCOL_TIMEOUT_MS = 5000;

// Configurable protocol baud rate (stored in EEPROM)
// Supported: 9600, 19200, 38400, 57600, 115200
const long PROTOCOL_BAUD_RATES[] = {9600, 19200, 38400, 57600, 115200};
const int PROTOCOL_BAUD_COUNT = 5;
int protocolBaudIndex = 4;  // Default: 115200 (index 4)

// Show received commands on OLED (default: true)
bool protocolShowCommands = true;
char protocolLastCommand[PROTOCOL_BUF_SIZE];  // Last received command for display
char protocolLastResponse[PROTOCOL_BUF_SIZE];  // Last sent response for display

// Time of last received protocol char — used to detect stalled partial commands
unsigned long protocolLastRxTime = 0;
const unsigned long PROTOCOL_PARTIAL_TIMEOUT_MS = 500;  // Stalled partial command flush

// Transient OLED warning for protocol errors (auto-clears after 5s)
char oledWarnText[21] = "";
unsigned long oledWarnUntil = 0;
void showOledWarning(const char* msg) {
  strncpy(oledWarnText, msg, sizeof(oledWarnText) - 1);
  oledWarnText[sizeof(oledWarnText) - 1] = '\0';
  oledWarnUntil = millis() + 5000;
}

// Suppress RX/TX overlay on OLED for 5s after any button press
// This prevents continuous client polling from hiding the menu
unsigned long oledCmdSuppressUntil = 0;
const unsigned long OLED_CMD_SUPPRESS_MS = 5000;

void resetOledCmdSuppress() {
  oledCmdSuppressUntil = millis() + OLED_CMD_SUPPRESS_MS;
}

// TCP input buffer (Default Protocol over TCP)
char tcpProtocolBuf[PROTOCOL_BUF_SIZE];
int tcpProtocolBufIdx = 0;

// ==================== PROTOCOL DEBUG LOG ====================
// RAM-only ring buffer capturing raw protocol traffic (RX + TX) on both the
// USB serial and TCP interfaces, for diagnosing external tracking software.
// Zero flash wear: nothing is written to the filesystem. Capture is armed
// only while a debug client polls /debug-log (auto-disarms 5 s after the
// last poll) — opening the debug tab starts logging, closing it stops it.
#define DBGLOG_MAX_ENTRIES 96
#define DBGLOG_MSG_MAX     56
struct DbgLogEntry {
  uint32_t t;      // millis() at capture
  uint8_t dir;     // 'R' = received, 'T' = transmitted
  uint8_t src;     // 'S' = USB serial, 'T' = TCP, 'C' = console
  uint8_t len;     // escaped message length
  char msg[DBGLOG_MSG_MAX];
};  // 64 bytes
static DbgLogEntry dbgLogBuf[DBGLOG_MAX_ENTRIES];  // ~6 KB static RAM
static uint32_t dbgLogSeq = 0;        // entries captured this session
static bool dbgLogArmed = false;
static unsigned long dbgLogLastPollMs = 0;
const unsigned long DBGLOG_IDLE_DISARM_MS = 5000;

void dbgLogArm(bool fresh) {
  if (fresh || !dbgLogArmed) {
    dbgLogArmed = true;
    dbgLogSeq = 0;
    memset(dbgLogBuf, 0, sizeof(dbgLogBuf));  // clear stale slots from previous sessions
  }
  dbgLogLastPollMs = millis();
}

// ==================== TIME REFERENCE (wall-clock timestamps) ====================
// The ESP8266 has no RTC. Debug-log entries store millis() since boot; wall-clock
// time is derived at render time:  epoch_ms = refEpoch*1000 + (t - refMillis).
// The reference is set by NTP (STA mode with internet) or by the one-click
// browser sync on the /debug-usb page (works in AP mode too). The display
// offset (user's UTC offset) persists across reboots (EEPROM bytes 106-110).
#define TIME_OFFSET_EEPROM   106   // int32 UTC offset seconds (bytes 106-109)
#define TIME_MAGIC_EEPROM    110   // 0x74 't' when the offset is valid

uint32_t timeRefEpoch = 0;      // epoch seconds captured at timeRefMillis (0 = unset)
uint32_t timeRefMillis = 0;     // millis() when the reference was captured
int32_t  timeOffsetSec = 0;     // display offset (persisted)
char     timeSource[5] = "none";  // "NTP" | "SYNC" | "none"
static uint32_t lastTimeCheckMs = 0;

bool timeRefValid() { return timeRefEpoch > 1000000000u; }  // after year 2001

void setTimeRef(uint32_t epochSec, const char* src) {
  timeRefEpoch = epochSec;
  timeRefMillis = millis();
  strncpy(timeSource, src, sizeof(timeSource) - 1);
  timeSource[sizeof(timeSource) - 1] = '\0';
}

void loadTimeOffset() {
  if (EEPROM.read(TIME_MAGIC_EEPROM) != 0x74) return;
  uint32_t v = ((uint32_t)EEPROM.read(TIME_OFFSET_EEPROM) << 24) |
               ((uint32_t)EEPROM.read(TIME_OFFSET_EEPROM + 1) << 16) |
               ((uint32_t)EEPROM.read(TIME_OFFSET_EEPROM + 2) << 8) |
               (uint32_t)EEPROM.read(TIME_OFFSET_EEPROM + 3);
  timeOffsetSec = (int32_t)v;
}

void saveTimeOffset() {
  uint32_t v = (uint32_t)timeOffsetSec;
  EEPROM.write(TIME_OFFSET_EEPROM, (v >> 24) & 0xFF);
  EEPROM.write(TIME_OFFSET_EEPROM + 1, (v >> 16) & 0xFF);
  EEPROM.write(TIME_OFFSET_EEPROM + 2, (v >> 8) & 0xFF);
  EEPROM.write(TIME_OFFSET_EEPROM + 3, v & 0xFF);
  EEPROM.write(TIME_MAGIC_EEPROM, 0x74);
  EEPROM.commit();
}

// Start NTP (best effort). In AP mode there is no uplink and this never
// succeeds - the browser sync button on /debug-usb covers that case.
void initTimeNtp() {
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  Serial.println("TIME: NTP configured");
}

// Pick up NTP results once every 30 s while WiFi is connected.
// The reference is re-baselined SILENTLY each cycle so SNTP step
// corrections are absorbed, but only GENUINE transitions are logged:
// first acquisition (none->NTP) or NTP taking over from a manual browser
// sync (SYNC->NTP). Logging every 30 s would fill the ring with duplicate
// "NTP synced" entries and push real protocol traffic out of the buffer.
void checkTimeSync() {
  unsigned long now = millis();
  if (now - lastTimeCheckMs < 30000) return;
  lastTimeCheckMs = now;
  if (WiFi.status() != WL_CONNECTED) return;
  time_t t = time(nullptr);
  if (t <= 1000000000) return;
  bool fresh = !timeRefValid();
  bool upgrade = (strcmp(timeSource, "SYNC") == 0);
  setTimeRef((uint32_t)t, "NTP");
  if (fresh) {
    Serial.print("TIME: NTP synced, epoch=");
    Serial.println(t);
    dbgLogCapture("TX", "CON", "TIME: NTP synced");
  } else if (upgrade) {
    Serial.println("TIME: NTP active (supersedes manual sync)");
    dbgLogCapture("TX", "CON", "TIME: NTP active (supersedes manual sync)");
  }
}

// Howard Hinnant's civil-from-days (proleptic Gregorian, all dates).
static void civilFromDays(int64_t z, int& y, int& m, int& d) {
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  int32_t doe = (int32_t)(z - era * 146097);                     // [0, 146096]
  int32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
  int32_t yy  = (int32_t)era * 400 + yoe;
  int32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);         // [0, 365]
  int32_t mp  = (5 * doy + 2) / 153;                             // [0, 11]
  d = doy - (153 * mp + 2) / 5 + 1;                              // [1, 31]
  m = mp + (mp < 10 ? 3 : -9);                                   // [1, 12]
  y = yy + (m <= 2);  // Jan/Feb fall in the previous year (March-based calendar)
}

// Line prefix for a captured entry: "[<boot-ms>] YYYY-MM-DD HH:MM:SS.mmm "
// when a time reference is set, else just "[<boot-ms>] ". out >= 40 bytes.
// Entries captured more than an hour BEFORE the reference (i.e. it landed
// mid-session) fall back to the bare form - their wall time would be bogus.
static void formatLogPrefix(uint32_t tBootMs, char* out, size_t n) {
  if (!timeRefValid()) {
    snprintf(out, n, "[%lu] ", (unsigned long)tBootMs);
    return;
  }
  uint32_t delta = tBootMs - timeRefMillis;  // uint32 wrap = correct ms delta
  if (delta > 3600000u) {
    snprintf(out, n, "[%lu] ", (unsigned long)tBootMs);
    return;
  }
  uint64_t totalMs = (uint64_t)timeRefEpoch * 1000ULL + (uint64_t)delta;
  int64_t localSec = (int64_t)(totalMs / 1000ULL) + timeOffsetSec;
  uint32_t ms = (uint32_t)(totalMs % 1000ULL);
  int64_t days = localSec / 86400;
  int32_t secOfDay = (int32_t)(localSec - days * 86400);
  int y, m, d;
  civilFromDays(days, y, m, d);
  snprintf(out, n, "[%lu] %04d-%02d-%02d %02d:%02d:%02d.%03lu ",
           (unsigned long)tBootMs, y, m, d,
           secOfDay / 3600, (secOfDay / 60) % 60, secOfDay % 60, (unsigned long)ms);
}

// Current device time as "YYYY-MM-DD HH:MM:SS" (JSON status + file header).
// Returns false when no reference is set.
static bool formatDeviceTime(char* out, size_t n) {
  if (!timeRefValid()) return false;
  uint64_t totalMs = (uint64_t)timeRefEpoch * 1000ULL +
                     (uint64_t)(uint32_t)(millis() - timeRefMillis);
  int64_t localSec = (int64_t)(totalMs / 1000ULL) + timeOffsetSec;
  int64_t days = localSec / 86400;
  int32_t secOfDay = (int32_t)(localSec - days * 86400);
  int y, m, d;
  civilFromDays(days, y, m, d);
  snprintf(out, n, "%04d-%02d-%02d %02d:%02d:%02d", y, m, d,
           secOfDay / 3600, (secOfDay / 60) % 60, secOfDay % 60);
  return true;
}

// Escape control chars so the log reads as plain text (\r \n \t \\ \" \xNN).
// The output is also safe to embed in JSON strings.
static void dbgLogEscape(const char* in, int inLen, char* out, int maxOut) {
  static const char hexd[] = "0123456789abcdef";
  int o = 0;
  for (int i = 0; i < inLen && o + 4 < maxOut; i++) {
    char c = in[i];
    if (c == '\r')                        { out[o++] = '\\'; out[o++] = 'r'; }
    else if (c == '\n')                   { out[o++] = '\\'; out[o++] = 'n'; }
    else if (c == '\t')                   { out[o++] = '\\'; out[o++] = 't'; }
    else if (c == '\\' || c == '"')       { out[o++] = '\\'; out[o++] = c; }
    else if (c >= 0x20 && c < 0x7f)       { out[o++] = c; }
    else {
      out[o++] = '\\'; out[o++] = 'x';
      out[o++] = hexd[(c >> 4) & 0xF]; out[o++] = hexd[c & 0xF];
    }
  }
  out[o] = '\0';
}

// Escape backslashes for JSON embedding. The message is already printable-safe
// (control chars rendered as \r \n \xNN two-char sequences); without this, JSON
// parsing would reinterpret those sequences and corrupt the line.
static void dbgLogJsonEscape(const char* in, char* out, int maxOut) {
  int o = 0;
  for (int i = 0; in[i] != '\0' && o + 3 < maxOut; i++) {
    if (in[i] == '\\') out[o++] = '\\';
    out[o++] = in[i];
  }
  out[o] = '\0';
}

// Capture one line while armed. dir: "RX"/"TX", src: "SER"/"TCP"/"CON".
void dbgLogCapture(const char* dir, const char* src, const char* text) {
  if (!dbgLogArmed) return;
  char esc[DBGLOG_MSG_MAX];
  dbgLogEscape(text, (int)strlen(text), esc, DBGLOG_MSG_MAX);
  DbgLogEntry& e = dbgLogBuf[dbgLogSeq % DBGLOG_MAX_ENTRIES];
  e.t = millis();
  e.dir = dir[0];
  e.src = src[0];
  e.len = (uint8_t)strlen(esc);
  memcpy(e.msg, esc, e.len + 1);
  dbgLogSeq++;
}

// Protocol target tracking — auto-stop when target is reached
bool protocolTargetAzSet = false;
bool protocolTargetElSet = false;
float protocolTargetAz = 0.0;
float protocolTargetEl = 0.0;
const float PROTOCOL_STOP_TOLERANCE = 3.0; // Degrees — don't start movement if within this range

// Protocol variant selection (stored in EEPROM)
enum ProtocolVariant {
  PROTO_GS232A,   // Yaesu original format (+nnnn+nnnn)
  PROTO_GS232B,   // Hamlib standard (AZ=nnn   EL=nnn)
  PROTO_EASYCOM   // Easycom protocol
};
ProtocolVariant currentProtocol = PROTO_GS232A;  // Default: GS-232A


// Direction reversal safeguard — configurable delay before reversing direction
int protocolReverseDelay = 1;  // Default: 1 second
bool protocolAzReversing = false;
bool protocolElReversing = false;
unsigned long protocolAzReverseStart = 0;
unsigned long protocolElReverseStart = 0;
String protocolAzPendingDir = "";
String protocolElPendingDir = "";

// ELF (Elevation Flip / Over-the-top) mode
// When enabled, transforms satellite coordinates to physical rotator coordinates
// Real Az = (Sat Az + 180) % 360, Real El = 180 - Sat El
bool elfMode = false;
#define ELF_EEPROM_BYTE 90

// ELF transformation functions (self-inverse)
float elfTransformAz(float satAz) {
  float realAz = fmod(satAz + 180.0f, 360.0f);
  if (realAz < 0) realAz += 360.0f;
  return realAz;
}
float elfTransformEl(float satEl) {
  return 180.0f - satEl;
}

// Conditional wrappers - only transform when elfMode is ON
float elfAz(float az) {
  return elfMode ? elfTransformAz(az) : az;
}
float elfEl(float el) {
  return elfMode ? elfTransformEl(el) : el;
}

// Motor engagement delay — suppress stale position readings while motors ramp up
// Must be short enough that Gpredict sees progress before giving up (~1.5s timeout)
// 200ms allows ADC sliding window (4x100ms) to stabilize without triggering Gpredict abort
const unsigned long MOTOR_ENGAGE_DELAY_MS = 200;
unsigned long azEngageStart = 0;
unsigned long elEngageStart = 0;
float azPositionAtEngage = 0.0;  // Last known good position when motors started
float elPositionAtEngage = 0.0;

long getProtocolBaud() {
  return PROTOCOL_BAUD_RATES[protocolBaudIndex];
}

// Protocol-specific response formatting helpers
void formatPositionResponse(char* buf, size_t bufsize, int azDeg, int elDeg, const char* suffix) {
  switch (currentProtocol) {
    case PROTO_GS232A:
      // Yaesu original format: +nnnn+nnnn [suffix]
      if (suffix && suffix[0]) {
        snprintf(buf, bufsize, "+%04d+%04d %s", azDeg, elDeg, suffix);
      } else {
        snprintf(buf, bufsize, "+%04d+%04d", azDeg, elDeg);
      }
      break;
      
    case PROTO_GS232B:
      // Hamlib standard: AZ=nnn EL=nnn [suffix]
      if (suffix && suffix[0]) {
        snprintf(buf, bufsize, "AZ=%03d EL=%03d %s", azDeg, elDeg, suffix);
      } else {
        snprintf(buf, bufsize, "AZ=%03d EL=%03d", azDeg, elDeg);
      }
      break;
      
    case PROTO_EASYCOM:
      // Easycom: AZnnn.0ELnnn.0 [suffix]
      if (suffix && suffix[0]) {
        snprintf(buf, bufsize, "AZ%d.0EL%d.0 %s", azDeg, elDeg, suffix);
      } else {
        snprintf(buf, bufsize, "AZ%d.0EL%d.0", azDeg, elDeg);
      }
      break;
  }
}

// Interface that delivered the current/most-recent protocol command.
// Replies are always returned over the SAME interface the command arrived on:
// RX_SRC_SER -> USB serial wire, RX_SRC_TCP -> rotctl TCP client. Never cross.
enum ProtocolRxSource { RX_SRC_NONE = 0, RX_SRC_SER, RX_SRC_TCP };
uint8_t protocolRxSource = RX_SRC_NONE;

// Get protocol-specific line ending
// Hamlib source: gs232a.c REPLY_EOM="\n", gs232b.c REPLY_EOM="\r\n"
const char* getProtocolLineEnding() {
  switch (currentProtocol) {
    case PROTO_GS232A:
      return "\n";     // GS-232A: hamlib reads until LF only
    case PROTO_GS232B:
      return "\r\n";   // GS-232B: hamlib reads until CRLF
    case PROTO_EASYCOM:
      return "\n";     // Easycom: hamlib reads until LF only
  }
  return "\r\n";
}

// Process command based on current protocol variant
void processProtocolCommand(const char* cmd, int len) {
  switch (currentProtocol) {
    case PROTO_GS232A:
    case PROTO_GS232B:
      processGs232Command(cmd, len);
      break;
    case PROTO_EASYCOM:
      processEasycomCommand(cmd, len);
      break;
  }
}


// EEPROM storage for protocol settings (bytes 100-104 — must not overlap with calibration at 67-83)
#define EEPROM_PROTO_BAUD_IDX 100
#define EEPROM_PROTO_SHOW_CMDS 101
#define EEPROM_PROTO_REVERSE_DELAY 103
#define EEPROM_PROTO_MAGIC    102
#define EEPROM_PROTO_MAGIC_VAL 0xB7
#define EEPROM_PROTO_VARIANT 104

// Load protocol settings from EEPROM
void loadProtocolSettings() {
  uint8_t magic = EEPROM.read(EEPROM_PROTO_MAGIC);
  if (magic == EEPROM_PROTO_MAGIC_VAL) {
    uint8_t idx = EEPROM.read(EEPROM_PROTO_BAUD_IDX);
    if (idx < PROTOCOL_BAUD_COUNT) {
      protocolBaudIndex = idx;
    }
    uint8_t show = EEPROM.read(EEPROM_PROTO_SHOW_CMDS);
    protocolShowCommands = (show == 1);
    uint8_t revDelay = EEPROM.read(EEPROM_PROTO_REVERSE_DELAY);
    if (revDelay <= 3) {
      protocolReverseDelay = revDelay;
    }
    uint8_t variant = EEPROM.read(EEPROM_PROTO_VARIANT);
    if (variant <= 2) {
      currentProtocol = (ProtocolVariant)variant;
    }
  }
}

// Save protocol settings to EEPROM
void saveProtocolSettings() {
  EEPROM.write(EEPROM_PROTO_BAUD_IDX, (uint8_t)protocolBaudIndex);
  EEPROM.write(EEPROM_PROTO_SHOW_CMDS, (uint8_t)(protocolShowCommands ? 1 : 0));
  EEPROM.write(EEPROM_PROTO_REVERSE_DELAY, (uint8_t)protocolReverseDelay);
  EEPROM.write(EEPROM_PROTO_VARIANT, (uint8_t)currentProtocol);
  EEPROM.write(EEPROM_PROTO_MAGIC, EEPROM_PROTO_MAGIC_VAL);
  EEPROM.commit();
}

// ==================== DISPLAY SETTINGS (EEPROM byte 105) ====================
// 1 = show decimal degrees (one decimal digit) for Az/El; any other value
// (0 or erased 0xFF) = whole degrees. Default OFF.
#define EEPROM_DISP_AZEL_DECIMALS 105
bool azElDisplayDecimals = false;

void loadDisplaySettings() {
  azElDisplayDecimals = (EEPROM.read(EEPROM_DISP_AZEL_DECIMALS) == 1);
}

void saveDisplaySettings() {
  EEPROM.write(EEPROM_DISP_AZEL_DECIMALS, azElDisplayDecimals ? 1 : 0);
  EEPROM.commit();
}

// Restart serial with new baud rate
void applyProtocolBaud() {
  Serial.end();
  delay(100);
  Serial.begin(getProtocolBaud());
  DBG_PRINT("Protocol baud set to: "); DBG_PRINTLN(getProtocolBaud());
}

// Helper: parse integer from buffer starting at index, returns length consumed (0 if none)
int parseNumber(const char* buf, int start, int* num, int* consumed) {
  int i = start;
  int len = strlen(buf);
  bool negative = false;
  int value = 0;
  
  if (i < len && buf[i] == '-') {
    negative = true;
    i++;
  }
  
  while (i < len && buf[i] >= '0' && buf[i] <= '9') {
    value = value * 10 + (buf[i] - '0');
    i++;
  }
  
  if (i == start) {
    *consumed = 0;
    return -1;  // No number found
  }
  
  *num = negative ? -value : value;
  *consumed = i - start;
  return 0;
}



// ==================== MENU SYSTEM ====================
typedef void (*MenuAction)();

struct MenuItem {
  const char* label;
  MenuAction action;
  int timerSeconds;     // 0 = no timer, >0 = countdown
  bool visible;         // Dynamic visibility
  bool (*isSelectable)();  // nullptr = always selectable; returns false = selection blocked
  // When blocked, this returns which item indices (0-based) are the blockers, for footer display
  int (*blockerIndices)(int* indices, int maxCount);  // Returns count of blockers written to indices[]
};

class Menu {
public:
  Menu(const char* title, MenuItem* items, int itemCount, Menu* parent = nullptr)
    : title(title), items(items), itemCount(itemCount), parent(parent),
      cursor(0), scrollOffset(0), timerStart(0), timerActive(false), userInteracted(false), footerText(nullptr) {}

  void setParent(Menu* p) {
    parent = p;
  }

  void init() {
    cursor = 0;
    scrollOffset = 0;
    timerActive = false;
    timerStart = 0;
    userInteracted = false;  // Reset: timer can auto-start again
    moveToFirstVisible();
    // Auto-start timer on the first visible item that has one
    for (int i = 0; i < itemCount; i++) {
      if (items[i].visible && items[i].timerSeconds > 0) {
        cursor = i;
        startTimer();
        return;
      }
    }
  }

  void setFooter(const char* text) {
    footerText = text;
  }

  void navigate() {
    userInteracted = true;  // Cancel auto-timer on any button press
    timerActive = false;
    int next = cursor + 1;
    if (next >= itemCount) next = 0;
    moveToVisibleFrom(next);
    updateScroll();
  }

  // Update scroll offset so the cursor is always visible
  // When cursor moves to row VISIBLE_ROWS, scroll up by 1
  // When wrapping (cursor < scrollOffset), reset scroll
  void updateScroll() {
    // Count visible items to find the actual visible range
    int visibleCount = 0;
    for (int i = scrollOffset; i < itemCount && visibleCount < VISIBLE_ROWS; i++) {
      if (items[i].visible) visibleCount++;
    }
    
    // If cursor is below the visible area, scroll down
    while (cursor >= scrollOffset + visibleCount) {
      scrollOffset++;
    }
    
    // If cursor wrapped to top (above scroll area), reset scroll
    if (cursor < scrollOffset) {
      scrollOffset = 0;
    }
  }

  void select() {
    userInteracted = true;  // Cancel auto-timer on any button press
    timerActive = false;
    if (cursor >= 0 && cursor < itemCount && items[cursor].visible && items[cursor].action) {
      // Check if selection is allowed
      if (items[cursor].isSelectable == nullptr || items[cursor].isSelectable()) {
        items[cursor].action();
      }
    }
  }

  // Check if current item is selectable (for footer display)
  bool isCurrentSelectable() {
    if (cursor < 0 || cursor >= itemCount) return true;
    if (items[cursor].isSelectable == nullptr) return true;
    return items[cursor].isSelectable();
  }

  // Get blocker indices for current item (1-based for display)
  // Returns count of blockers written to indices[] (as 1-based values)
  int getBlockerIndices(int* indices, int maxCount) {
    if (cursor < 0 || cursor >= itemCount) return 0;
    if (items[cursor].blockerIndices == nullptr) return 0;
    int raw[4];
    int count = items[cursor].blockerIndices(raw, 4);
    int out = 0;
    for (int i = 0; i < count && out < maxCount && out < 4; i++) {
      indices[out++] = raw[i] + 1;  // Convert to 1-based
    }
    return out;
  }

  void back() {
    userInteracted = true;
    timerActive = false;
    if (parent) {
      parent->cursor = cursor;
      parent->draw();
      extern Menu* currentMenu;
      currentMenu = parent;
    } else {
      init();
    }
  }
  
  Menu* getParent() {
    return parent;
  }

  void tick() {
    // Don't auto-fire if user has interacted or item is not selectable
    if (timerActive && !userInteracted && cursor >= 0 && cursor < itemCount) {
      int remaining = items[cursor].timerSeconds - (int)((millis() - timerStart) / 1000);
      if (remaining <= 0) {
        timerActive = false;
        if (items[cursor].visible && items[cursor].action) {
          // Check if selection is allowed
          if (items[cursor].isSelectable == nullptr || items[cursor].isSelectable()) {
            items[cursor].action();
          }
        }
      }
    }
  }

  int timerRemaining() {
    if (!timerActive) return 0;
    if (cursor < 0 || cursor >= itemCount) return 0;
    int remaining = items[cursor].timerSeconds - (int)((millis() - timerStart) / 1000);
    return max(0, remaining);
  }

  void startTimer() {
    if (cursor >= 0 && cursor < itemCount && items[cursor].timerSeconds > 0) {
      timerActive = true;
      timerStart = millis();
    }
  }

  void draw() {
    display.clearDisplay();
    display.setTextSize(1);
    
    // Title bar (white on black)
    display.fillRect(0, 0, 128, 8, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setCursor(0, 0);
    String titleLine = String(title) + " ";
    while (titleLine.length() < 16) titleLine += " ";
    display.print(titleLine);
    
    // Solid white line below title
    display.drawLine(0, 8, 127, 8, SSD1306_WHITE);
    
    // Menu items - start 1 row below separator, with scrolling
    int y = 10;
    int visibleDrawn = 0;
    for (int i = scrollOffset; i < itemCount && visibleDrawn < VISIBLE_ROWS; i++) {
      if (!items[i].visible) continue;
      
      if (i == cursor) {
        // Selected - inverted background
        display.fillRect(0, y, 128, 8, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      
      display.setCursor(2, y);
      String line = String(items[i].label);
      
      // Append timer if active on this item
      if (i == cursor && timerActive && items[i].timerSeconds > 0) {
        line += "[" + String(timerRemaining()) + "]";
      }
      
      display.print(line);
      y += 11;
      visibleDrawn++;
    }
    
    // Dotted separator above footer
    for (int x = 0; x < 128; x += 3) {
      display.drawPixel(x, 53, SSD1306_WHITE);
    }
    
    // Footer
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 56);
    if (!isCurrentSelectable()) {
      // Inhibited footer takes priority — show blocker indices
      int blockers[4];
      int count = getBlockerIndices(blockers, 4);
      char footerBuf[24] = "1.Nav  Sel[";
      int pos = 11;
      for (int i = 0; i < count && pos < 22; i++) {
        if (i > 0) {
          if (pos < 21) footerBuf[pos++] = ',';
        }
        int tens = blockers[i] / 10;
        int ones = blockers[i] % 10;
        if (tens > 0 && pos < 20) footerBuf[pos++] = '0' + tens;
        if (pos < 21) footerBuf[pos++] = '0' + ones;
      }
      if (pos < 23) footerBuf[pos++] = ']';
      footerBuf[pos] = '\0';
      display.println(footerBuf);
    } else if (footerText) {
      display.println(footerText);
    } else {
      display.println("1.Nav 1|Sel 2.Back");
    }
    
    display.display();
  }

  // Static helper for quick 3-line notices (no header/footer, big text)
  // center: true = center align, false = left align (default)
  static void drawQuickNotice(const char* line1, const char* line2, const char* line3, bool center = false) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    
    if (center) {
      int x1 = (int)max((int)0, (int)((128 - strlen(line1) * 12) / 2));
      display.setCursor(x1, 0);
      display.println(line1);
      
      int x2 = (int)max((int)0, (int)((128 - strlen(line2) * 12) / 2));
      display.setCursor(x2, 16);
      display.println(line2);
      
      int x3 = (int)max((int)0, (int)((128 - strlen(line3) * 12) / 2));
      display.setCursor(x3, 32);
      display.println(line3);
    } else {
      display.setCursor(0, 0);
      display.println(line1);
      display.setCursor(0, 16);
      display.println(line2);
      display.setCursor(0, 32);
      display.println(line3);
    }
    
    display.display();
  }
  
  // Static helper for notice screens with consistent styling
  static void drawNotice(const char* title, const char** lines, int lineCount,
                          const char* footer, bool customFooter) {
    display.clearDisplay();
    display.setTextSize(1);
    
    // Title bar (white on black)
    display.fillRect(0, 0, 128, 8, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setCursor(0, 0);
    String titleLine = String(title) + " ";
    while (titleLine.length() < 16) titleLine += " ";
    display.print(titleLine);
    
    // Solid white line below title
    display.drawLine(0, 8, 127, 8, SSD1306_WHITE);
    
    // Content area
    int y = 10;
    for (int i = 0; i < lineCount && y < 54; i++) {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(2, y);
      display.print(lines[i]);
      y += 11;
    }
    
    // Dotted separator above footer
    for (int x = 0; x < 128; x += 3) {
      display.drawPixel(x, 53, SSD1306_WHITE);
    }
    
    // Footer
    if (customFooter && strlen(footer) > 0) {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 56);
      display.println(footer);
    }
    
    display.display();
  }

private:
  const char* title;
  MenuItem* items;
  int itemCount;
  Menu* parent;
  int cursor;
  int scrollOffset;  // Top visible item index (for scrolling menus)
  unsigned long timerStart;
  bool timerActive;
  bool userInteracted;  // True when user presses a button — disables auto-timer
  const char* footerText;  // Custom footer text (nullptr = default "1.Nav 1|Sel 2.Back")
  
  // Number of visible menu rows (4 on 64px OLED: title 10px + 4*11px = 54px, footer at 56px)
  static const int VISIBLE_ROWS = 4;

  void moveToFirstVisible() {
    for (int i = 0; i < itemCount; i++) {
      if (items[i].visible) {
        cursor = i;
        return;
      }
    }
    cursor = 0;
  }

  void moveToVisibleFrom(int start) {
    for (int i = 0; i < itemCount; i++) {
      int idx = (start + i) % itemCount;
      if (items[idx].visible) {
        cursor = idx;
        return;
      }
    }
    cursor = start;
  }
};

// Forward declaration
extern Menu* currentMenu;
extern bool skipBtnProcessing;
extern bool hamlibServerEnabled;
extern char setupWifiLabel[];
extern MenuItem networkItems[];
extern Menu setupMenu;
extern Menu pcInterfaceMenu;
extern Menu baudMenu;
extern Menu networkMenu;

// ==================== WIFI CREDENTIAL MANAGEMENT ====================
// See unified EEPROM layout below for address definitions

String savedSsid = "";
String savedPass = "";

void loadWifiCredentials() {
  uint8_t magic = EEPROM.read(EEPROM_MAGIC_BYTE);
  if (magic != EEPROM_MAGIC) {
    savedSsid = "";
    savedPass = "";
    return;
  }
  uint8_t ssidLen = EEPROM.read(EEPROM_SSID_LEN);
  if (ssidLen > 32) ssidLen = 32;
  char ssidBuf[33];
  for (uint8_t i = 0; i < ssidLen; i++) ssidBuf[i] = EEPROM.read(EEPROM_SSID_DATA + i);
  ssidBuf[ssidLen] = '\0';
  savedSsid = ssidBuf;
  
  uint8_t passLen = EEPROM.read(EEPROM_PASS_LEN);
  if (passLen > 32) passLen = 32;
  char passBuf[33];
  for (uint8_t i = 0; i < passLen; i++) passBuf[i] = EEPROM.read(EEPROM_PASS_DATA + i);
  passBuf[passLen] = '\0';
  savedPass = passBuf;
}

void saveWifiCredentials(const char* ssid, const char* pass) {
  // Zero out only the credential area (bytes 0-66), not the entire EEPROM
  // Bytes 67+ contain calibration data and must not be touched
  for (int i = 0; i <= 66; i++) EEPROM.write(i, 0);
  
  uint8_t ssidLen = (uint8_t)min(strlen(ssid), (size_t)32);
  EEPROM.write(EEPROM_SSID_LEN, ssidLen);
  for (uint8_t i = 0; i < ssidLen; i++) EEPROM.write(EEPROM_SSID_DATA + i, ssid[i]);
  
  uint8_t passLen = (uint8_t)min(strlen(pass), (size_t)32);
  EEPROM.write(EEPROM_PASS_LEN, passLen);
  for (uint8_t i = 0; i < passLen; i++) EEPROM.write(EEPROM_PASS_DATA + i, pass[i]);
  
  EEPROM.write(EEPROM_MAGIC_BYTE, EEPROM_MAGIC);
  EEPROM.commit();
  savedSsid = ssid;
  savedPass = pass;
  Serial.println("WiFi credentials saved to EEPROM");
}

bool hasWifiCredentials() {
  return savedSsid.length() > 0;
}

String maskPassword(const String& pass) {
  if (pass.length() == 0) return "";
  if (pass.length() == 1) return "*";
  String masked = String(pass[0]);
  for (size_t i = 1; i < pass.length() - 1; i++) {
    masked += "*";
  }
  if (pass.length() > 1) {
    masked += String(pass[pass.length() - 1]);
  }
  return masked;
}

// ==================== WIFI CONNECTION STATE ====================
enum WifiState {
  WIFI_IDLE,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  WIFI_FAILED,
  WIFI_AP_MODE
};
WifiState wifiState = WIFI_IDLE;
unsigned long wifiStart = 0;
const unsigned long WIFI_TIMEOUT = 15000;

void connectWifiSta() {
  deviceWifiMode = MODE_STA;
  wifiState = WIFI_CONNECTING;
  wifiStart = millis();
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  
  Serial.print("Connecting to ");
  Serial.println(savedSsid);
}

void startApMode() {
  // Stop captive portal if running (it uses open AP)
  stopCaptivePortal();
  
  deviceWifiMode = MODE_AP;
  wifiState = WIFI_AP_MODE;
  
  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_AP);
  delay(100);  // Allow WiFi mode to settle
  
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  
  bool ok = WiFi.softAP(apSsid, apPassword);
  if (!ok) {
    Serial.println("AP: softAP failed, retrying...");
    delay(200);
    ok = WiFi.softAP(apSsid, apPassword);
  }
  if (!ok) {
    Serial.println("AP: softAP STILL failed!");
  }
  
  // Start DNS server for captive portal effect (opens browser automatically)
  dnsServer.start(53, "*", apIP);
  
  setupWebServer();
  startRotctlServer();  // Always listen on port 4533 — protocol depends on Hamlib Server setting
  
  Serial.println("AP Mode: " + String(apSsid));
  Serial.println("IP: " + String(AP_IP));
}

void stopApMode() {
  dnsServer.stop();
  WiFi.mode(WIFI_OFF);
  wifiState = WIFI_IDLE;
}

void tickWifi() {
  if (wifiState == WIFI_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiState = WIFI_CONNECTED;
      Serial.println("WiFi Connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP().toString());
      Serial.print("RSSI: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      setupWebServer();
      startRotctlServer();  // Always listen on port 4533 — protocol depends on Hamlib Server setting
      initTimeNtp();        // Best-effort wall-clock sync (no-op without internet)
    } else if (millis() - wifiStart > WIFI_TIMEOUT) {
      wifiState = WIFI_FAILED;
      Serial.println("WiFi connection timed out");
    }
  }
}

// ==================== REUSABLE WIFI CONNECTION DISPLAY ====================
// Returns: 0=cancelled, 1=connected, 2=failed
int waitForWifiConnection(bool allowCancel) {
  Serial.println("Waiting for WiFi connection...");

  connectWifiSta();

  int dotCount = 0;
  while (wifiState == WIFI_CONNECTING) {
    tickWifi();
    ESP.wdtFeed();

    dotCount++;
    if (dotCount % 4 == 0) {
      String netLine = "Network: " + savedSsid;
      String waitLine = "Waiting: " + String(dotCount / 4) + "s";
      const char* lines[] = { netLine.c_str(), "", waitLine.c_str() };
      Menu::drawNotice("WiFi Connecting", lines, 3, "2:Cancel", true);
    }

    if (allowCancel && !digitalRead(BTN2)) {
      delay(300);
      WiFi.disconnect();
      wifiState = WIFI_IDLE;
      return 0;  // Cancelled
    }

    delay(250);
  }

  if (wifiState == WIFI_CONNECTED) {
    // Show connected screen with IP, signal strength, and countdown
    int rssi = WiFi.RSSI();
    String quality;
    if (rssi >= -50) quality = "Excellent";
    else if (rssi >= -60) quality = "Good";
    else if (rssi >= -70) quality = "Fair";
    else quality = "Poor";

    String ipStr = WiFi.localIP().toString();

    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Connected!");
    display.setTextSize(1);
    display.setCursor(0, 24);
    display.print("IP: "); display.println(ipStr);
    display.setCursor(0, 36);
    display.print("Signal: "); display.print(rssi); display.println(" dBm");
    display.setCursor(0, 48);
    display.print("Quality: "); display.println(quality);

    // Countdown from 5 seconds — just a timer in bottom-right corner
    for (int sec = 5; sec > 0; sec--) {
      ESP.wdtFeed();
      display.fillRect(118, 56, 10, 8, SSD1306_BLACK);
      display.setCursor(120, 56);
      display.print(sec);
      display.display();

      // Check for button press to skip ahead
      for (int i = 0; i < 20; i++) {
        if (digitalRead(BTN1) || !digitalRead(BTN2)) {
          delay(300);
          return 1;  // Connected
        }
        delay(50);
      }
    }

    return 1;  // Connected
  }

  return 2;  // Failed
}


// ==================== CAPTIVE PORTAL ====================
bool captivePortalActive = false;

void startCaptivePortal() {
  captivePortalActive = true;
  deviceWifiMode = MODE_AP;
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid);  // Open for captive portal setup
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  
  dnsServer.start(53, "*", apIP);
  
  // Idempotent: registers all routes + begins the HTTP server exactly once
  // (covers first-boot AP flows where STA never connected yet). The portal
  // itself needs NO route changes - onNotFound dispatches on
  // captivePortalActive, and /save is always registered.
  setupWebServer();
  
  Serial.println("Captive portal started on " + String(apSsid));
}

void stopCaptivePortal() {
  captivePortalActive = false;
  dnsServer.stop();
  // No server.stop()/re-registration: the HTTP server keeps listening across
  // mode switches (keepAlive(false) means no clients linger), and re-running
  // setupWebServer() would leak duplicate handler nodes (it can't un-register).
}

// ==================== ROTCTL TCP SERVER ====================
// Hamlib rotctld-compatible TCP server on port 4533
// Accepts one client at a time; when disconnected, reverts to serial-only

void startRotctlServer() {
  if (rotctlServerStarted) return;  // Already running
  rotctlServerStarted = true;
  rotctlServer.begin();
  rotctlServer.setNoDelay(true);
  Serial.println("ROTCTL: TCP server started on port " + String(ROTCTL_PORT));
}

void stopRotctlServer() {
  rotctlServerStarted = false;
  if (rotctlConnected) {
    rotctlClient.stop();
    rotctlConnected = false;
  }
  rotctlServer.close();
  Serial.println("ROTCTL: TCP server stopped");
}

// Throttled console print for rotctl connect/disconnect events. A pathological
// accept/disconnect churn (zombie TCP connections) must never drown the USB
// serial wire - in Oct 2026 an unthrottled loop produced ~11k msgs/s and
// eroded heap until only the static-string prints survived.
static unsigned long lastRotctlConsoleMs = 0;
void rotctlConsole(const char* m) {
  unsigned long now = millis();
  if (now - lastRotctlConsoleMs < 1000) return;
  lastRotctlConsoleMs = now;
  Serial.println(m);
  dbgLogCapture("TX", "CON", m);
}

void checkRotctlClient() {
  // Accept new connection
  if (!rotctlConnected && rotctlServer.hasClient()) {
    if (rotctlClient) rotctlClient.stop();  // Close any lingering client
    WiFiClient nc = rotctlServer.available();
    if (!nc.connected()) {
      // Zombie/half-dead connection (e.g. peer vanished without a clean
      // close). Discard it instead of committing: re-accepting dead clients
      // in a tight loop is what caused the disconnect-spam incident.
      nc.stop();
      return;
    }
    rotctlClient = nc;
    rotctlConnected = true;
    tcpProtocolBufIdx = 0;  // Reset TCP buffer
    {
      String m = "ROTCTL: Client connected from " + nc.remoteIP().toString();
      rotctlConsole(m.c_str());
    }
    return;
  }

  // Check for disconnection
  if (rotctlConnected && !rotctlClient.connected()) {
    rotctlClient.stop();
    rotctlConnected = false;
    tcpProtocolBufIdx = 0;
    rotctlConsole("ROTCTL: Client disconnected");
    return;
  }
  
  // Read data from TCP client and route through Default Protocol handler
  if (rotctlConnected && rotctlClient.available()) {
    while (rotctlClient.available()) {
      char ch = rotctlClient.read();
      
      if (ch == '\n' || ch == '\r') {
        if (tcpProtocolBufIdx > 0) {
          tcpProtocolBuf[tcpProtocolBufIdx] = '\0';
          DBG_PRINTLN(tcpProtocolBuf);
          {
            // Debug log: exact line as received on TCP, terminator included
            char rawLine[PROTOCOL_BUF_SIZE + 1];
            memcpy(rawLine, tcpProtocolBuf, tcpProtocolBufIdx);
            rawLine[tcpProtocolBufIdx] = ch;
            rawLine[tcpProtocolBufIdx + 1] = '\0';
            dbgLogCapture("RX", "TCP", rawLine);
          }
          // This command arrived over TCP — replies must go back over TCP
          protocolRxSource = RX_SRC_TCP;
          // Track last command for the OLED RX/TX overlay
          strncpy(protocolLastCommand, tcpProtocolBuf, PROTOCOL_BUF_SIZE - 1);
          protocolLastCommand[PROTOCOL_BUF_SIZE - 1] = '\0';
          processDefaultProtocolCommand(tcpProtocolBuf, tcpProtocolBufIdx);
          tcpProtocolBufIdx = 0;
        }
      } else {
        if (tcpProtocolBufIdx < PROTOCOL_BUF_SIZE - 1) {
          tcpProtocolBuf[tcpProtocolBufIdx++] = ch;
        } else {
          // Buffer overflow — discard oversized command and recover so the
          // parser state can't wedge on a malformed stream.
          {
            char ov[PROTOCOL_BUF_SIZE + 16];
            snprintf(ov, sizeof(ov), "[OVERFLOW] %.*s", PROTOCOL_BUF_SIZE - 1, tcpProtocolBuf);
            dbgLogCapture("RX", "TCP", ov);
          }
          tcpProtocolBufIdx = 0;
          tcpProtocolBuf[0] = '\0';
          showOledWarning("ERR: OVERFLOW");
        }
      }
    }
  }
}

// Send a protocol reply back over the SAME interface the command arrived on.
// A command received on USB serial is answered on the serial wire; a command
// received over TCP is answered over TCP. Replies never cross interfaces —
// this keeps each stream clean for its own client (the old unconditional
// serial echo broke GS-232A/B clients; see commit d4614b5).
void protocolSend(const String& response) {
  const char* lineEnding = getProtocolLineEnding();
  String fullResponse = response + lineEnding;

  // Route the reply to the interface that delivered the command
  bool delivered = false;
  if (protocolRxSource == RX_SRC_SER) {
    Serial.print(fullResponse);
    delivered = true;
  } else if (rotctlConnected && rotctlClient.connected()) {
    rotctlClient.print(fullResponse);
    delivered = true;
  }

  // Debug log: record where the response went. "DROP" = generated but no
  // delivery target on the command's own interface.
  dbgLogCapture("TX", delivered ? (protocolRxSource == RX_SRC_SER ? "SER" : "TCP") : "DROP", fullResponse.c_str());

  strncpy(protocolLastResponse, response.c_str(), PROTOCOL_BUF_SIZE - 1);
  protocolLastResponse[PROTOCOL_BUF_SIZE - 1] = '\0';
}

// Feed a single character into the protocol parser (used by both Serial and TCP)
void processProtocolChar(char ch) {
  protocolLastRxTime = millis();
  if (ch == '\n' || ch == '\r') {
    if (protocolBufIdx > 0) {
      protocolBuf[protocolBufIdx] = '\0';
      strncpy(protocolLastCommand, protocolBuf, PROTOCOL_BUF_SIZE - 1);
      protocolLastCommand[PROTOCOL_BUF_SIZE - 1] = '\0';
      DBG_PRINT("PROTOCOL:RX:"); DBG_PRINTLN(protocolBuf);
      {
        // Debug log: exact line as received on USB serial, terminator included
        char rawLine[PROTOCOL_BUF_SIZE + 1];
        memcpy(rawLine, protocolBuf, protocolBufIdx);
        rawLine[protocolBufIdx] = ch;
        rawLine[protocolBufIdx + 1] = '\0';
        dbgLogCapture("RX", "SER", rawLine);
        }

        // This command arrived over USB serial — replies must go back over serial
        protocolRxSource = RX_SRC_SER;

        // Special command: MODE:ROTATOR (exact match — a prefix match would
      // trigger on any longer line merely starting with "MODE:ROTATOR")
      if (protocolBufIdx == 12 && strncmp(protocolBuf, "MODE:ROTATOR", 12) == 0) {
        stopAll();
        exitCalibrationMode();
        currentMenu = nullptr;
        protocolSend("MODE:ROTATOR");
        protocolBufIdx = 0;
        protocolLastCmd = millis();
        return;
      }
      
      // Route based on selected protocol variant
      switch (currentProtocol) {
        case PROTO_GS232A:
        case PROTO_GS232B:
          processGs232Command(protocolBuf, protocolBufIdx);
          break;
        case PROTO_EASYCOM:
          processEasycomCommand(protocolBuf, protocolBufIdx);
          break;
      }
      
      protocolBufIdx = 0;
      protocolLastCmd = millis();
    }
  } else {
    if (protocolBufIdx < PROTOCOL_BUF_SIZE - 1) {
      protocolBuf[protocolBufIdx++] = ch;
    } else {
      // Buffer overflow — discard the oversized command and recover.
      // Without this, every subsequent char is dropped silently until the
      // next newline, wedging the protocol state machine.
      {
        char ov[PROTOCOL_BUF_SIZE + 16];
        snprintf(ov, sizeof(ov), "[OVERFLOW] %.*s", PROTOCOL_BUF_SIZE - 1, protocolBuf);
        dbgLogCapture("RX", "SER", ov);
      }
      protocolBufIdx = 0;
      protocolBuf[0] = '\0';
      showOledWarning("ERR: OVERFLOW");
    }
  }
}

// Detect a stalled partial command in the protocol buffer (chars received
// but no terminator within PROTOCOL_PARTIAL_TIMEOUT_MS). Flush it so the
// parser state can't wedge on an unterminated or malformed command stream.
// Called from loop() — never blocks.
void checkProtocolStall() {
  if (protocolBufIdx == 0) return;
  if (millis() - protocolLastRxTime < PROTOCOL_PARTIAL_TIMEOUT_MS) return;
  DBG_PRINTLN("PROTOCOL:STALL: flushing partial command");
  {
    char st[PROTOCOL_BUF_SIZE + 32];
    snprintf(st, sizeof(st), "[STALL: no terminator within 500ms] %.*s", PROTOCOL_BUF_SIZE - 1, protocolBuf);
    dbgLogCapture("RX", "SER", st);
  }
  protocolBufIdx = 0;
  protocolBuf[0] = '\0';
  showOledWarning("ERR: NO EOL");
}

// ==================== PROGMEM HTML PAGES ====================
// All HTML lives in flash, zero RAM overhead at runtime

const char html_portal[] PROGMEM = R"RAWHTML(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:40px 20px}
h1{color:#00d4ff}
form{background:#16213e;padding:20px;border-radius:8px;max-width:300px;margin:20px auto}
label{display:block;text-align:left;margin:10px 0 5px;color:#00d4ff}
input{width:100%;padding:10px;border:none;border-radius:4px;background:#0f3460;color:#fff;font-size:16px;box-sizing:border-box}
button{margin-top:20px;padding:12px 30px;background:#00d4ff;color:#1a1a2e;border:none;border-radius:4px;font-size:16px;font-weight:bold;cursor:pointer}
</style></head><body>
<h1>AZELCTRL Setup</h1>
<p>Enter your home WiFi credentials</p>
<form method='POST' action='/save'>
<label>WiFi Name (SSID)</label>
<input name='ssid' required placeholder='Your WiFi name'>
<label>Password</label>
<input name='pass' type='password' placeholder='Your WiFi password'>
<button type='submit'>Save & Connect</button>
</form></body></html>
)RAWHTML";

const char html_portal_saved[] PROGMEM = R"RAWHTML(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:60px 20px}
h1{color:#00d4ff}
p{font-size:18px}
</style></head><body>
<h1>Saved!</h1>
<p>WiFi credentials saved to device.</p>
<p>You can now disconnect and use the Connect option on the device.</p>
</body></html>
)RAWHTML";

// Protocol debug log viewer — served at /debug-usb (opens in its own tab).
// Polls /debug-log while open; firmware capture stops ~5 s after close.
const char html_debug_usb[] PROGMEM = R"RAWHTML(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>AZELCtrl Debug USB</title>
<style>
body{font-family:Consolas,Menlo,monospace;background:#1a1a2e;color:#eee;margin:0;padding:14px}
h1{color:#00d4ff;font-size:15px;margin:0 0 3px}
.sub{color:#888;font-size:11px;margin-bottom:8px;line-height:1.4}
.bar{margin-bottom:8px}
button{padding:6px 14px;background:#00d4ff;color:#1a1a2e;border:none;border-radius:4px;font-weight:bold;cursor:pointer;font-size:12px}
#status{font-size:11px;color:#aaa;margin-left:10px}
pre{background:#0f0f1e;border:1px solid #333;border-radius:6px;padding:10px;height:calc(100vh - 140px);overflow-y:auto;white-space:pre-wrap;word-break:break-all;font-size:12px;line-height:1.5;margin:0}
.rx{color:#7ec8ff}.tx{color:#7dff9e}.ev{color:#ffb347}
</style></head><body>
<h1>AZELCtrl - Protocol Debug Log</h1>
<div class='sub'>USB serial + TCP protocol traffic, exactly as seen by the device.<br>
Logging is active while this tab is open and stops when you close it. Nothing is written to the device's flash.</div>
<div class='bar'><button id='dl'>Download log</button><button id='sync'>Sync time</button><span id='status'>connecting...</span></div>
<pre id='log'></pre>
<script>
// NB: must not be named 'status' - top-level `var status` cannot shadow the
// legacy-unforgeable window.status property, so the element ref was silently
// discarded and the page stuck on "connecting..." forever.
var seq=-1,pre=document.getElementById('log'),statusEl=document.getElementById('status'),syncBtnEl=document.getElementById('sync');
function cls(l){return l.indexOf('] RX')>-1?'rx':(l.indexOf('] TX')>-1?'tx':'ev');}
function poll(){
  fetch('/debug-log?since='+seq).then(function(r){return r.json();}).then(function(j){
    // j.seq is the session TOTAL (next slot index), not the last delivered
    // entry's seq: the handler's scan covers slots start..j.seq-1. Track the
    // highest slot actually scanned so the next poll picks up exactly where
    // this one left off - no skips, no duplicates.
    seq = j.seq > 0 ? j.seq - 1 : -1;
    var atBottom=pre.scrollTop+pre.clientHeight>=pre.scrollHeight-30;
    for(var i=0;i<j.lines.length;i++){
      var d=document.createElement('span');d.className=cls(j.lines[i]);d.textContent=j.lines[i]+'\n';pre.appendChild(d);
    }
    while(pre.childNodes.length>2000)pre.removeChild(pre.firstChild);
    if(atBottom)pre.scrollTop=pre.scrollHeight;
    statusEl.textContent='armed - '+j.seq+' entries captured'+(j.time?' - '+j.time:' (no time - click Sync)');
    statusEl.style.color='#7dff9e';
  }).catch(function(){
    statusEl.textContent='connection lost - retrying';statusEl.style.color='#ff6b6b';
  });
}
document.getElementById('dl').onclick=function(){window.location='/debug-log-download';};
// One-click wall-clock sync: sends this browser's epoch time + UTC offset so
// the device can stamp log lines with real local time (works in AP mode too).
syncBtnEl.onclick=function(){
  syncBtnEl.textContent='syncing...';
  fetch('/set-time?ts='+Date.now()+'&off='+(-new Date().getTimezoneOffset()*60))
    .then(function(r){return r.json();})
    .then(function(j){syncBtnEl.textContent=j.ok?'time synced':'sync failed';})
    .catch(function(){syncBtnEl.textContent='sync failed';});
};
poll();setInterval(poll,600);
</script></body></html>
)RAWHTML";

// Satellite tracker HTML — served from SPIFFS (index.html.gz) with PROGMEM fallback

// Helper: send PROGMEM content via web server
void sendProgmemHTML(const char* pgmHtml) {
  server.setContentLength(strlen_P(pgmHtml));
  server.send(200, "text/html; charset=utf-8");
  server.sendContent(pgmHtml);
}

// Serve gzip-compressed HTML from SPIFFS, fallback to PROGMEM
void sendHtml() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.keepAlive(false);
  if (SPIFFS.begin() && SPIFFS.exists(spiffsHtmlFile)) {
    File f = SPIFFS.open(spiffsHtmlFile, "r");
    if (f.size() > 0) {
      server.setContentLength(f.size());
      server.sendHeader("Content-Encoding", "gzip");
      server.send(200, "text/html");
      const size_t chunkSize = 1024;
      uint8_t buf[chunkSize];
      while (f.available()) {
        size_t toRead = f.available() < chunkSize ? f.available() : chunkSize;
        f.read(buf, toRead);
        server.sendContent((const char*)buf, toRead);
        yield();
        ESP.wdtFeed();
      }
      f.close();
      return;
    }
    f.close();
  }
  // Fallback: serve from PROGMEM
  server.setContentLength(sizeof(index_html_gz));
  server.sendHeader("Content-Encoding", "gzip");
  server.send(200, "text/html");
  // Send in chunks, reading from PROGMEM properly with memcpy_P
  // Abort early if client disconnects
  const size_t progmemChunk = 256;
  uint8_t sendBuf[progmemChunk];
  size_t sent = 0;
  while (sent < sizeof(index_html_gz)) {
    if (!server.client().connected()) break;
    size_t toSend = min(progmemChunk, sizeof(index_html_gz) - sent);
    memcpy_P(sendBuf, index_html_gz + sent, toSend);
    server.sendContent((const char*)sendBuf, toSend);
    sent += toSend;
    yield();
    ESP.wdtFeed();
  }
}

// Serve world coastline data from SPIFFS (gzip-compressed, returns 404 if not found)
void sendCoastlineData() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.keepAlive(false);
  if (SPIFFS.exists(spiffsCoastlineFile)) {
    File f = SPIFFS.open(spiffsCoastlineFile, "r");
    if (f.size() > 0) {
      server.setContentLength(f.size());
      server.sendHeader("Content-Encoding", "gzip");
      server.send(200, "application/javascript");
      const size_t chunkSize = 1024;
      uint8_t buf[chunkSize];
      while (f.available()) {
        size_t toRead = f.available() < chunkSize ? f.available() : chunkSize;
        f.read(buf, toRead);
        server.sendContent((const char*)buf, toRead);
        yield();
        ESP.wdtFeed();
      }
      f.close();
      return;
    }
    f.close();
  }
  server.send(404, "text/plain", "world_coastline.js.gz not found. Upload via Manage tab.");
}

// Upload a file to SPIFFS
void handleUpload() {
  HTTPUpload& upload = server.upload();
  static File spiffsFile;
  static String uploadPath;
  static bool uploadWriteError = false;
  static size_t uploadBytesWritten = 0;

  if (upload.status == UPLOAD_FILE_START) {
    // Sanitize filename — strip path, allow alphanumeric + ._-
    String filename = upload.filename;
    int lastSlash = filename.lastIndexOf('/');
    if (lastSlash >= 0) filename = filename.substring(lastSlash + 1);
    uploadPath = "/" + filename;

    // Remove old file if exists
    if (SPIFFS.exists(uploadPath)) {
      SPIFFS.remove(uploadPath);
    }
    spiffsFile = SPIFFS.open(uploadPath, "w");
    uploadWriteError = !spiffsFile;
    uploadBytesWritten = 0;
    Serial.print("Upload: start ");
    Serial.println(uploadPath);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (spiffsFile) {
      // Verify the write — SPIFFS can fail mid-file under RAM pressure;
      // without this a truncated file would be served silently.
      size_t n = spiffsFile.write(upload.buf, upload.currentSize);
      if (n != upload.currentSize) {
        uploadWriteError = true;
      } else {
        uploadBytesWritten += n;
      }
    } else {
      uploadWriteError = true;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (spiffsFile) {
      spiffsFile.close();
    }
    if (uploadWriteError || (size_t)upload.totalSize != uploadBytesWritten) {
      // Incomplete write — remove the partial file so the portal falls back
      // to the built-in copy instead of serving a corrupted page.
      if (SPIFFS.exists(uploadPath)) {
        SPIFFS.remove(uploadPath);
      }
      Serial.println("Upload: FAILED (write error or size mismatch) — partial file removed, retry the upload");
      server.send(500, "text/plain", "Upload failed: incomplete write. Please retry.");
    } else {
      Serial.print("Upload: done ");
      Serial.print(uploadPath);
      Serial.print(" (");
      Serial.print(upload.totalSize);
      Serial.println(" bytes)");
      server.send(200, "text/plain", "Upload complete");
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (spiffsFile) {
      spiffsFile.close();
    }
    if (SPIFFS.exists(uploadPath)) {
      SPIFFS.remove(uploadPath);
    }
    server.send(500, "text/plain", "Upload failed");
  }
}

// Get Content-Type based on file extension
const char* getContentType(const String& path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css"))  return "text/css";
  if (path.endsWith(".js"))   return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".gif"))  return "image/gif";
  if (path.endsWith(".ico"))  return "image/x-icon";
  if (path.endsWith(".webp")) return "image/webp";
  if (path.endsWith(".gz"))   return "application/gzip";
  return "application/octet-stream";
}

// List all files in SPIFFS as JSON
void listSpiffsFiles() {
  Dir dir = SPIFFS.openDir("/");
  String json = "[";
  bool first = true;
  while (dir.next()) {
    if (!first) json += ",";
    first = false;
    json += "{";
    json += "\"name\":\"" + dir.fileName() + "\",";
    json += "\"size\":" + String(dir.fileSize());
    json += "}";
  }
  json += "]";
  // Include filesystem capacity so clients can diagnose partial uploads
  FSInfo fsInfo;
  if (SPIFFS.info(fsInfo)) {
    String out = "{\"files\":" + json + ",";
    out += "\"total\":" + String(fsInfo.totalBytes) + ",";
    out += "\"free\":" + String((uint32_t)(fsInfo.totalBytes - fsInfo.usedBytes)) + "}";
    server.send(200, "application/json", out);
  } else {
    server.send(200, "application/json", json);
  }
}

// Delete a file from SPIFFS
void deleteSpiffsFile() {
  String path = server.arg("path");
  if (path.isEmpty() || !SPIFFS.exists(path)) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  if (SPIFFS.remove(path)) {
    server.send(200, "text/plain", "Deleted: " + path);
  } else {
    server.send(500, "text/plain", "Delete failed");
  }
}

// Serve files from SPIFFS via /files/ path
void serveSpiffsFile(const String& path) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.keepAlive(false);
  // Remove leading /files/ prefix
  String spiffsPath = path;
  if (spiffsPath.startsWith("/files/")) {
    spiffsPath = spiffsPath.substring(7);  // Remove "/files/"
  }
  if (!spiffsPath.startsWith("/")) {
    spiffsPath = "/" + spiffsPath;
  }

  if (!SPIFFS.exists(spiffsPath)) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  File f = SPIFFS.open(spiffsPath, "r");
  if (!f || f.size() == 0) {
    server.send(404, "text/plain", "File not found");
    f.close();
    return;
  }

  const char* contentType = getContentType(spiffsPath);
  bool isGzipped = spiffsPath.endsWith(".gz");

  server.setContentLength(f.size());
  if (isGzipped) {
    server.sendHeader("Content-Encoding", "gzip");
  }
  // Use send() with empty body - setContentLength() must be called BEFORE send()
  server.send(200, contentType, "");
  // Now stream the file content
  const size_t chunkSize = 512;
  uint8_t buf[chunkSize];
  size_t remaining = f.size();
  while (remaining > 0) {
    size_t toRead = remaining < chunkSize ? remaining : chunkSize;
    size_t bytesRead = f.read(buf, toRead);
    if (bytesRead > 0) {
      server.sendContent(reinterpret_cast<char*>(buf), bytesRead);
      remaining -= bytesRead;
    } else {
      break;
    }
  }
  f.close();
}

void handlePortal() {
  sendProgmemHTML(html_portal);
}

void handleSaveWifi() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  
  if (ssid.length() > 0) {
    Serial.print("Saving WiFi: SSID=");
    Serial.println(ssid);
    saveWifiCredentials(ssid.c_str(), pass.c_str());
    
    sendProgmemHTML(html_portal_saved);
    
    delay(2000);  // Let the response reach the phone
    Serial.println("Stopping captive portal after WiFi save...");
    stopCaptivePortal();
    // Does NOT reboot - returns control to Station Settings screen
  } else {
    // Empty submission (e.g., captive portal detection) - redirect to portal
    Serial.println("handleSaveWifi: empty SSID, returning portal");
    sendProgmemHTML(html_portal);
  }
}

void tickCaptivePortal() {
  if (captivePortalActive) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
}

// ==================== SAFETY: DIRECTION REVERSE PAUSE ====================
const unsigned long REVERSE_PAUSE_MS = 600;

enum AxisDirection {
  DIR_STOPPED,
  DIR_CW,
  DIR_CCW,
  DIR_UP,
  DIR_DOWN
};

AxisDirection azDirection = DIR_STOPPED;
AxisDirection elDirection = DIR_STOPPED;
// Track mechanical offset from center for direction calculation
// Positive = CW from center, Negative = CCW from center
// N360/S360: range -180° to +180°
// N450/S450: range -225° to +225°
float mechanicalAzOffset = 0.0;  // Current mechanical offset from center
float mechanicalElOffset = 0.0;  // Current mechanical offset from center
float lastAzReading = 0.0;       // Previous azimuth reading for delta calculation

bool isPausing = false;
unsigned long pauseStartTime = 0;
String pendingDir = "";
bool pauseAz = false;
bool pauseEl = false;

void checkPauseComplete() {
  if (!isPausing) return;
  
  unsigned long elapsed = millis() - pauseStartTime;
  if (elapsed >= REVERSE_PAUSE_MS) {
    if (pauseAz) pauseAz = false;
    if (pauseEl) pauseEl = false;
    
    if (!pauseAz && !pauseEl) {
      isPausing = false;
      executeMove(pendingDir);
      pendingDir = "";
    } else {
      if (!pauseAz) executeAzimuthMove(pendingDir);
      else if (!pauseEl) executeElevationMove(pendingDir);
    }
  }
}

// ==================== CALIBRATION CONSTANTS ====================
const unsigned long CAL_SWEEP_TIMEOUT = 120000;  // 2 minutes per sweep
const unsigned long CAL_DETECT_TIME = 6000;      // 6 seconds to detect cable connection (3s each direction)
const float CAL_MAX_SAFE_V = 3.250F;             // Hard voltage safety limit
const float CAL_STABILITY_THRESHOLD = 15.0F;     // mV - voltage change over stability window
const float CAL_MIN_RANGE = 0.5F;                // Minimum valid range (maxV - minV)
const float CAL_DETECT_MIN_CHANGE = 70.0F;       // mV - minimum voltage change to confirm cable
const float CAL_ASSUMED_PRE_CAL_MAX = 3.200F;    // Assumed max voltage before calibration
const unsigned long CAL_HEADING_PAUSE = 5000;    // Pause 5s at each cardinal heading during return sweep
#define CAL_V_SAMPLES 20

// Rotator model - single enum for all 4 options
enum RotModel { ROT_N360, ROT_S360, ROT_N450, ROT_S450 };
RotModel rotModel = ROT_N360;

// Calibrated voltage ranges (loaded from EEPROM)
float calAzMinV = 0.0F, calAzMaxV = 3.2F;
float calElMinV = 0.0F, calElMaxV = 3.2F;
bool calAzValid = false, calElValid = false;

// ====================== MOVEMENT CONTROL ======================
void stopAzimuth() {
  digitalWrite(AZ_ENABLE, LOW);
  digitalWrite(AZ_DIR_CW, HIGH);
  azDirection = DIR_STOPPED;
  azEngageStart = 0;  // Clear engagement timer
  // Don't clear protocolTargetAzSet here — it's cleared by auto-stop or explicit stop commands
  protocolAzReversing = false;
  // Clear movement indicator if azimuth was moving
  if (currentMove == "LEFT" || currentMove == "RIGHT" || currentMove == "AZ POT") {
    currentMove = "IDLE";
  }
}

void stopElevation() {
  digitalWrite(EL_ENABLE, LOW);
  digitalWrite(EL_DIR_UP, HIGH);
  elDirection = DIR_STOPPED;
  elEngageStart = 0;  // Clear engagement timer
  protocolTargetElSet = false;  // Clear so tolerance check works on next target
  protocolElReversing = false;
  // Clear movement indicator if elevation was moving
  if (currentMove == "UP" || currentMove == "DOWN" || currentMove == "EL POT") {
    currentMove = "IDLE";
  }
}

void stopAll() {
  stopAzimuth();
  stopElevation();
  moving = false;
  currentMove = "IDLE";
  // Clear protocol targets so auto-stop logic doesn't restart movement
  protocolTargetAzSet = false;
  protocolTargetElSet = false;
  protocolAzReversing = false;
  protocolElReversing = false;
}

void executeAzimuthMove(String dir) {
  // Only stop if actually moving in a DIFFERENT direction — skip if idle or same direction
  bool needStop = (azDirection != DIR_STOPPED && azDirection != DIR_CCW && dir == "LEFT") ||
                  (azDirection != DIR_STOPPED && azDirection != DIR_CW  && dir == "RIGHT");
  if (needStop) {
    stopAzimuth();
  } else if (azDirection == DIR_STOPPED) {
    // Already stopped — just ensure pins are clean (no-op, but explicit)
    digitalWrite(AZ_ENABLE, LOW);
    digitalWrite(AZ_DIR_CW, HIGH);
  }
  
  // Check voltage safety before moving
  if (adsPresent && calAzValid) {
    float azVoltage = (float)ads.readADC_SingleEnded(0) / 32767.0 * 4.096F;
    if (dir == "RIGHT" && azVoltage >= CAL_MAX_SAFE_V) {
      // Safety limit — cannot move RIGHT (AZ)
      return;
    }
  }
  
  if (currentMove == "IDLE" || currentMove == "UP" || currentMove == "DOWN") {
    currentMove = dir;
  }
  moving = true;
  
  // Mark engagement start — use current Az as the last known good position
  azPositionAtEngage = displayAzimuth(currentAz);
  azEngageStart = millis();
  
  if (dir == "LEFT") {
    azDirection = DIR_CCW;
    digitalWrite(AZ_DIR_CW, HIGH);
    digitalWrite(AZ_ENABLE, HIGH);
  }
  if (dir == "RIGHT") {
    azDirection = DIR_CW;
    digitalWrite(AZ_DIR_CW, LOW);
    digitalWrite(AZ_ENABLE, HIGH);
  }
}

void executeElevationMove(String dir) {
  // Only stop if actually moving in a DIFFERENT direction — skip if idle or same direction
  bool needStop = (elDirection != DIR_STOPPED && elDirection != DIR_DOWN && dir == "UP") ||
                  (elDirection != DIR_STOPPED && elDirection != DIR_UP  && dir == "DOWN");
  if (needStop) {
    stopElevation();
  } else if (elDirection == DIR_STOPPED) {
    // Already stopped — just ensure pins are clean (no-op, but explicit)
    digitalWrite(EL_ENABLE, LOW);
    digitalWrite(EL_DIR_UP, HIGH);
  }
  
  // Check voltage safety before moving
  if (adsPresent && calElValid) {
    float elVoltage = (float)ads.readADC_SingleEnded(1) / 32767.0 * 4.096F;
    if (dir == "UP" && elVoltage >= CAL_MAX_SAFE_V) {
      // Safety limit — cannot move UP (EL)
      return;
    }
  }
  
  if (currentMove == "IDLE" || currentMove == "LEFT" || currentMove == "RIGHT") {
    currentMove = dir;
  }
  moving = true;
  
  // Mark engagement start — use current El as the last known good position
  elPositionAtEngage = currentEl;
  elEngageStart = millis();
  
  if (dir == "UP") {
    elDirection = DIR_UP;
    digitalWrite(EL_DIR_UP, LOW);
    digitalWrite(EL_ENABLE, HIGH);
  }
  if (dir == "DOWN") {
    elDirection = DIR_DOWN;
    digitalWrite(EL_DIR_UP, HIGH);
    digitalWrite(EL_ENABLE, HIGH);
  }
}

void executeMove(String dir) {
  if (dir == "LEFT" || dir == "RIGHT") {
    executeAzimuthMove(dir);
  } else if (dir == "UP" || dir == "DOWN") {
    executeElevationMove(dir);
  }
}

void startMove(String dir) {
  bool azNeedsPause = false;
  bool elNeedsPause = false;
  
  if (dir == "LEFT" || dir == "RIGHT") {
    if (azDirection != DIR_STOPPED) {
      azNeedsPause = (dir == "LEFT" && azDirection == DIR_CW) ||
                     (dir == "RIGHT" && azDirection == DIR_CCW);
    }
  }
  
  if (dir == "UP" || dir == "DOWN") {
    if (elDirection != DIR_STOPPED) {
      elNeedsPause = (dir == "UP" && elDirection == DIR_DOWN) ||
                     (dir == "DOWN" && elDirection == DIR_UP);
    }
  }
  
  if (!azNeedsPause && !elNeedsPause) {
    executeMove(dir);
    return;
  }
  
  if (azNeedsPause) {
    stopAzimuth();
    pauseAz = true;
  }
  if (elNeedsPause) {
    stopElevation();
    pauseEl = true;
  }
  
  if (!isPausing) {
    isPausing = true;
    pauseStartTime = millis();
    pendingDir = dir;
  }
}

// ====================== CALIBRATION (VOLTAGE-BASED) ======================
// Calibration finds min/max voltages by sweeping to hard limits.
// AZ: CCW→min (~0V at -180°), CW→max (~3.2V at +180°)
// EL: DOWN→min (~0V at 0°), UP→max (~3.2V at 90°)
// Safety: never exceed CAL_MAX_SAFE_V (3.250V)
// Results saved to EEPROM so calibration persists across reboots.

enum CalState {
  CAL_IDLE,
  CAL_SWEEP_MIN,        // Move CCW/DOWN until voltage stabilizes (also detects cable)
  CAL_SWEEP_MAX,        // Move CW/UP until voltage stabilizes or hits safety limit
  CAL_ADJUST_MAX,       // Prompt user to manually adjust to exact heading (180° AZ / 90° EL)
  CAL_RETURN_MIN,       // Return sweep CCW/DOWN to minimum, pausing at cardinal headings
  CAL_COMPLETE,         // Calibration complete, saved to EEPROM
  CAL_ERROR             // Error state with message
};

CalState calState = CAL_IDLE;
int calCurrentAxis = -1;  // 0=AZ, 1=EL, -1=none
int calDirection = 0;     // 1=CW/UP, -1=CCW/DOWN
unsigned long calPhaseStart = 0;

float calMinVoltage = 0;
float calMaxVoltage = 0;
float calStabilityMax = 0;
float calStabilityMin = 0;
unsigned long calStabilityStart = 0;
String calErrorMessage = "";

// Return sweep tracking (CAL_RETURN_MIN)
String calCurrentHeading = "";
unsigned long calHeadingPauseStart = 0;
int calLastHeadingIndex = -1;  // Track which cardinal heading we last paused at

// Voltage samples for stability detection
float calVoltageSamples[CAL_V_SAMPLES];
int calVSampleIdx = 0;
float calInitialVoltage = 0;  // Voltage at start of CAL_SWEEP_MIN (for cable detection)

// Estimate heading from voltage during calibration (before calAzValid/calElValid is set)
// Uses rotator model offset/span + assumed or actual voltage range
float calEstimatedHeading(float voltage, int axis) {
  if (axis == 0) {
    // AZ: use rotator model offset/span
    float minV = calMinVoltage;
    float maxV = calMaxVoltage;
    float range = maxV - minV;
    if (range < 0.01F) {
      // No calibration data yet — assume ~3.2V range from 0V
      range = CAL_ASSUMED_PRE_CAL_MAX;
      minV = 0;
      maxV = CAL_ASSUMED_PRE_CAL_MAX;
    }
    float fraction = (voltage - minV) / range;
    fraction = constrain(fraction, 0, 1);
    return azOffset() + (fraction * azSpan());
  } else {
    // EL: 0 to 180
    float minV = calMinVoltage;
    float maxV = calMaxVoltage;
    float range = maxV - minV;
    if (range < 0.01F) {
      range = CAL_ASSUMED_PRE_CAL_MAX;
      minV = 0;
      maxV = CAL_ASSUMED_PRE_CAL_MAX;
    }
    float fraction = (voltage - minV) / range;
    fraction = constrain(fraction, 0, 1);
    return fraction * 180.0F;
  }
}

// Cardinal headings for the return sweep (ordered max→min for CCW sweep)
// Uses raw mechanical headings from calEstimatedHeading() (no compass conversion needed)
// Full range covers all variants:
// Index 0: +270° = W (N450/S450 start only)
// Index 1: +225° = SW (N450/S450 only)
// Index 2: +180° = S (all variants start here or later)
// Index 3: +135° = SE
// Index 4: +90° = E
// Index 5: +45° = NE
// Index 6: 0° = N
// Index 7: -45° = NW
// Index 8: -90° = W
// Index 9: -135° = SW
// Index 10: -180° = S (end for all variants)
const char* calCardinalNames[] = {"W", "SW", "S", "SE", "E", "NE", "N", "NW", "W", "SW", "S"};
const float calCardinalAngles[] = {270, 225, 180, 135, 90, 45, 0, -45, -90, -135, -180};
const int calCardinalCount = 11;

// Get the next cardinal heading index we should pause at during return sweep
// Returns -1 when no more headings to pause at
int calNextCardinalHeading(float currentHeading) {
  // Calculate the last heading index for this variant
  // N360/S360: start at index 2 (S=+180), end at index 10 (S=-180)
  // N450/S450: start at index 0 (W=+270), end at index 10 (S=-180)
  int startIndex = (rotModel == ROT_N450 || rotModel == ROT_S450) ? 0 : 2;
  int lastIndex = 10;
  
  // Clamp calLastHeadingIndex to valid range
  if (calLastHeadingIndex < startIndex) calLastHeadingIndex = startIndex;
  
  // We're sweeping CCW from max toward min, so mechanical headings decrease
  for (int i = calLastHeadingIndex + 1; i <= lastIndex; i++) {
    float tolerance = (i == lastIndex) ? 0.0F : 1.0F;
    if (currentHeading <= calCardinalAngles[i] + tolerance) {
      Serial.print("CAL: Heading ");
      Serial.print(currentHeading, 1);
      Serial.print("° reached cardinal ");
      Serial.println(calCardinalNames[i]);
      return i;
    }
  }
  return -1;
}

// Calibrated voltage ranges (loaded from EEPROM) — declared with constants above
// float calAzMinV, calAzMaxV, calElMinV, calElMaxV, calAzValid, calElValid

void loadCalibration() {
  if (EEPROM.read(CAL_EEPROM_MAGIC) != CAL_EEPROM_MAGIC_VAL) {
    return;
  }
  uint8_t* bytes = (uint8_t*)&calAzMinV;
  for (int i = 0; i < 4; i++) bytes[i] = EEPROM.read(CAL_EEPROM_AZ_MIN + i);
  bytes = (uint8_t*)&calAzMaxV;
  for (int i = 0; i < 4; i++) bytes[i] = EEPROM.read(CAL_EEPROM_AZ_MAX + i);
  bytes = (uint8_t*)&calElMinV;
  for (int i = 0; i < 4; i++) bytes[i] = EEPROM.read(CAL_EEPROM_EL_MIN + i);
  bytes = (uint8_t*)&calElMaxV;
  for (int i = 0; i < 4; i++) bytes[i] = EEPROM.read(CAL_EEPROM_EL_MAX + i);
  
  calAzValid = (calAzMaxV - calAzMinV > CAL_MIN_RANGE);
  calElValid = (calElMaxV - calElMinV > CAL_MIN_RANGE);
}

void saveCalibration() {
  uint8_t* bytes;
  if (calCurrentAxis == 0) {
    bytes = (uint8_t*)&calAzMinV;
    for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_AZ_MIN + i, bytes[i]);
    bytes = (uint8_t*)&calAzMaxV;
    for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_AZ_MAX + i, bytes[i]);
    calAzValid = true;
  } else {
    bytes = (uint8_t*)&calElMinV;
    for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_EL_MIN + i, bytes[i]);
    bytes = (uint8_t*)&calElMaxV;
    for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_EL_MAX + i, bytes[i]);
    calElValid = true;
  }
  EEPROM.write(CAL_EEPROM_MAGIC, CAL_EEPROM_MAGIC_VAL);
  EEPROM.commit();
  Serial.println("CAL: Calibration saved to EEPROM");
}

void loadWifiMode() {
  if (EEPROM.read(WIFI_MODE_MAGIC) != WIFI_MODE_MAGIC_VAL) {
    Serial.println("WIFI: No WiFi mode in EEPROM - defaulting to DISABLED");
    deviceWifiMode = MODE_DISABLED;
    return;
  }
  deviceWifiMode = (DeviceWifiMode)EEPROM.read(WIFI_MODE_EEPROM);
  Serial.print("WIFI: Loaded mode = ");
  if (deviceWifiMode == MODE_STA) Serial.println("STA");
  else if (deviceWifiMode == MODE_AP) Serial.println("AP");
  else Serial.println("DISABLED");
}

void saveWifiMode() {
  EEPROM.write(WIFI_MODE_EEPROM, (uint8_t)deviceWifiMode);
  EEPROM.write(WIFI_MODE_MAGIC, WIFI_MODE_MAGIC_VAL);
  EEPROM.commit();
  Serial.println("WIFI: WiFi mode saved to EEPROM");
}

void loadApPassword() {
  uint8_t magic = EEPROM.read(AP_PASS_EEPROM_MAGIC);
  if (magic == AP_PASS_EEPROM_MAGIC_VAL) {
    for (int i = 0; i < 8; i++) {
      apPassword[i] = EEPROM.read(AP_PASS_EEPROM_START + i);
    }
    apPassword[8] = '\0';
    Serial.print("AP: Loaded password = ");
    Serial.println(apPassword);
  } else {
    // Default password
    strcpy(apPassword, "yaesu123");
    Serial.println("AP: Using default password");
  }
}

void saveApPassword() {
  for (int i = 0; i < 8; i++) {
    EEPROM.write(AP_PASS_EEPROM_START + i, apPassword[i]);
  }
  EEPROM.write(AP_PASS_EEPROM_MAGIC, AP_PASS_EEPROM_MAGIC_VAL);
  EEPROM.commit();
  Serial.print("AP: Password saved = ");
  Serial.println(apPassword);
}

bool hasRotatorModel() {
  return EEPROM.read(ROT_EEPROM_MAGIC) == ROT_EEPROM_MAGIC_VAL;
}

bool hasAnySettings() {
  return hasRotatorModel() && EEPROM.read(WIFI_MODE_MAGIC) == WIFI_MODE_MAGIC_VAL;
}

void loadRotatorModel() {
  if (EEPROM.read(ROT_EEPROM_MAGIC) != ROT_EEPROM_MAGIC_VAL) {
    Serial.println("ROT: No rotator model in EEPROM - will prompt at boot");
    return;
  }
  rotModel = (RotModel)EEPROM.read(ROT_EEPROM_MODEL);
  Serial.print("ROT: Loaded model = ");
  Serial.println(rotModel);
}

// ELF mode persistence
void loadElfMode() {
  elfMode = (EEPROM.read(ELF_EEPROM_BYTE) == 1);
  Serial.print("ELF: Mode = ");
  Serial.println(elfMode ? "ON" : "OFF");
}

void saveElfMode() {
  EEPROM.write(ELF_EEPROM_BYTE, elfMode ? 1 : 0);
  EEPROM.commit();
}

void saveRotatorModel(bool clearCal = false) {
  EEPROM.write(ROT_EEPROM_MODEL, (uint8_t)rotModel);
  EEPROM.write(ROT_EEPROM_MAGIC, ROT_EEPROM_MAGIC_VAL);
  EEPROM.commit();
  Serial.println("ROT: Rotator model saved to EEPROM");
  
  if (clearCal) {
    // Clear calibration when model changes — azimuth range may have changed
    clearCalibration();
  }
}

void clearCalibration() {
  // Invalidate calibration magic byte
  EEPROM.write(CAL_EEPROM_MAGIC, 0);
  EEPROM.commit();
  calAzValid = false;
  calElValid = false;
  calAzMinV = calAzMaxV = calElMinV = calElMaxV = 0;
  Serial.println("CAL: Calibration cleared — recalibrate required");
}

String azRangeLabel() {
  switch (rotModel) {
    case ROT_N360: return "-180 to +180";
    case ROT_S360: return "0 to 360";
    case ROT_N450: return "-180 to +270";
    case ROT_S450: return "0 to 450";
  }
  return "";
}

float azSpan() {
  if (rotModel == ROT_N450 || rotModel == ROT_S450) return 450.0F;
  return 360.0F;
}

float azOffset() {
  if (rotModel == ROT_S360 || rotModel == ROT_S450) return 0.0F;
  return -180.0F;
}

// Convert azimuth to display format (always positive, matching controller display)
// N360: -180→180, -90→270, 0→0, +90→90, +180→180
// S360: no change (already 0-360)
// N450: -180→180, -90→270, 0→0, +90→90, +180→180, +270→270
// S450: no change (already 0-450)
float displayAzimuth(float az) {
  if (az < 0) az += 360.0F;
  return az;
}

void setupRotatorModel() {
  Serial.println("ROT: Starting rotator model setup");

  const char* modelLabels[4] = {"N360 (-180 to 180)", "S360 (0 to 360)", "N450 (-225 to 225)", "S450 (-45 to 405)"};
  int choice = rotModel;  // Default to current
  bool firstDraw = true;
  bool btn1WasPressed = false;
  bool btn2WasPressed = false;

  while (true) {
    // Check for MODE:ROTATOR command to escape blocking menu
    if (checkModeCommand()) return;

    bool btn1 = digitalRead(BTN1);      // HIGH = pressed
    bool btn2 = !digitalRead(BTN2);     // LOW = pressed

    display.clearDisplay();
    display.setTextSize(1);

    display.fillRect(0, 0, 128, 8, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setCursor(0, 0);
    display.print("Rotator Variant?             ");

    display.drawLine(0, 8, 127, 8, SSD1306_WHITE);

    int y = 12;
    for (int i = 0; i < 4; i++) {
      if (i == choice) {
        display.fillRect(0, y, 128, 8, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }

      display.setCursor(2, y);
      display.print(modelLabels[i]);
      y += 11;
    }

    for (int x = 0; x < 128; x += 3) {
      display.drawPixel(x, 53, SSD1306_WHITE);
    }

    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 56);
    display.println("1.Nav  1|Sel  2.Back");

    display.display();

    // Wait for buttons released on first draw to prevent carryover from long-press
    if (firstDraw) {
      firstDraw = false;
      while (digitalRead(BTN1) || !digitalRead(BTN2)) delay(50);
      delay(100);
      btn1WasPressed = false;
      btn2WasPressed = false;
      continue;
    }

    unsigned long now = millis();

    // Btn1: short-press = cycle, long-press = select
    if (btn1 && !btn1WasPressed) {
      resetOledCmdSuppress();
      unsigned long pressStart = now;
      while (digitalRead(BTN1) && (millis() - pressStart < 800)) {
        delay(20);
      }
      if (millis() - pressStart >= 800) {
        // Long press - select and save
        Serial.print("ROT: Selected model ");
        Serial.println(choice);
        rotModel = (RotModel)choice;
        saveRotatorModel(true);  // Clear calibration when model changes
        // Return immediately — do NOT wait for button release here.
        // The caller (actionSetupRotator) will wait for release on firstDraw.
        return;
      }
      // Short press - cycle choice
      choice = (choice + 1) % 4;
    }

    // Btn2: edge-detect press = back (short or long)
    if (btn2 && !btn2WasPressed) {
      resetOledCmdSuppress();
      // Debounce delay
      delay(BTN_DEBOUNCE_MS);
      Serial.println("ROT: Back from rotator setup");
      // Wait for button release before returning to prevent carryover
      while (!digitalRead(BTN2)) delay(50);
      delay(50);
      return;
    }

    btn1WasPressed = btn1;
    btn2WasPressed = btn2;

    delay(50);
  }
}

void enterCalibrationMode(int axis) {
  calState = CAL_SWEEP_MIN;
  calCurrentAxis = axis;
  calDirection = 0;
  calPhaseStart = millis();
  calMinVoltage = 0;
  calMaxVoltage = 0;
  calStabilityMax = 0;
  calStabilityMin = 32767;
  calStabilityStart = millis();
  calInitialVoltage = 0;
  calVSampleIdx = 0;
  calErrorMessage = "";
  calCurrentHeading = "";
  calHeadingPauseStart = 0;
  calLastHeadingIndex = -1;
  stopAll();
  currentMenu = nullptr;
  
  String axisName = (axis == 0) ? "AZ" : "EL";
  Serial.print("CAL: Entering ");
  Serial.print(axisName);
  Serial.println(" pot calibration mode");
}

void exitCalibrationMode() {
  calState = CAL_IDLE;
  calCurrentAxis = -1;
  calDirection = 0;
  stopAll();
  Serial.println("CAL: Exiting calibration");
}

// Protocol movement helpers — used by both GS-232 and Easycom handlers
void executeProtocolAzMove(float targetAz) {
  // Convert -180..+180 to 0..360 for internal logic
  // This lets all existing direction-finding, mechanical-stop, and ELF logic work unchanged
  if (targetAz < 0.0) targetAz += 360.0;
  if (targetAz > 360.0) targetAz -= 360.0;
  
  // When ELF is on, incoming targets are in satellite space — transform to physical
  float physTarget = elfAz(targetAz);
  
  // Direction calculation: compare in the same coordinate space
  float curForDiff;
  float tgtForDiff;
  if (elfMode) {
    curForDiff = elfAz(currentAz);
    tgtForDiff = targetAz;
  } else {
    curForDiff = displayAzimuth(currentAz);
    tgtForDiff = targetAz;
  }
  float diff = tgtForDiff - curForDiff;
  if (diff > 180.0) diff -= 360.0;
  if (diff < -180.0) diff += 360.0;
  
  // Don't start movement if target is within auto-stop tolerance and we're stopped
  // This prevents relay clicking when new targets arrive within the zone where
  // auto-stop would immediately kill movement anyway
  if (azDirection == DIR_STOPPED && !protocolTargetAzSet && abs(diff) <= PROTOCOL_STOP_TOLERANCE) {
#if DEBUG_ENABLED
    Serial.printf("DBG:PROT_AZ:TOLERANCE:diff=%.1f within %d tolerance, not starting\n", diff, (int)PROTOCOL_STOP_TOLERANCE);
#endif
    protocolTargetAz = physTarget;
    return;
  }
  
  protocolTargetAz = physTarget;
  protocolTargetAzSet = true;
#if DEBUG_ENABLED
  Serial.print("DBG:PROT_AZ:RECEIVED:target="); Serial.print(targetAz);
  Serial.print(" curRaw="); Serial.print(currentAz);
  Serial.print(" curDisp="); Serial.println(displayAzimuth(currentAz));
#endif
#if DEBUG_ENABLED
  Serial.printf("DBG:PROT_AZ:DIFF:rawDiff=%.1f normDiff=%.1f\n", diff, diff);
#endif
  float physicalStop;
  switch (rotModel) {
    case ROT_N360: physicalStop = 180.0; break;
    case ROT_S360: physicalStop = 0.0; break;
    case ROT_N450: physicalStop = 135.0; break;
    case ROT_S450: physicalStop = 315.0; break;
    default: physicalStop = 180.0; break;
  }
#if DEBUG_ENABLED
  Serial.printf("DBG:PROT_AZ:PHYSSTOP:physicalStop=%.1f rotModel=%d\n", physicalStop, rotModel);
#endif
  bool mustRotateCW = false;
  bool mustRotateCCW = false;
  if (elfMode) {
    float satStop = elfAz(physicalStop);
    if (curForDiff >= satStop && tgtForDiff < satStop) {
      mustRotateCW = true;
    } else if (curForDiff < satStop && tgtForDiff >= satStop) {
      mustRotateCCW = true;
    }
  } else {
    if (curForDiff >= physicalStop && tgtForDiff < physicalStop) {
      mustRotateCW = true;
    } else if (curForDiff < physicalStop && tgtForDiff >= physicalStop) {
      mustRotateCCW = true;
    }
  }
  if (rotModel == ROT_N450 || rotModel == ROT_S450) {
    float centerHeading = (rotModel == ROT_N450) ? 0.0 : 180.0;
    float targetOffset = physTarget - centerHeading;
    if (targetOffset > 180.0) targetOffset -= 360.0;
    if (targetOffset < -180.0) targetOffset += 360.0;
    float cwDist = targetOffset - mechanicalAzOffset;
    float ccwDist = mechanicalAzOffset - targetOffset;
    float maxTravel = 225.0;
    bool cwValid = (mechanicalAzOffset + cwDist) <= maxTravel;
    bool ccwValid = (mechanicalAzOffset - ccwDist) >= -maxTravel;
#if DEBUG_ENABLED
    Serial.printf("DBG:PROT_AZ:N450S450:center=%.1f tgtOff=%.1f cwDist=%.1f ccwDist=%.1f cwVal=%d ccwVal=%d\n", 
      centerHeading, targetOffset, cwDist, ccwDist, cwValid, ccwValid);
#endif
    if (cwValid && ccwValid) {
      if (abs(cwDist) < abs(ccwDist)) mustRotateCW = true; else mustRotateCCW = true;
    } else if (cwValid) { mustRotateCW = true; } else if (ccwValid) { mustRotateCCW = true; }
  }
#if DEBUG_ENABLED
  Serial.printf("DBG:PROT_AZ:FLAGS:mustCW=%d mustCCW=%d diff=%.1f\n", mustRotateCW, mustRotateCCW, diff);
#endif
  if (mustRotateCW || (diff > 1.0 && !mustRotateCCW)) {
    // Need to move CW (RIGHT)
#if DEBUG_ENABLED
    Serial.printf("DBG:PROT_AZ:ACTION:NEED_CW:azDir=%d\n", azDirection);
#endif
    if (azDirection == DIR_CCW) {
      // Direction reversal — stop, delay, then resume
#if DEBUG_ENABLED
      Serial.println("DBG:PROT_AZ:ACTION:REVERSAL_CW");
#endif
      stopAzimuth(); protocolAzReversing = true; protocolAzReverseStart = millis(); protocolAzPendingDir = "RIGHT";
    } else if (azDirection != DIR_CW) {
      // Not currently moving — start CW movement
#if DEBUG_ENABLED
      Serial.println("DBG:PROT_AZ:ACTION:START_CW");
#endif
      executeAzimuthMove("RIGHT");
    } else {
#if DEBUG_ENABLED
      Serial.println("DBG:PROT_AZ:ACTION:CONTINUE_CW");
#endif
    }
    // If already DIR_CW: do nothing, movement continues toward new target
  } else if (mustRotateCCW || diff < -1.0) {
    // Need to move CCW (LEFT)
#if DEBUG_ENABLED
    Serial.printf("DBG:PROT_AZ:ACTION:NEED_CCW:azDir=%d\n", azDirection);
#endif
    if (azDirection == DIR_CW) {
      // Direction reversal — stop, delay, then resume
#if DEBUG_ENABLED
      Serial.println("DBG:PROT_AZ:ACTION:REVERSAL_CCW");
#endif
      stopAzimuth(); protocolAzReversing = true; protocolAzReverseStart = millis(); protocolAzPendingDir = "LEFT";
    } else if (azDirection != DIR_CCW) {
      // Not currently moving — start CCW movement
#if DEBUG_ENABLED
      Serial.println("DBG:PROT_AZ:ACTION:START_CCW");
#endif
      executeAzimuthMove("LEFT");
    } else {
#if DEBUG_ENABLED
      Serial.println("DBG:PROT_AZ:ACTION:CONTINUE_CCW");
#endif
    }
    // If already DIR_CCW: do nothing, movement continues toward new target
  } else {
#if DEBUG_ENABLED
    Serial.println("DBG:PROT_AZ:ACTION:STOPPED (at target)");
#endif
    stopAzimuth(); protocolTargetAzSet = false;
  }
}

void executeProtocolElMove(float targetEl) {
  if (targetEl < 0.0) targetEl = 0.0;
  if (targetEl > 180.0) targetEl = 180.0;
  
  // When ELF is on, incoming targets are in satellite space — transform to physical
  float physTarget = elfEl(targetEl);
  
  // Direction calculation: compare in the same coordinate space
  // When ELF is on, both in satellite space; when off, use raw physical coords
  float curForDiff = elfEl(currentEl);
  float diff = targetEl - curForDiff;
  
  // Don't start movement if target is within auto-stop tolerance and we're stopped
  // This prevents relay clicking when new targets arrive within the zone where
  // auto-stop would immediately kill movement anyway
  if (elDirection == DIR_STOPPED && !protocolTargetElSet && abs(diff) <= PROTOCOL_STOP_TOLERANCE) {
#if DEBUG_ENABLED
    Serial.printf("DBG:PROT_EL:TOLERANCE:diff=%.1f within %d tolerance, not starting\n", diff, (int)PROTOCOL_STOP_TOLERANCE);
#endif
    protocolTargetEl = physTarget;
    return;
  }
  
  protocolTargetEl = physTarget;
  protocolTargetElSet = true;
  
  // Convert satellite-space direction to physical direction
  // In ELF mode: satellite UP = physical DOWN (Real El = 180 - Sat El)
  String moveDir;
  if (diff > 1.0) {
    moveDir = elfMode ? "DOWN" : "UP";
  } else if (diff < -1.0) {
    moveDir = elfMode ? "UP" : "DOWN";
  } else {
    stopElevation(); protocolTargetElSet = false;
    return;
  }
  
  if (moveDir == "UP") {
    if (elDirection == DIR_DOWN) {
      stopElevation(); protocolElReversing = true; protocolElReverseStart = millis(); protocolElPendingDir = "UP";
    } else { executeElevationMove("UP"); }
  } else {
    if (elDirection == DIR_UP) {
      stopElevation(); protocolElReversing = true; protocolElReverseStart = millis(); protocolElPendingDir = "DOWN";
    } else { executeElevationMove("DOWN"); }
  }
}

// ==================== DEFAULT PROTOCOL HANDLER ====================
// Hamlib's universal protocol - used over TCP by clients like Gpredict/rotctl
// Command format: single char + optional args, separated by spaces
// Response format: "RPRT 0\n" for success, "value\n" for queries, "RPRT -N\n" for errors

// Send a Default Protocol response back over the SAME interface the command
// arrived on (TCP clients are answered over TCP; serial over serial).
void dpSend(const String& response) {
  String full = response + "\n";

  // Route the reply to the interface that delivered the command
  bool delivered = false;
  if (protocolRxSource == RX_SRC_SER) {
    Serial.print(full);
    delivered = true;
  } else if (rotctlConnected && rotctlClient.connected()) {
    rotctlClient.print(full);
    delivered = true;
  }

  // Debug log: record where the response went ("DROP" = no delivery target)
  dbgLogCapture("TX", delivered ? (protocolRxSource == RX_SRC_SER ? "SER" : "TCP") : "DROP", full.c_str());

  strncpy(protocolLastResponse, response.c_str(), PROTOCOL_BUF_SIZE - 1);
  protocolLastResponse[PROTOCOL_BUF_SIZE - 1] = '\0';
}

// Send RPRT response
void dpRprt(int code) {
  dpSend("RPRT " + String(code));
}

// Parse space-separated arguments from a command line
// Returns number of args found (max 4)
int dpParseArgs(char* line, char* args[4]) {
  int count = 0;
  char* p = strtok(line, " ");
  while (p && count < 4) {
    args[count++] = p;
    p = strtok(NULL, " ");
  }
  return count;
}

// Process TCP commands on port 4533
// When Hamlib Server = Y: speak rotctl text protocol (Gpredict)
// When Hamlib Server = N: speak configured serial protocol (GS-232A/B/Easycom)
void processDefaultProtocolCommand(const char* cmd, int len) {
  if (len == 0) return;

  // Route based on Hamlib Server setting
  if (!hamlibServerEnabled) {
    // Speak the configured serial protocol over TCP
    processProtocolCommand(cmd, len);
    return;
  }

  // Hamlib Server enabled — speak rotctl text protocol
  // Make a mutable copy for strtok
  char buf[PROTOCOL_BUF_SIZE];
  strncpy(buf, cmd, PROTOCOL_BUF_SIZE - 1);
  buf[PROTOCOL_BUF_SIZE - 1] = '\0';

  char* args[4];
  int argc = dpParseArgs(buf, args);
  if (argc == 0) return;

  // Replace commas with periods in all args (Gpredict locale handling)
  for (int i = 0; i < argc; i++) {
    char* p = args[i];
    while (*p) {
      if (*p == ',') *p = '.';
      p++;
    }
  }

  char cmdChar = args[0][0];
  
  switch (cmdChar) {
    case 'p': {
      // Get position → returns azimuth\nelevation\nRPRT 0\n
      float az = displayAzimuth(currentAz);
      float el = currentEl;
      
      // During motor engagement, report last known good position to avoid
      // stale ADC readings that show backward drift (mechanical backlash)
      unsigned long now = millis();
      if (azEngageStart > 0 && (now - azEngageStart < MOTOR_ENGAGE_DELAY_MS)) {
        az = azPositionAtEngage;
      }
      if (elEngageStart > 0 && (now - elEngageStart < MOTOR_ENGAGE_DELAY_MS)) {
        el = elPositionAtEngage;
      }
      
      // When ELF mode is on, report effective satellite coords
      az = elfAz(az);
      el = elfEl(el);
      
      // Convert to -180..+180 range for Gpredict compatibility
      // Gpredict's AzType=1 config expects negative azimuths
      if (az > 180.0) az -= 360.0;
      
      #if DEBUG_ENABLED
      Serial.print("DBG:DP_P:az="); Serial.print(az, 2);
      Serial.print(" el="); Serial.print(el, 2);
      Serial.print(" rawAz="); Serial.print(currentAz, 2);
      Serial.print(" rawEl="); Serial.println(currentEl, 2);
      #endif
      // Send all 3 lines in ONE TCP packet - Gpredict does a single recv()
      // and expects azimuth\nelevation\nRPRT N\n all at once
      dpSend(String(az, 2) + "\n" + String(el, 2) + "\nRPRT 0");
      break;
    }
    
    case 'P': {
      // Set position: P azimuth elevation
      if (argc >= 3) {
        float targetAz = atof(args[1]);
        float targetEl = atof(args[2]);
        
        // Clamp values
        if (targetAz < 0) targetAz += 360;
        if (targetAz >= 360) targetAz -= 360;
        if (targetEl < 0) targetEl = 0;
        if (targetEl > 180) targetEl = 180;
        
        // Calculate direction in satellite space BEFORE transforming to physical
        // Transform current physical position to satellite space for comparison
        float satCurAz = elfAz(currentAz);
        float satCurEl = elfEl(currentEl);
        float diffAz = targetAz - satCurAz;
        if (diffAz > 180) diffAz -= 360;
        if (diffAz < -180) diffAz += 360;
        
        String wantAzDir = "STOP";
        if (abs(diffAz) > 1.0) {
          wantAzDir = (diffAz > 0) ? "RIGHT" : "LEFT";
        }
        
        float diffEl = targetEl - satCurEl;
        String wantElDir = "STOP";
        if (abs(diffEl) > 1.0) {
          wantElDir = (diffEl > 0) ? "UP" : "DOWN";
        }
        
        // Now transform satellite coords to physical rotator coords for actual movement
        float physTargetAz = elfAz(targetAz);
        float physTargetEl = elfEl(targetEl);
        
        // Always update protocol targets for auto-stop (physical coords)
        protocolTargetAz = physTargetAz;
        protocolTargetAzSet = true;
        protocolTargetEl = physTargetEl;
        protocolTargetElSet = true;
        
        // Only change motors if direction actually changed — don't restart same-direction movement
        if (wantAzDir == "STOP") {
          stopAzimuth();
        } else if (azDirection == DIR_STOPPED ||
                   (wantAzDir == "RIGHT" && azDirection != DIR_CW) ||
                   (wantAzDir == "LEFT" && azDirection != DIR_CCW)) {
          executeAzimuthMove(wantAzDir);
        }
        // else: already moving CW/CCW, same direction — just keep going
        
        if (wantElDir == "STOP") {
          stopElevation();
        } else if (elDirection == DIR_STOPPED ||
                   (wantElDir == "UP" && elDirection != DIR_UP) ||
                   (wantElDir == "DOWN" && elDirection != DIR_DOWN)) {
          executeElevationMove(wantElDir);
        }
        // else: already moving UP/DOWN, same direction — just keep going
      }
      dpRprt(0);
      break;
    }
    
    case 'S': {
      // Stop rotation
      stopAll();
      dpRprt(0);
      break;
    }
    
    case 's': {
      // Get status: returns status flags as integer + RPRT 0
      int status = 0;
      if (azDirection != DIR_STOPPED || elDirection != DIR_STOPPED) {
        status |= 4;  // Moving
      }
      dpSend(String(status));
      dpRprt(0);
      break;
    }
    
    case 'M': {
      // Move: M direction [speed]
      // Direction: LEFT, RIGHT, UP, DOWN, or numeric
      if (argc >= 2) {
        String dir = args[1];
        dir.toUpperCase();
        if (dir == "LEFT" || dir == "CCW") executeAzimuthMove("LEFT");
        else if (dir == "RIGHT" || dir == "CW") executeAzimuthMove("RIGHT");
        else if (dir == "UP") executeElevationMove("UP");
        else if (dir == "DOWN") executeElevationMove("DOWN");
        else {
          // Numeric direction
          int d = atoi(args[1]);
          if (d == 1 || d == -1) executeAzimuthMove(d > 0 ? "RIGHT" : "LEFT");
          else if (d == 2 || d == -2) executeElevationMove(d > 0 ? "UP" : "DOWN");
        }
      }
      dpRprt(0);
      break;
    }
    
    case '_': {
      // Get info/version
      dpSend("AZELCtrl v" + String(FIRMWARE_VERSION));
      dpRprt(0);
      break;
    }
    
    case 'K': {
      // Park (not implemented, treat as stop)
      stopAll();
      dpRprt(0);
      break;
    }
    
    case 'R': {
      // Reset: R reset_type
      // reset_type: 0 = soft, 1 = hard, 2 = park
      stopAll();
      dpRprt(0);
      break;
    }
    
    case 'v': {
      // Get level: v level_name
      // ROT_LEVEL_ROT_SPEED = rotation speed (not implemented, return default)
      if (argc >= 2 && strcmp(args[1], "ROT_LEVEL_ROT_SPEED") == 0) {
        dpSend("1");  // Default speed
      } else {
        dpRprt(-2);  // EINVAL
        return;
      }
      dpRprt(0);
      break;
    }
    
    case 'V': {
      // Set level: V level_name value
      if (argc >= 3 && strcmp(args[1], "ROT_LEVEL_ROT_SPEED") == 0) {
        // Speed setting not yet implemented, accept silently
        dpRprt(0);
      } else {
        dpRprt(-2);  // EINVAL
      }
      break;
    }
    
    case 'q': {
      // Quit connection
      dpRprt(0);
      break;
    }
    
    default: {
      dpRprt(-2);  // EINVAL - unknown command
      break;
    }
  }
}

// Process GS-232A/B commands
void processGs232Command(const char* cmd, int len) {
  char upper = toupper(cmd[0]);
  
  switch (upper) {
    case 'H': {
      if (len == 1) {
        // H = query heading — protocol-specific format
        float az = displayAzimuth(currentAz);
        // When ELF mode is on, report effective satellite coords
        az = elfAz(az);
        int azDeg = (int)round(az);
        if (azDeg < 0) azDeg += 360;
        if (azDeg >= 360) azDeg -= 360;
        char buf[32];
        switch (currentProtocol) {
          case PROTO_GS232A:
          case PROTO_GS232B:
            snprintf(buf, sizeof(buf), "H%03d", azDeg);
            break;
          case PROTO_EASYCOM:
            snprintf(buf, sizeof(buf), "AZ%.1f", az);
            break;
        }
        protocolSend(String(buf));
      } else {
        // Hnnn = set heading (same as Mnnn)
        int targetAz = 0;
        int consumed = 0;
        if (parseNumber(cmd, 1, &targetAz, &consumed) == 0) {
          if (targetAz >= 0 && targetAz <= 360) {
            executeProtocolAzMove(targetAz);
            protocolSend("OK");
          } else {
            protocolSend("ERR");
          }
        } else {
          protocolSend("ERR");
        }
      }
      break;
    }
    case 'M': {
      // Mnnn = move azimuth to heading
      if (len > 1) {
        int targetAz = 0;
        int consumed = 0;
        if (parseNumber(cmd, 1, &targetAz, &consumed) == 0) {
          if (targetAz >= 0 && targetAz <= 360) {
            executeProtocolAzMove(targetAz);
            protocolSend("OK");
          } else {
            protocolSend("ERR");
          }
        } else {
          protocolSend("ERR");
        }
      } else {
        protocolSend("ERR");
      }
      break;
    }
    case 'W': {
      // Wnnn nnn = move to azimuth/elevation position
      if (len > 1) {
        int targetAz = 0, targetEl = 0;
        int consumed = 0;
        // Parse azimuth (3 digits)
        if (parseNumber(cmd, 1, &targetAz, &consumed) == 0) {
          // Skip space separator
          int elOffset = 1 + consumed;
          while (elOffset < len && cmd[elOffset] == ' ') elOffset++;
          // Parse elevation
          int elConsumed = 0;
          if (parseNumber(cmd, elOffset, &targetEl, &elConsumed) == 0) {
            if (targetAz >= 0 && targetAz <= 360 && targetEl >= 0 && targetEl <= 180) {
              executeProtocolAzMove(targetAz);
              executeProtocolElMove(targetEl);
              protocolSend("OK");
            } else {
              protocolSend("ERR");
            }
          } else {
            protocolSend("ERR");
          }
        } else {
          protocolSend("ERR");
        }
      } else {
        protocolSend("ERR");
      }
      break;
    }
    case 'V': {
      if (len == 1) {
        // V = query elevation
        float el = currentEl;
        int elDeg = (int)round(el);
        protocolSend(String("V") + String(elDeg, DEC));
      } else {
        // Vnnn = set elevation
        int targetEl = 0;
        int consumed = 0;
        if (parseNumber(cmd, 1, &targetEl, &consumed) == 0) {
          if (targetEl >= 0 && targetEl <= 180) {
            executeProtocolElMove((float)targetEl);
            protocolSend("OK");
          } else {
            protocolSend("ERR");
          }
        } else {
          protocolSend("ERR");
        }
      }
      break;
    }
    case 'C': {
      // GS-232B C command family:
      // C  = Current position (azimuth, elevation)
      // C1 = Current position with rate of change
      // C2 = Current position with status
      // C3 = Current position with full status
      float az = displayAzimuth(currentAz);
      float el = currentEl;
      // When ELF mode is on, report effective satellite coords
      az = elfAz(az);
      el = elfEl(el);
      int azDeg = (int)round(az);
      int elDeg = (int)round(el);
      if (azDeg < 0) azDeg += 360;
      if (azDeg >= 360) azDeg -= 360;
      char buf[64];
      int pos = 0;
      if (len > 1) {
        char suffix = toupper(cmd[1]);
        if (suffix == '1') {
          // C1: position with rate of change
          formatPositionResponse(buf, sizeof(buf), azDeg, elDeg, "RATE=00.0");
        } else if (suffix == '2') {
          // C2: current position with status
          formatPositionResponse(buf, sizeof(buf), azDeg, elDeg, "");
        } else if (suffix == '3') {
          // C3: position with full status
          formatPositionResponse(buf, sizeof(buf), azDeg, elDeg, "STATUS=0");
        }
      } else {
        // Bare C: Caaa.ee
        pos += snprintf(buf + pos, sizeof(buf) - pos, "C%03d.%02d", azDeg, elDeg);
      }
      protocolSend(String(buf));
      break;
    }
    case 'L': {
      startMove("LEFT");
      protocolSend("OK");
      break;
    }
    case 'R': {
      startMove("RIGHT");
      protocolSend("OK");
      break;
    }
    case 'U': {
      // In ELF mode: satellite UP = physical DOWN (Real El = 180 - Sat El)
      elfMode ? startMove("DOWN") : startMove("UP");
      protocolSend("OK");
      break;
    }
    case 'D': {
      // In ELF mode: satellite DOWN = physical UP
      elfMode ? startMove("UP") : startMove("DOWN");
      protocolSend("OK");
      break;
    }
    case 'A':  // All Stop (both axes)
    case 'S': {
      stopAll();
      protocolSend("OK");
      break;
    }
    case 'E':  // Stop elevation only
      stopElevation();
      protocolSend("OK");
      break;
    case 'Z':  // Stop azimuth only
      stopAzimuth();
      protocolSend("OK");
      break;
    case 'X': {
      // Xn = Set speed (1-4, where 1=slowest, 4=fastest)
      if (len > 1) {
        int speed = 0;
        int consumed = 0;
        if (parseNumber(cmd, 1, &speed, &consumed) == 0) {
          if (speed >= 1 && speed <= 4) {
            // Store speed for future use (not currently used for motor control)
            // protocolSpeed = speed;
            protocolSend("OK");
          } else {
            protocolSend("ERR");
          }
        } else {
          protocolSend("ERR");
        }
      } else {
        protocolSend("ERR");
      }
      break;
    }
    default:
      protocolSend("ERR");
      break;
  }
}

// Process Easycom commands
void processEasycomCommand(const char* cmd, int len) {
  while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\t')) len--;
  
  // Parse first token (command identifier)
  int tokenLen = 0;
  while (tokenLen < len && cmd[tokenLen] != ' ' && cmd[tokenLen] != '\t') tokenLen++;
  
  // Convert first two chars of token to uppercase for matching
  char token[4];
  if (tokenLen >= 2) {
    token[0] = toupper(cmd[0]);
    token[1] = toupper(cmd[1]);
    token[2] = '\0';
  } else if (tokenLen == 1) {
    token[0] = toupper(cmd[0]);
    token[1] = '\0';
    token[2] = '\0';
  } else {
    protocolSend("ERR");
    return;
  }
  
  // AZ — query or set azimuth
  if (strcmp(token, "AZ") == 0) {
    if (len <= tokenLen) {
      // Bare AZ query — hamlib sends "AZ\n", expects "AZxxx.x"
      float az = displayAzimuth(currentAz);
      char buf[32];
      snprintf(buf, sizeof(buf), "AZ%.1f", az);
      protocolSend(String(buf));
    } else {
      // AZ xxx.x — set azimuth (skip spaces after token)
      const char* val = cmd + tokenLen;
      while (*val == ' ' || *val == '\t') val++;
      float targetAz;
      if (!parseFloat(val, &targetAz)) {
        protocolSend("ERR");
        showOledWarning("ERR: BAD AZ VALUE");
        return;
      }
      if (targetAz >= 0.0 && targetAz <= 360.0) {
        executeProtocolAzMove(targetAz);
        protocolSend("OK");
      } else {
        protocolSend("ERR");
      }
    }
  }
  // EL — query or set elevation
  else if (strcmp(token, "EL") == 0) {
    if (len <= tokenLen) {
      // Bare EL query — hamlib sends "EL\n", expects "ELyy.y"
      char buf[32];
      snprintf(buf, sizeof(buf), "EL%.1f", currentEl);
      protocolSend(String(buf));
    } else {
      // EL yy.y — set elevation (skip spaces after token)
      const char* val = cmd + tokenLen;
      while (*val == ' ' || *val == '\t') val++;
      float targetEl;
      if (!parseFloat(val, &targetEl)) {
        protocolSend("ERR");
        showOledWarning("ERR: BAD EL VALUE");
        return;
      }
      if (targetEl >= 0.0 && targetEl <= 180.0) {
        executeProtocolElMove(targetEl);
        protocolSend("OK");
      } else {
        protocolSend("ERR");
      }
    }
  }
  // ML — move left (azimuth decrease / CCW)
  else if (strcmp(token, "ML") == 0) {
    startMove("LEFT");
    protocolSend("OK");
  }
  // MR — move right (azimuth increase / CW)
  else if (strcmp(token, "MR") == 0) {
    startMove("RIGHT");
    protocolSend("OK");
  }
  // MU — move up (elevation increase)
  else if (strcmp(token, "MU") == 0) {
    startMove("UP");
    protocolSend("OK");
  }
  // MD — move down (elevation decrease)
  else if (strcmp(token, "MD") == 0) {
    startMove("DOWN");
    protocolSend("OK");
  }
  // SA — stop azimuth
  else if (strcmp(token, "SA") == 0) {
    stopAzimuth();
    protocolSend("OK");
  }
  // SE — stop elevation
  else if (strcmp(token, "SE") == 0) {
    stopElevation();
    protocolSend("OK");
  }
  // S — all stop (also accepted by hamlib)
  else if (strcmp(token, "S") == 0 || tokenLen == 1 && token[0] == 'S') {
    stopAll();
    protocolSend("OK");
  }
  // VE — version query
  else if (strcmp(token, "VE") == 0) {
    protocolSend("VEYCINT/1.0");
  }
  // GS — get status
  else if (strcmp(token, "GS") == 0) {
    // Status: 1=Idle, 2=Moving, 4=Pointing, 8=Error
    int status = moving ? 2 : 1;
    char buf[16];
    snprintf(buf, sizeof(buf), "GS%d", status);
    protocolSend(String(buf));
  }
  // VL/VR/VU/VD — velocity commands (EasyComm 3)
  else if (token[0] == 'V' && (token[1] == 'L' || token[1] == 'R' || token[1] == 'U' || token[1] == 'D')) {
    // Velocity move — start movement in direction
    if (token[1] == 'L') startMove("LEFT");
    else if (token[1] == 'R') startMove("RIGHT");
    else if (token[1] == 'U') startMove("UP");
    else if (token[1] == 'D') startMove("DOWN");
    protocolSend("OK");
  }
  // AO / LO — AOS / LOS (acknowledge, no action needed)
  else if (strcmp(token, "AO") == 0 || strcmp(token, "LO") == 0) {
    protocolSend("OK");
  }
  // UP / DN / DM / UM / DR / UR — radio control (not implemented, acknowledge)
  else if (strcmp(token, "UP") == 0 || strcmp(token, "DN") == 0 ||
           strcmp(token, "DM") == 0 || strcmp(token, "UM") == 0 ||
           strcmp(token, "DR") == 0 || strcmp(token, "UR") == 0 ||
           strcmp(token, "OP") == 0 || strcmp(token, "IP") == 0 ||
           strcmp(token, "AN") == 0 || strcmp(token, "ST") == 0) {
    protocolSend("OK");
  }
  // Backslash query (rotctl identification)
  else if (len >= 2 && cmd[0] == '\\' && cmd[1] == '?') {
    protocolSend("\\?EASYCOM,ROTATOR");
  }
  else {
    protocolSend("ERR");
  }
}

// Helper: parse a float from string. Returns true on success.
// Rejects empty/garbage input (strtof would silently return 0 otherwise).
static bool parseFloat(const char* s, float* out) {
  while (*s == ' ' || *s == '\t') s++;
  if (*s == '\0') return false;
  errno = 0;
  char* end;
  float val = strtof(s, &end);
  if (end == s) return false;        // no digits consumed
  if (errno == ERANGE) return false; // overflow / underflow
  *out = val;
  return true;
}

// Main protocol input handler — read from serial and feed to parser
void processProtocolInput() {
  while (Serial.available()) {
    char ch = Serial.read();
    processProtocolChar(ch);
    ESP.wdtFeed();  // Keep draining a fast command stream without tripping the watchdog
  }
}

// Check for MODE:ROTATOR command in serial buffer (called from blocking loops)
bool checkModeCommand() {
  while (Serial.available()) {
    char ch = Serial.read();
    static char modeBuf[32];
    static int modeIdx = 0;
    
    if (ch == '\n' || ch == '\r') {
      modeBuf[modeIdx] = '\0';
      if (modeIdx > 0) dbgLogCapture("RX", "SER", modeBuf);
      // Exact-length match only: a prefix match alone would trigger on
      // any longer line merely starting with "MODE:ROTATOR".
      if (modeIdx == 12 && strncmp(modeBuf, "MODE:ROTATOR", 12) == 0) {
        stopAll();
        exitCalibrationMode();
        currentMenu = nullptr;
        // Serial.println("MODE:ROTATOR");
        modeIdx = 0;
        return true;
      }
      modeIdx = 0;
    } else if (modeIdx < 31) {
      modeBuf[modeIdx++] = ch;
    }
    ESP.wdtFeed();  // Keep draining a fast stream without tripping the watchdog
  }
  return false;
}



void setCalDirection(int dir) {
  calDirection = dir;
  if (calCurrentAxis == 0) {
    // AZ: CW = RIGHT, CCW = LEFT
    if (dir == 1) {
      executeAzimuthMove("RIGHT");
    } else if (dir == -1) {
      executeAzimuthMove("LEFT");
    }
  } else if (calCurrentAxis == 1) {
    // EL: UP = UP, DOWN = DOWN
    if (dir == 1) {
      executeElevationMove("UP");
    } else if (dir == -1) {
      executeElevationMove("DOWN");
    }
  }
}

// Check if voltage has stabilized (change < threshold over recent samples)
bool isVoltageStable(float currentV) {
  if (calVSampleIdx < CAL_V_SAMPLES) return false;
  
  float minV = currentV, maxV = currentV;
  for (int i = 0; i < CAL_V_SAMPLES; i++) {
    if (calVoltageSamples[i] < minV) minV = calVoltageSamples[i];
    if (calVoltageSamples[i] > maxV) maxV = calVoltageSamples[i];
  }
  // Include current reading
  if (currentV < minV) minV = currentV;
  if (currentV > maxV) maxV = currentV;
  
  float range = (maxV - minV) * 1000;  // Convert to mV
  return range < CAL_STABILITY_THRESHOLD;
}

void updateCalibrationState() {
  if (calState == CAL_IDLE) return;
  
  float v = (calCurrentAxis == 0) ? currentAz : currentEl;
  float vRaw = (calCurrentAxis == 0) ? 
    ((float)ads.readADC_SingleEnded(0) / 32767.0 * 4.096F) :
    ((float)ads.readADC_SingleEnded(1) / 32767.0 * 4.096F);
  
  // Add to voltage samples for stability detection
  if (calVSampleIdx < CAL_V_SAMPLES) {
    calVoltageSamples[calVSampleIdx++] = vRaw;
  } else {
    // Shift samples
    for (int i = 0; i < CAL_V_SAMPLES - 1; i++) {
      calVoltageSamples[i] = calVoltageSamples[i + 1];
    }
    calVoltageSamples[CAL_V_SAMPLES - 1] = vRaw;
  }
  
  switch (calState) {
    case CAL_SWEEP_MIN: {
      // Move CCW/DOWN to find minimum voltage.
      // First call: capture initial voltage, start moving CCW.
      // If initial voltage < 30mV and voltage doesn't change → already at limit.
      // Otherwise keep going until voltage stabilizes.
      if (calVSampleIdx <= 1) {
        // First iteration: capture initial voltage, start moving CCW/DOWN
        calInitialVoltage = vRaw;
        calStabilityMax = vRaw;
        calStabilityMin = vRaw;
        setCalDirection(-1);
        Serial.print("CAL: Starting sweep to minimum (initial: ");
        Serial.print(vRaw, 3);
        Serial.println("V)");
      }
      
      // Track voltage range
      if (vRaw > calStabilityMax) calStabilityMax = vRaw;
      if (vRaw < calStabilityMin) calStabilityMin = vRaw;
      
      // Check if voltage stabilized (reached hard limit)
      if (isVoltageStable(vRaw)) {
        calMinVoltage = vRaw;
        stopAll();
        calDirection = 0;
        Serial.print("CAL: Min voltage found (stable): ");
        Serial.println(calMinVoltage, 3);
        
        // Proceed to sweep max
        calState = CAL_SWEEP_MAX;
        calPhaseStart = millis();
        calVSampleIdx = 0;
        memset(calVoltageSamples, 0, sizeof(calVoltageSamples));
        setCalDirection(1);
        Serial.println("CAL: Starting sweep to maximum");
      }
      // If initial voltage was already very low (<30mV) and voltage hasn't
      // changed significantly, we were already at the CCW limit
      else if (calInitialVoltage < 0.03F &&
               (calStabilityMax - calStabilityMin) * 1000 < 50) {
        // Voltage was already near zero and hasn't changed → already at limit
        calMinVoltage = vRaw;
        stopAll();
        calDirection = 0;
        Serial.print("CAL: Already at minimum (initial: ");
        Serial.print(calInitialVoltage, 3);
        Serial.println("V)");
        
        // Proceed to sweep max
        calState = CAL_SWEEP_MAX;
        calPhaseStart = millis();
        calVSampleIdx = 0;
        memset(calVoltageSamples, 0, sizeof(calVoltageSamples));
        setCalDirection(1);
        Serial.println("CAL: Starting sweep to maximum");
      }
      else if (millis() - calPhaseStart >= CAL_SWEEP_TIMEOUT) {
        // Timeout - use current reading as best guess
        calMinVoltage = vRaw;
        stopAll();
        calDirection = 0;
        Serial.print("CAL: Timeout sweeping min, using: ");
        Serial.println(calMinVoltage, 3);
        
        // Proceed to sweep max
        calState = CAL_SWEEP_MAX;
        calPhaseStart = millis();
        calVSampleIdx = 0;
        memset(calVoltageSamples, 0, sizeof(calVoltageSamples));
        setCalDirection(1);
      }
      break;
    }
      
    case CAL_SWEEP_MAX: {
      // Move CW/UP to find maximum voltage.
      // After 3 seconds, check if voltage increased → cable detection.
      // Continue until voltage stabilizes or hits safety limit.
      if (calVSampleIdx <= 1) {
        calStabilityMax = vRaw;
        calStabilityMin = vRaw;
      }
      
      // Track voltage range
      if (vRaw > calStabilityMax) calStabilityMax = vRaw;
      if (vRaw < calStabilityMin) calStabilityMin = vRaw;
      
      // After 3 seconds, check if voltage increased (cable detection)
      if (millis() - calPhaseStart >= 3000 && calVSampleIdx > 5) {
        float increase = (calStabilityMax - calMinVoltage) * 1000;  // mV increase from min
        Serial.print("CAL: Voltage increase after 3s: ");
        Serial.print(increase);
        Serial.println(" mV");
        
        if (increase < CAL_DETECT_MIN_CHANGE) {
          // Voltage didn't increase → cable unplugged or not connected
          calState = CAL_ERROR;
          char buf[30];
          snprintf(buf, sizeof(buf), "dV (%.0fmV) too small", increase);
          calErrorMessage = String(buf);
          stopAll();
          calDirection = 0;
          Serial.println("CAL: ERROR - No voltage increase, check cable");
          break;
        }
      }
      
      // Safety limit
      if (vRaw >= CAL_MAX_SAFE_V) {
        calMaxVoltage = vRaw;
        stopAll();
        calDirection = 0;
        Serial.print("CAL: Max voltage hit safety limit: ");
        Serial.println(calMaxVoltage, 3);
        
        // Prompt user to adjust to exact heading
        calState = CAL_ADJUST_MAX;
        Serial.println("CAL: Waiting for user to adjust to exact heading");
      }
      // Voltage stabilized (reached hard limit)
      // Only check stability if voltage has increased significantly from min
      // to avoid false detection while rotator is just starting to move
      else if ((calStabilityMax - calMinVoltage) * 1000 > 100 && isVoltageStable(vRaw)) {
        calMaxVoltage = vRaw;
        stopAll();
        calDirection = 0;
        Serial.print("CAL: Max voltage found (stable): ");
        Serial.println(calMaxVoltage, 3);
        
        // Prompt user to adjust to exact heading
        calState = CAL_ADJUST_MAX;
        Serial.println("CAL: Waiting for user to adjust to exact heading");
      }
      else if (millis() - calPhaseStart >= CAL_SWEEP_TIMEOUT) {
        // Timeout - use current reading
        calMaxVoltage = vRaw;
        stopAll();
        calDirection = 0;
        Serial.print("CAL: Timeout sweeping max, using: ");
        Serial.println(calMaxVoltage, 3);
        
        // Prompt user to adjust to exact heading
        calState = CAL_ADJUST_MAX;
        Serial.println("CAL: Waiting for user to adjust to exact heading");
      }
      break;
    }
      
    case CAL_ADJUST_MAX: {
      // Prompt user to manually adjust to exact heading (180° AZ / 90° EL)
      // Btn1 = Continue, Btn2 = Back/Cancel
      float vRawNow = (calCurrentAxis == 0) ?
        ((float)ads.readADC_SingleEnded(0) / 32767.0 * 4.096F) :
        ((float)ads.readADC_SingleEnded(1) / 32767.0 * 4.096F);
      calMaxVoltage = vRawNow;  // Update max voltage as user adjusts
      break;
    }
      
    case CAL_RETURN_MIN: {
      // Return sweep: move back to minimum, pausing at interval headings
      float estHeading;
      
      if (calCurrentAxis == 0) {
        // AZ: use cardinal headings (S, SW, W, NW, N, NE, E, SE, S)
        estHeading = calEstimatedHeading(vRaw, 0);
        
        if (calCurrentHeading.length() > 0 && millis() - calHeadingPauseStart < CAL_HEADING_PAUSE) {
          break;
        }
        
        if (calCurrentHeading.length() > 0) {
          calCurrentHeading = "";
          calVSampleIdx = 0;
          setCalDirection(-1);
        }
        
        if (isVoltageStable(vRaw)) {
          stopAll();
          calDirection = 0;
          calState = CAL_COMPLETE;
          Serial.println("CAL: Return sweep complete — at minimum");
          break;
        }
        
        if (millis() - calPhaseStart >= CAL_SWEEP_TIMEOUT) {
          stopAll();
          calDirection = 0;
          calState = CAL_COMPLETE;
          Serial.println("CAL: Return sweep timeout — proceeding to complete");
          break;
        }
        
        int nextHeading = calNextCardinalHeading(estHeading);
        if (nextHeading >= 0) {
          calCurrentHeading = String(calCardinalNames[nextHeading]);
          calHeadingPauseStart = millis();
          calLastHeadingIndex = nextHeading;
          stopAll();
          calDirection = 0;
          Serial.print("CAL: Pausing at ");
          Serial.println(calCurrentHeading);
        } else if (calDirection != -1) {
          setCalDirection(-1);
        }
      } else {
        // EL: use elevation interval headings (180, 150, 120, 90, 60, 30, 0)
        estHeading = calEstimatedHeading(vRaw, 1);
        
        if (calCurrentHeading.length() > 0 && millis() - calHeadingPauseStart < CAL_HEADING_PAUSE) {
          break;
        }
        
        if (calCurrentHeading.length() > 0) {
          calCurrentHeading = "";
          calVSampleIdx = 0;
          setCalDirection(-1);
        }
        
        if (isVoltageStable(vRaw)) {
          stopAll();
          calDirection = 0;
          calState = CAL_COMPLETE;
          Serial.println("CAL: EL return sweep complete — at minimum");
          break;
        }
        
        if (millis() - calPhaseStart >= CAL_SWEEP_TIMEOUT) {
          stopAll();
          calDirection = 0;
          calState = CAL_COMPLETE;
          Serial.println("CAL: EL return sweep timeout — proceeding to complete");
          break;
        }
        
        // Check elevation intervals: 135, 90, 45, 0
        const float elIntervals[] = {135, 90, 45, 0};
        const char* elIntervalNames[] = {"135", "90", "45", "0"};
        const int elIntervalCount = 4;
        
        for (int i = calLastHeadingIndex + 1; i < elIntervalCount; i++) {
          float tolerance = (i == elIntervalCount - 1) ? 0.0F : 1.0F;
          if (estHeading <= elIntervals[i] + tolerance) {
            calCurrentHeading = String(elIntervalNames[i]);
            calHeadingPauseStart = millis();
            calLastHeadingIndex = i;
            stopAll();
            calDirection = 0;
            Serial.print("CAL: EL pausing at ");
            Serial.println(calCurrentHeading);
            break;
          }
        }
        
        if (calCurrentHeading.length() == 0 && calDirection != -1) {
          setCalDirection(-1);
        }
      }
      break;
    }
      
    case CAL_COMPLETE:
      // Wait for user input
      break;
      
    case CAL_ERROR:
      // Wait for user input
      break;
  }
}

void displayCalibrationScreen() {
  float vRaw = (calCurrentAxis == 0) ? 
    ((float)ads.readADC_SingleEnded(0) / 32767.0 * 4.096F) :
    ((float)ads.readADC_SingleEnded(1) / 32767.0 * 4.096F);
  
  display.clearDisplay();
  display.setTextSize(1);
  
  // Title bar
  display.fillRect(0, 0, 128, 8, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
  display.setCursor(0, 0);
  display.print("Cal-");
  display.print(calCurrentAxis == 0 ? "AZ:" : "EL:");
  
  // Status text right after prefix
  display.print(" ");
  switch (calState) {
    case CAL_SWEEP_MIN:
      if (calCurrentAxis == 0) display.print("Sweep CCW");
      else display.print("Sweep DOWN");
      break;
    case CAL_SWEEP_MAX:
      if (calCurrentAxis == 0) display.print("Sweep CW");
      else display.print("Sweep UP");
      break;
    case CAL_RETURN_MIN:
      if (calCurrentAxis == 0) display.print("Return CCW");
      else display.print("Return DOWN");
      break;
    case CAL_ADJUST_MAX: display.print("Adjust Max"); break;
    case CAL_COMPLETE: display.print("CALIBRATED"); break;
    case CAL_ERROR: display.print("ERROR!"); break;
    default: break;
  }
  
  // Solid white line below title
  display.drawLine(0, 8, 127, 8, SSD1306_WHITE);
  
  // Content area
  int y = 10;
  display.setTextColor(SSD1306_WHITE);
  
  switch (calState) {
    case CAL_SWEEP_MIN:
      display.setCursor(2, y);
      display.print("Moving ");
      if (calCurrentAxis == 0) display.print("CCW");
      else display.print("DOWN");
      display.println("...");
      break;
      
    case CAL_SWEEP_MAX:
      display.setCursor(2, y);
      display.print("Min: ");
      display.print(calMinVoltage, 3);
      display.println("V");
      break;
      
    case CAL_RETURN_MIN: {
      display.setCursor(2, y);
      display.print("Min: ");
      display.print(calMinVoltage, 3);
      display.println("V");
      y += 11;
      display.setCursor(2, y);
      if (calCurrentHeading.length() > 0) {
        // Paused — show countdown
        int remaining = max(0, (int)((CAL_HEADING_PAUSE - (millis() - calHeadingPauseStart)) / 1000));
        display.print("Stopped at: ");
        display.print(calCurrentHeading);
        display.print(" [");
        display.print(remaining);
        display.println("]");
      } else {
        // Moving — show next heading to pause at
        if (calCurrentAxis == 0) {
          // AZ: cardinal headings
          if (calLastHeadingIndex + 1 < calCardinalCount) {
            display.print("Next: ");
            display.println(calCardinalNames[calLastHeadingIndex + 1]);
          } else {
            display.println("Next: ---");
          }
        } else {
          // EL: elevation intervals
          const char* elIntervalNames[] = {"135", "90", "45", "0"};
          const int elIntervalCount = 4;
          if (calLastHeadingIndex + 1 < elIntervalCount) {
            display.print("Next: ");
            display.println(elIntervalNames[calLastHeadingIndex + 1]);
          } else {
            display.println("Next: ---");
          }
        }
      }
      break;
    }
      
    case CAL_ADJUST_MAX: {
      display.setCursor(2, y);
      if (calCurrentAxis == 0) {
        display.println("Adjust Az to");
        y += 11;
        display.setCursor(2, y);
        display.println("exactly 180 deg");
      } else {
        display.setCursor(2, y);
        display.println("Adjust El to");
        y += 11;
        display.setCursor(2, y);
        display.println("exactly 180 deg");
      }
      break;
    }
      
    case CAL_COMPLETE:
      y = 10;
      display.setCursor(2, y);
      display.print("Min:");
      display.print((int)(calMinVoltage * 1000));
      display.print("mV  Max:");
      display.print((int)(calMaxVoltage * 1000));
      display.println("mV");
      break;
      
    case CAL_ERROR:
      display.setCursor(2, y);
      display.println(calErrorMessage);
      y += 10;
      display.setCursor(2, y);
      display.println("Check cables & pwr");
      break;
  }
  
  // Dotted separator above readings
  for (int x = 0; x < 128; x += 3) {
    display.drawPixel(x, 29, SSD1306_WHITE);
  }
  
  // Show AZ and EL readings with voltages (aligned decimal points)
  // AZ: " AZ: -180.0 ( 123mV)" — value padded to 6 chars, voltage padded to 4
  // EL: " EL:   90.0 (3200mV)"
  display.setCursor(0, 32);
  display.print(" AZ:");
  {
    // During AZ calibration, use estimated heading based on rotator model
    float azVal = (calCurrentAxis == 0 && calState != CAL_IDLE && calState != CAL_COMPLETE && calState != CAL_ERROR)
      ? displayAzimuth(calEstimatedHeading(azVoltage, 0)) : displayAzimuth(currentAz);
    String azStr = String(azVal, 1);
    while (azStr.length() < 6) azStr = " " + azStr;
    display.print(azStr);
  }
  display.print(" (");
  {
    int azMv = (int)(azVoltage * 1000);
    String mvStr = String(azMv);
    while (mvStr.length() < 4) mvStr = " " + mvStr;
    display.print(mvStr);
  }
  display.println("mV)");
  display.setCursor(0, 43);
  display.print(" EL:");
  {
    // During EL calibration, use estimated heading
    float elVal = (calCurrentAxis == 1 && calState != CAL_IDLE && calState != CAL_COMPLETE && calState != CAL_ERROR)
      ? calEstimatedHeading(elVoltage, 1) : currentEl;
    String elStr = String(elVal, 1);
    while (elStr.length() < 6) elStr = " " + elStr;
    display.print(elStr);
  }
  display.print(" (");
  {
    int elMv = (int)(elVoltage * 1000);
    String mvStr = String(elMv);
    while (mvStr.length() < 4) mvStr = " " + mvStr;
    display.print(mvStr);
  }
  display.println("mV)");
  
  // Dotted separator above footer
  for (int x = 0; x < 128; x += 3) {
    display.drawPixel(x, 53, SSD1306_WHITE);
  }
  
  // Footer - context-sensitive
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 56);
  if (calState == CAL_COMPLETE) display.println("1.Done 2.Back");
  else if (calState == CAL_ERROR) display.println("1.Retry 2.Back");
  else if (calState == CAL_ADJUST_MAX) display.println("1.Continue 2.Cancel");
  else display.println("1.Exit 2.Back");
  
  display.display();
}

// ====================== POSITION READING ======================
void readPositions() {
  if (!adsPresent) return;
  
  unsigned long now = millis();
  static unsigned long lastAzRead = 0;
  static unsigned long lastElRead = 0;
  // Faster reads during tracking, smaller smoothing window
  const unsigned long ADC_INTERVAL = 100;
  
  // Moving average buffers to suppress relay-induced ADC spikes
  static int16_t azAdcHistory[4];
  static uint8_t azHistIdx = 0;
  static int16_t elAdcHistory[4];
  static uint8_t elHistIdx = 0;
  
  if (now - lastAzRead >= ADC_INTERVAL) {
    lastAzRead = now;
    int16_t rawAdcAz = ads.readADC_SingleEnded(0);
    rawAdcAz = constrain(rawAdcAz, 0, 32767);
    azAdcHistory[azHistIdx] = rawAdcAz;
    azHistIdx = (azHistIdx + 1) % 4;
    int32_t sumAz = 0;
    for (uint8_t i = 0; i < 4; i++) sumAz += azAdcHistory[i];
    int16_t adcAz = sumAz / 4;
    
    azVoltage = (float)adcAz / 32767.0 * 4.096F;
    
    if (calAzValid && (calAzMaxV - calAzMinV) > 0.01F) {
      float fraction = (azVoltage - calAzMinV) / (calAzMaxV - calAzMinV);
      fraction = constrain(fraction, 0, 1);
      currentAz = azOffset() + (fraction * azSpan());
    } else {
      float azFraction = (float)adcAz / 32767.0;
      currentAz = azFraction * 360.0;
    }
  }
  
  if (now - lastElRead >= ADC_INTERVAL) {
    lastElRead = now;
    int16_t rawAdcEl = ads.readADC_SingleEnded(1);
    rawAdcEl = constrain(rawAdcEl, 0, 32767);
    elAdcHistory[elHistIdx] = rawAdcEl;
    elHistIdx = (elHistIdx + 1) % 4;
    int32_t sumEl = 0;
    for (uint8_t i = 0; i < 4; i++) sumEl += elAdcHistory[i];
    int16_t adcEl = sumEl / 4;
    
    elVoltage = (float)adcEl / 32767.0 * 4.096F;
    
    if (calElValid && (calElMaxV - calElMinV) > 0.01F) {
      float fraction = (elVoltage - calElMinV) / (calElMaxV - calElMinV);
      fraction = constrain(fraction, 0, 1);
      currentEl = fraction * 180.0F;
    } else {
      float elFraction = (float)adcEl / 32767.0;
      currentEl = elFraction * 180.0;
    }
  }
}

// ====================== MENU DEFINITIONS ======================
// Forward declarations
void actionSetWifi();
void actionReviewWifi();
void actionConnectWifi();
void actionApMode();
void actionRotatorControl();
void actionBackToWifi();
void actionCalibratePots();
void actionCalibrateAz();
void actionCalibrateEl();
void actionBackToMain();
void actionRetryWifi();
void actionApStatus();
void actionRegenApPassword();
void actionRotatorModel();
void actionBootSkip();
void actionBootSetup();
void actionSetupRotator();
void actionSetupCalibrate();
void actionSetupWifi();
void actionWifiModeSta();
void actionWifiModeAp();
void actionWifiModeDisable();
void actionSetupWipe();
void showStationSettings();
int waitForWifiConnection(bool allowCancel = true);

// Boot Menu (first screen after boot)
// ==================== PC INTERFACE SETTINGS ====================
// Dynamic labels
char setupBaudLabel[32];
char setupShowCmdLabel[32];
char setupReverseDelayLabel[32];
void updateBaudLabel() {
  snprintf(setupBaudLabel, sizeof(setupBaudLabel), "Baud Rate (%ld)", getProtocolBaud());
}
void updateShowCmdLabel() {
  snprintf(setupShowCmdLabel, sizeof(setupShowCmdLabel), "Show Commands (%c)", protocolShowCommands ? 'Y' : 'N');
}
void updateReverseDelayLabel() {
  snprintf(setupReverseDelayLabel, sizeof(setupReverseDelayLabel), "Reverse Delay (%ds)", protocolReverseDelay);
}

// Protocol variant label and submenu
char setupProtocolLabel[32];
const char* protocolNames[] = {"GS-232A", "GS-232B", "Easycom"};

void updateProtocolLabel() {
  snprintf(setupProtocolLabel, sizeof(setupProtocolLabel), "Protocol (%s)", protocolNames[currentProtocol]);
}

void actionSetProtocol(int variant) {
  if (variant >= 0 && variant <= 2) {
    currentProtocol = (ProtocolVariant)variant;
    saveProtocolSettings();
    updateProtocolLabel();
  }
}

void actionProtoGS232A() {
  actionSetProtocol(PROTO_GS232A);
  updateProtocolLabel();
  currentMenu->back();
}
void actionProtoGS232B() {
  actionSetProtocol(PROTO_GS232B);
  updateProtocolLabel();
  currentMenu->back();
}
void actionProtoEasycom() {
  actionSetProtocol(PROTO_EASYCOM);
  updateProtocolLabel();
  currentMenu->back();
}

// Protocol submenu items
MenuItem protocolItems[] = {
  {"GS-232A (Yaesu)",   actionProtoGS232A,   0, true, nullptr, nullptr},
  {"GS-232B (Hamlib)",  actionProtoGS232B,   0, true, nullptr, nullptr},
  {"Easycom",           actionProtoEasycom,  0, true, nullptr, nullptr},
};

Menu protocolMenu("Protocol", protocolItems, 3, nullptr);

void actionSelectProtocol() {
  updateProtocolLabel();
  protocolMenu.setParent(&pcInterfaceMenu);
  currentMenu = &protocolMenu;
  currentMenu->init();
  currentMenu->setFooter(showCmdFooter());
}

void actionPcInterface() {
  updateBaudLabel();
  updateShowCmdLabel();
  updateReverseDelayLabel();
  updateProtocolLabel();
  currentMenu = &pcInterfaceMenu;
  currentMenu->init();
  currentMenu->setFooter(showCmdFooter());
}

// ==================== NETWORK MENU ====================
bool hamlibServerEnabled = true;  // Default: enabled
char setupHamlibLabel[24] = "Hamlib Server (Y)";

void updateNetworkLabels() {
  snprintf(setupHamlibLabel, sizeof(setupHamlibLabel), "Hamlib Server (%c)", hamlibServerEnabled ? 'Y' : 'N');
  networkItems[1].label = setupHamlibLabel;
  // WiFi mode label
  if (deviceWifiMode == MODE_STA) {
    strcpy(setupWifiLabel, "WiFi Mode(Station)");
  } else if (deviceWifiMode == MODE_AP) {
    strcpy(setupWifiLabel, "WiFi Mode(AP)");
  } else if (EEPROM.read(WIFI_MODE_MAGIC) == WIFI_MODE_MAGIC_VAL) {
    strcpy(setupWifiLabel, "WiFi Mode(Disabled)");
  } else {
    strcpy(setupWifiLabel, "WiFi Mode(None)");
  }
  networkItems[0].label = setupWifiLabel;
}

const char* hamlibFooter() {
  return hamlibServerEnabled ? "1.Nav 1|No 2.Back" : "1.Nav 1|Yes 2.Back";
}

void actionNetwork() {
  updateNetworkLabels();
  currentMenu = &networkMenu;
  currentMenu->init();
  currentMenu->setFooter(hamlibFooter());
}

void actionToggleHamlibServer() {
  hamlibServerEnabled = !hamlibServerEnabled;
  saveNetworkSettings();
  updateNetworkLabels();
  currentMenu->setFooter(hamlibFooter());
  currentMenu->draw();
  
  // Keep TCP server running — just change which protocol is spoken
  if (WiFi.status() == WL_CONNECTED && !rotctlServerStarted) {
    startRotctlServer();
  }
}

// Load/save network settings from EEPROM
void loadNetworkSettings() {
  if (EEPROM.read(EEPROM_NET_MAGIC) == EEPROM_NET_MAGIC_VAL) {
    hamlibServerEnabled = (EEPROM.read(EEPROM_NET_HAMLIB) == 1);
  }
}

void saveNetworkSettings() {
  EEPROM.write(EEPROM_NET_HAMLIB, hamlibServerEnabled ? 1 : 0);
  EEPROM.write(EEPROM_NET_MAGIC, EEPROM_NET_MAGIC_VAL);
  EEPROM.commit();
}

MenuItem networkItems[] = {
  {setupWifiLabel,      actionSetupWifi,       0, true, nullptr, nullptr},
  {setupHamlibLabel,    actionToggleHamlibServer, 0, true, nullptr, nullptr},
};

Menu networkMenu("Network", networkItems, 2, &setupMenu);

// Get custom footer for Show Commands toggle
const char* showCmdFooter() {
  return protocolShowCommands ? "1.Nav 1|No 2.Back" : "1.Nav 1|Yes 2.Back";
}

// Toggle Show Commands setting
void actionToggleShowCommands() {
  protocolShowCommands = !protocolShowCommands;
  saveProtocolSettings();
  updateShowCmdLabel();
  currentMenu->setFooter(showCmdFooter());
  currentMenu->draw();
}

void actionToggleReverseDelay() {
  // Cycle through 0, 1, 2, 3 seconds
  protocolReverseDelay = (protocolReverseDelay + 1) % 4;
  saveProtocolSettings();
  updateReverseDelayLabel();
  currentMenu->draw();
}

void actionPcBaudRate() {
  currentMenu = &baudMenu;
  currentMenu->init();
}

// Baud rate selection actions
void actionBaud115200() { protocolBaudIndex = 4; saveProtocolSettings(); applyProtocolBaud(); updateBaudLabel(); currentMenu->back(); }
void actionBaud57600()  { protocolBaudIndex = 3; saveProtocolSettings(); applyProtocolBaud(); updateBaudLabel(); currentMenu->back(); }
void actionBaud38400()  { protocolBaudIndex = 2; saveProtocolSettings(); applyProtocolBaud(); updateBaudLabel(); currentMenu->back(); }
void actionBaud19200()  { protocolBaudIndex = 1; saveProtocolSettings(); applyProtocolBaud(); updateBaudLabel(); currentMenu->back(); }
void actionBaud9600()   { protocolBaudIndex = 0; saveProtocolSettings(); applyProtocolBaud(); updateBaudLabel(); currentMenu->back(); }

// Baud rate selection menu (fastest to slowest)
MenuItem baudItems[] = {
  {"115200", actionBaud115200, 0, true, nullptr, nullptr},
  {"57600",  actionBaud57600,  0, true, nullptr, nullptr},
  {"38400",  actionBaud38400,  0, true, nullptr, nullptr},
  {"19200",  actionBaud19200,  0, true, nullptr, nullptr},
  {"9600",   actionBaud9600,   0, true, nullptr, nullptr},
};

Menu baudMenu("Select Baud Rate", baudItems, 5, &pcInterfaceMenu);

// PC Interface menu items
MenuItem pcInterfaceItems[] = {
  {setupBaudLabel,        actionPcBaudRate,           0, true, nullptr, nullptr},
  {setupShowCmdLabel,     actionToggleShowCommands,   0, true, nullptr, nullptr},
  {setupReverseDelayLabel, actionToggleReverseDelay,  0, true, nullptr, nullptr},
  {setupProtocolLabel,    actionSelectProtocol,       0, true, nullptr, nullptr},
};

Menu pcInterfaceMenu("PC Interface", pcInterfaceItems, 4, &setupMenu);

MenuItem bootItems[] = {
  {"Rotator Ctrl", actionBootSkip, 3, true, nullptr, nullptr},    // 3s timeout auto-select
  {"Setup", actionBootSetup, 0, true, nullptr, nullptr}
};

// Setup Menu
char setupRotatorLabel[24] = "Rot Variant";
char setupCalLabel[24] = "Rot Calibrate";
char setupWifiLabel[28] = "WiFi Mode(None)";

// Dependency check: Rot Calibrate requires Rot Variant to be configured
bool checkCalibrateSelectable() {
  return hasRotatorModel();
}

int getCalibrateBlockers(int* indices, int maxCount) {
  if (!hasRotatorModel() && maxCount > 0) {
    indices[0] = 0;  // Index 0 = "Rot Variant" (0-based)
    return 1;
  }
  return 0;
}

MenuItem setupItems[] = {
  {setupRotatorLabel, actionSetupRotator, 5, true, nullptr, nullptr},
  {setupCalLabel, actionSetupCalibrate, 0, true, checkCalibrateSelectable, getCalibrateBlockers},
  {"Network", actionNetwork, 0, true, nullptr, nullptr},
  {"PC Interface", actionPcInterface, 0, true, nullptr, nullptr},
  {"Wipe all settings", actionSetupWipe, 0, true, nullptr, nullptr}
};

void updateSetupLabels() {
  // Rotator variant label
  if (hasRotatorModel()) {
    const char* modelNames[4] = {"N360", "S360", "N450", "S450"};
    snprintf(setupRotatorLabel, sizeof(setupRotatorLabel), "Rot Variant (%s)", modelNames[rotModel]);
  } else {
    strcpy(setupRotatorLabel, "Rot Variant");
  }
  setupItems[0].label = setupRotatorLabel;

  // Calibrate label
  if (calAzValid && calElValid) {
    strcpy(setupCalLabel, "Rot Calibrate (Y)");
  } else {
    strcpy(setupCalLabel, "Rot Calibrate (N)");
  }
  setupItems[1].label = setupCalLabel;

  // WiFi mode label is now shown inside the Network submenu, not here
  // (label remains "Network")

  // Footer: hide Back button when mandatory setup items are incomplete
  if (allSetupComplete()) {
    setupMenu.setFooter("1.Nav 1|Sel 2.Back");
  } else {
    setupMenu.setFooter("1.Nav 1|Sel");
  }
}

// Check if all mandatory setup items are configured
bool allSetupComplete() {
  return hasRotatorModel() && calAzValid && calElValid && EEPROM.read(WIFI_MODE_MAGIC) == WIFI_MODE_MAGIC_VAL;
}

// Set auto-select timer on the next unconfigured mandatory item
void updateSetupTimers() {
  // Reset all timers first
  setupItems[0].timerSeconds = 0;
  setupItems[1].timerSeconds = 0;
  setupItems[2].timerSeconds = 0;
  setupItems[3].timerSeconds = 0;

  // Find first unconfigured mandatory item
  if (!hasRotatorModel()) {
    setupItems[0].timerSeconds = 5;
    return;
  }
  if (!calAzValid || !calElValid) {
    setupItems[1].timerSeconds = 5;
    return;
  }
  if (EEPROM.read(WIFI_MODE_MAGIC) != WIFI_MODE_MAGIC_VAL) {
    setupItems[2].timerSeconds = 5;
    return;
  }
  // All configured - no timer needed
}

// WiFi Mode Menu
MenuItem wifiModeItems[] = {
  {"Station", actionWifiModeSta, 0, true, nullptr, nullptr},
  {"Access Point", actionWifiModeAp, 0, true, nullptr, nullptr},
  {"Disable", actionWifiModeDisable, 0, true, nullptr, nullptr}
};

// WiFi Connect Menu
MenuItem wifiConnectItems[] = {
  {"Set WiFi Creds", actionSetWifi, 0, true, nullptr, nullptr},
  {"Review Creds", actionReviewWifi, 0, true, nullptr, nullptr},
  {"Connect WiFi", actionConnectWifi, 5, false, nullptr, nullptr},  // Hidden until SSID saved
  {"Show IP", actionShowIP, 0, true, nullptr, nullptr},
  {"AP Mode (Field)", actionApMode, 0, true, nullptr, nullptr},
  {"Rotator Model", actionRotatorModel, 0, true, nullptr, nullptr}
};

// AP Mode Menu (shown when user selects Access Point from WiFi Mode)
MenuItem apModeItems[] = {
  {"AP Status", actionApStatus, 0, true, nullptr, nullptr},
  {"Regen AP Password", actionRegenApPassword, 0, true, nullptr, nullptr}
};

// Voltage Calibration Menu
MenuItem calPotsItems[] = {
  {"Azimuth", actionCalibrateAz, 0, true, nullptr, nullptr},
  {"Elevation", actionCalibrateEl, 0, true, nullptr, nullptr},
  {"Skip (Az=El=3.2V)", actionCalibrateSkip, 0, true, nullptr, nullptr}
};

// Menu instances
Menu bootMenu("Boot", bootItems, 2, nullptr);
Menu setupMenu("SETUP", setupItems, 5, &bootMenu);
Menu wifiModeMenu("WiFi Mode", wifiModeItems, 3, &networkMenu);
Menu wifiConnectMenu("WiFi Connect", wifiConnectItems, 6, &setupMenu);
Menu apModeMenu("AP Mode", apModeItems, 2, &wifiModeMenu);
Menu calPotsMenu("Voltage Calibration", calPotsItems, 3, &setupMenu);

// Current active menu (nullptr = rotator control mode)
Menu* currentMenu = &bootMenu;

// ====================== MENU ACTIONS ======================
void actionSetWifi() {
  Serial.println("Action: Set WiFi Credentials");
  
  // Stop any existing WiFi, start captive portal
  WiFi.disconnect();
  stopCaptivePortal();
  startCaptivePortal();
  
  const char* lines[] = {
    "Connect phone to:",
    apSsid,
    "",
    "IP: 192.168.4.1",
    ""
  };
  Menu::drawNotice("Captive Portal", lines, 5, "1.Done 2.Cancel", true);
  
  // Wait for button to fully release after long press
  delay(500);
  while (digitalRead(BTN1) || !digitalRead(BTN2)) {
    delay(50);
  }
  delay(100);
  
  // Block until user dismisses or credentials saved
  unsigned long portalStart = millis();
  unsigned long btn2PressStart = 0;
  while (millis() - portalStart < 600000) {
    tickCaptivePortal();
    
    if (!captivePortalActive) {
      // Credentials were saved and device rebooted
      // This won't be reached due to ESP.restart()
      break;
    }
    
    // Btn1: Done - stop portal and return to menu
    if (digitalRead(BTN1)) {
      delay(300);
      stopCaptivePortal();
      loadWifiCredentials();
      wifiConnectItems[2].visible = hasWifiCredentials();
      currentMenu->init();
      return;
    }
    
    // Btn2: Cancel - require sustained press
    if (!digitalRead(BTN2)) {
      if (btn2PressStart == 0) {
        btn2PressStart = millis();
      } else if (millis() - btn2PressStart > 500) {
        stopCaptivePortal();
        currentMenu->init();
        return;
      }
    } else {
      btn2PressStart = 0;
    }
    
    delay(50);
  }
  
  stopCaptivePortal();
  currentMenu->init();
}

void actionReviewWifi() {
  Serial.println("Action: Review Credentials");
  
  // Keep String objects alive to avoid dangling pointers
  String ssidLine = "SSID: " + savedSsid;
  String passLine = "Pass: " + maskPassword(savedPass);
  const char* lines[] = { ssidLine.c_str(), passLine.c_str(), "" };
  Menu::drawNotice("WiFi Creds", lines, 3, "1.OK 2.Back", true);
  
  // Wait for button to fully release after long press
  delay(500);
  while (digitalRead(BTN1) || !digitalRead(BTN2)) {
    delay(50);
  }
  delay(100);
  
  while (true) {
    // Check for MODE:ROTATOR command to escape blocking menu
    if (checkModeCommand()) return;
  // Wait indefinitely for user input
    if (digitalRead(BTN1)) {
      delay(300);
      currentMenu->draw();  // Preserve cursor position
      return;
    }
    if (!digitalRead(BTN2)) {
      delay(300);
      currentMenu->back();
      return;
    }
    delay(50);
  }
}

void actionConnectWifi() {
  Serial.println("Action: Connect WiFi");
  
  String netLine = "Network: " + savedSsid;
  const char* lines[] = { netLine.c_str(), "", "Please wait..." };
  Menu::drawNotice("WiFi Connecting", lines, 3, "2:Cancel", true);
  
  connectWifiSta();
  
  int dotCount = 0;
  while (wifiState == WIFI_CONNECTING) {
    tickWifi();
    ESP.wdtFeed();  // Prevent watchdog reset during long WiFi wait
    
    dotCount++;
    if (dotCount % 4 == 0) {
      String waitLine = "Waiting: " + String(dotCount / 4) + "s";
      String netLine2 = "Network: " + savedSsid;
      const char* lines2[] = { netLine2.c_str(), "", waitLine.c_str() };
      Menu::drawNotice("WiFi Connecting", lines2, 3, "2:Cancel", true);
    }
    
    if (!digitalRead(BTN2)) {
      delay(300);
      WiFi.disconnect();
      wifiState = WIFI_IDLE;
      currentMenu->draw();  // Preserve cursor position
      return;
    }
    
    delay(250);
  }
  
  if (wifiState == WIFI_CONNECTED) {
    // Show connected screen with IP, signal strength, and countdown
    int rssi = WiFi.RSSI();
    String quality;
    if (rssi >= -50) quality = "Excellent";
    else if (rssi >= -60) quality = "Good";
    else if (rssi >= -70) quality = "Fair";
    else quality = "Poor";
    
    String ipStr = WiFi.localIP().toString();
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Connected!");
    display.setTextSize(1);
    display.setCursor(0, 24);
    display.print("IP: "); display.println(ipStr);
    display.setCursor(0, 36);
    display.print("Signal: "); display.print(rssi); display.println(" dBm");
    display.setCursor(0, 48);
    display.print("Quality: "); display.println(quality);
    
    // Countdown from 5 seconds — just a timer in bottom-right corner
    for (int sec = 5; sec > 0; sec--) {
      ESP.wdtFeed();
      display.fillRect(118, 56, 10, 8, SSD1306_BLACK);
      display.setCursor(120, 56);
      display.print(sec);
      display.display();
      
      // Check for button press to skip ahead
      for (int i = 0; i < 20; i++) {
        if (digitalRead(BTN1) || !digitalRead(BTN2)) {
          delay(300);
          currentMenu = nullptr;  // Rotator control mode
          return;
        }
        delay(50);
      }
    }
    
    currentMenu = nullptr;  // Rotator control mode
  } else {
    // Connection failed - switch to AP mode and show notice
    WiFi.disconnect();
    startApMode();
    wifiState = WIFI_FAILED;
    
    // Show notice screen with countdown
    String countLine = "Continuing in 5s...";
    String ssidLine = "SSID: " + savedSsid;
    String apSsidLine = "SSID: " + String(apSsid);
    String apPassLine = "Pass: " + String(apPassword);
    const char* lines[] = {
      ssidLine.c_str(),
      "",
      "Switching to AP mode",
      apSsidLine.c_str(),
      apPassLine.c_str()
    };
    Menu::drawNotice("WiFi Failed", lines, 5, countLine.c_str(), true);
    
    // Countdown from 5 seconds
    for (int sec = 5; sec > 0; sec--) {
      // Update countdown - redraw just the footer area
      ESP.wdtFeed();  // Prevent watchdog reset
      display.fillRect(0, 53, 128, 12, SSD1306_BLACK);
      // Redraw dotted separator
      for (int x = 0; x < 128; x += 3) {
        display.drawPixel(x, 53, SSD1306_WHITE);
      }
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 56);
      display.print("Continuing in ");
      display.print(sec);
      display.println("s...");
      display.display();
      
      // Check for button presses during countdown
      for (int i = 0; i < 20; i++) {  // ~1 second per loop (50ms * 20)
        if (digitalRead(BTN1)) {
          delay(300);
          actionConnectWifi();  // Retry
          return;
        }
        if (!digitalRead(BTN2)) {
          delay(300);
          // Skip to AP Mode menu
          currentMenu = &apModeMenu;
          currentMenu->init();
          return;
        }
        delay(50);
      }
    }
    
    // Auto-transition to AP Mode menu
    currentMenu = &apModeMenu;
    currentMenu->init();
  }
}

void actionApMode() {
  Serial.println("Action: AP Mode (Field)");
  
  WiFi.disconnect();
  stopApMode();
  startApMode();
  
  currentMenu = &apModeMenu;
  currentMenu->init();
}

void actionRotatorControl() {
  // Enter rotator control mode
  stopAll();
  currentMenu = nullptr;  // nullptr = rotator control mode
}

void actionBackToWifi() {
  Serial.println("Action: Back to WiFi Connect");
  currentMenu = &wifiConnectMenu;
  currentMenu->init();
}

void actionCalibratePots() {
  Serial.println("Action: Calibrate Pots");
  currentMenu = &calPotsMenu;
  currentMenu->init();
}

void actionCalibrateAz() {
  Serial.println("Action: Calibrate AZ Pot");
  enterCalibrationMode(0);
}

void actionCalibrateEl() {
  Serial.println("Action: Calibrate EL Pot");
  enterCalibrationMode(1);
}

void actionCalibrateSkip() {
  Serial.println("CAL: Skipping calibration - using Az=El=3.2V");
  
  // Set calibration values as if sweep found 0V min and 3.2V max
  calAzMinV = 0.0F;
  calAzMaxV = 3.2F;
  calElMinV = 0.0F;
  calElMaxV = 3.2F;
  calAzValid = true;
  calElValid = true;
  
  // Save to EEPROM
  uint8_t* bytes;
  bytes = (uint8_t*)&calAzMinV;
  for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_AZ_MIN + i, bytes[i]);
  bytes = (uint8_t*)&calAzMaxV;
  for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_AZ_MAX + i, bytes[i]);
  bytes = (uint8_t*)&calElMinV;
  for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_EL_MIN + i, bytes[i]);
  bytes = (uint8_t*)&calElMaxV;
  for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_EL_MAX + i, bytes[i]);
  EEPROM.write(CAL_EEPROM_MAGIC, CAL_EEPROM_MAGIC_VAL);
  EEPROM.commit();
  
  Serial.println("CAL: Calibration saved (Az=El=0-3.2V)");

  // Update setup menu labels to reflect calibration status
  updateSetupLabels();

  // Refresh auto-timer logic — calibration is now done, no need to prompt for it
  updateSetupTimers();

  // Return to Setup screen
  currentMenu = &setupMenu;
  currentMenu->init();
}

void actionBackToMain() {
  Serial.println("Action: Back to Main");
  exitCalibrationMode();
  // Go to rotator control mode
  currentMenu = nullptr;
}

void actionRetryWifi() {
  Serial.println("Action: Retry Home WiFi");
  actionConnectWifi();
}

void actionApStatus() {
  Serial.println("Action: AP Status");
  
  // Get actual SSID and IP from WiFi (in case softAP used fallback)
  String actualSsid = WiFi.softAPSSID();
  IPAddress apIp = WiFi.softAPIP();
  if (apIp == IPAddress(0,0,0,0)) {
    apIp = IPAddress(192, 168, 4, 1);  // Fallback to configured IP
  }
  
  // Keep String objects alive to avoid dangling pointers
  String ssidLine = "SSID: " + actualSsid;
  String passLine = "Pass: " + String(apPassword);
  String ipLine = "IP: " + apIp.toString();
  const char* lines[] = {
    "AP Mode Active",
    ssidLine.c_str(),
    passLine.c_str(),
    ipLine.c_str()
  };
  Menu::drawNotice("AP Status", lines, 5, "1.OK 2.Back", true);
  
  // Wait for button to fully release after long press
  delay(500);
  while (digitalRead(BTN1) || !digitalRead(BTN2)) {
    delay(50);
  }
  delay(100);
  
  while (true) {
    // Check for MODE:ROTATOR command to escape blocking menu
    if (checkModeCommand()) return;
  // Wait indefinitely for user input
    if (digitalRead(BTN1)) {
      delay(300);
      currentMenu = &apModeMenu;
      currentMenu->init();
      return;
    }
    if (!digitalRead(BTN2)) {
      delay(300);
      currentMenu = &apModeMenu;
      currentMenu->init();
      return;
    }
    delay(50);
  }
}

void actionRegenApPassword() {
  Serial.println("Action: Regen AP Password");
  
  // Wait for button to fully release after long press
  delay(500);
  while (digitalRead(BTN1) || !digitalRead(BTN2)) {
    delay(50);
  }
  
  // Show "Generating..." screen
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 12);
  display.println("Generating");
  display.setTextSize(1);
  display.setCursor(0, 30);
  display.println("new password...");
  display.display();
  
  // Collect entropy by measuring button press timing
  // First, wait for button press
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 12);
  display.println("Press");
  display.setCursor(0, 28);
  display.println("Btn1");
  display.display();
  
  unsigned long pressTime = 0;
  while (!digitalRead(BTN1)) {
    delay(10);
  }
  delay(200);
  pressTime = millis();
  
  // Generate random 8-character alphanumeric password using button press time as seed
  // WPA2 requires minimum 8 characters
  randomSeed(pressTime + micros());
  const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789";
  const int charsetLen = 58;  // Excluded confusing chars (0,O,1,l,I)
  
  for (int i = 0; i < 8; i++) {
    apPassword[i] = charset[random(charsetLen)];
  }
  apPassword[8] = '\0';
  saveApPassword();
  
  // Show the new password
  String passStr = String(apPassword);
  String line1 = "New AP";
  String line2 = "Password:";
  String line3 = passStr;
  
  // Keep strings alive
  const char* lines[] = {
    line1.c_str(),
    line2.c_str(),
    "",
    line3.c_str(),
    ""
  };
  
  Menu::drawNotice("AP Password", lines, 5, "1.OK", true);
  
  // Wait for button to fully release
  delay(500);
  while (digitalRead(BTN1) || !digitalRead(BTN2)) {
    delay(50);
  }
  delay(100);
  
  // Wait indefinitely for Btn1 press to acknowledge
  while (true) {
    if (checkModeCommand()) return;
    if (digitalRead(BTN1)) {
      delay(300);
      currentMenu = &apModeMenu;
      currentMenu->init();
      return;
    }
    delay(50);
  }
}

void actionRotatorModel() {
  Serial.println("Action: Rotator Model");
  
  // Show current model
  const char* modelNames[4] = {"N360", "S360", "N450", "S450"};
  String modelStr = modelNames[rotModel];
  String rangeStr = azRangeLabel();
  
  String modelLine = "Model: " + modelStr;
  String rangeLine = "Range: " + rangeStr;
  
  const char* lines[] = {
    modelLine.c_str(),
    rangeLine.c_str(),
    "",
    "Change?",
    ""
  };
  Menu::drawNotice("Rotator Model", lines, 5, "1:Change 2:Back", true);
  
  // Wait for button to fully release after long press
  delay(500);
  while (digitalRead(BTN1) || !digitalRead(BTN2)) {
    delay(50);
  }
  delay(100);
  
  while (true) {
    // Check for MODE:ROTATOR command to escape blocking menu
    if (checkModeCommand()) return;

    if (digitalRead(BTN1)) {
      delay(300);
      // Re-enter setup
      setupRotatorModel();
      currentMenu = &wifiConnectMenu;
      currentMenu->init();
      return;
    }
    if (!digitalRead(BTN2)) {
      delay(300);
      currentMenu = &wifiConnectMenu;
      currentMenu->init();
      return;
    }
    delay(50);
  }
}

// ====================== BOOT & SETUP ACTIONS ======================

// Show WiFi connecting screen on OLED
void showWifiConnecting() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("WiFi");
  display.setCursor(0, 20);
  display.println("Connect");
  display.setTextSize(1);
  display.setCursor(0, 44);
  display.print("SSID: "); display.println(savedSsid);
  display.setCursor(0, 56);
  display.println("Please wait...");
  display.display();
}

// Wait for WiFi connection during boot with visual feedback
void waitForBootWifi() {
  int dotCount = 0;
  unsigned long startTime = millis();
  const unsigned long TIMEOUT = 15000;
  
  while (wifiState == WIFI_CONNECTING && millis() - startTime < TIMEOUT) {
    tickWifi();
    ESP.wdtFeed();
    
    dotCount++;
    if (dotCount % 4 == 0) {
      // Update waiting dots
      display.fillRect(0, 56, 128, 8, SSD1306_BLACK);
      display.setCursor(0, 56);
      display.print("Waiting: "); display.print(dotCount / 4); display.println("s");
      display.display();
    }
    delay(250);
  }
  
  if (wifiState == WIFI_CONNECTED) {
    // Show connected screen with IP and 5-second countdown
    String ipStr = WiFi.localIP().toString();
    String rssiStr = String(WiFi.RSSI()) + " dBm";
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Connected!");
    display.setTextSize(1);
    display.setCursor(0, 24);
    display.print("IP: "); display.println(ipStr);
    display.setCursor(0, 36);
    display.print("Signal: "); display.println(rssiStr);
    display.setCursor(0, 48);
    display.print("SSID: "); display.println(savedSsid);
    
    // Countdown from 5 seconds — just a timer in bottom-right corner
    for (int sec = 5; sec > 0; sec--) {
      ESP.wdtFeed();
      display.fillRect(118, 56, 10, 8, SSD1306_BLACK);
      display.setCursor(120, 56);
      display.print(sec);
      display.display();
      
      // Check for button press to skip ahead
      for (int i = 0; i < 20; i++) {
        if (digitalRead(BTN1) || !digitalRead(BTN2)) {
          delay(300);
          return;
        }
        delay(50);
      }
    }
  } else {
    // Connection failed — show error briefly
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 12);
    display.println("WiFi");
    display.setCursor(0, 28);
    display.println("Failed!");
    display.display();
    delay(2000);
    WiFi.disconnect();
  }
}

// Show current IP address on a notice screen
void actionShowIP() {
  if (WiFi.status() == WL_CONNECTED) {
    String ipStr = WiFi.localIP().toString();
    String rssiStr = String(WiFi.RSSI()) + " dBm";
    String modeStr = (deviceWifiMode == MODE_STA) ? "Station" : "AP Mode";
    const char* lines[] = {
      modeStr.c_str(),
      "",
      ("IP: " + ipStr).c_str(),
      ("Signal: " + rssiStr).c_str(),
      ("SSID: " + savedSsid).c_str()
    };
    Menu::drawNotice("WiFi Status", lines, 5, "1|OK  2.Back", true);
    
    // Wait for button press
    while (true) {
      ESP.wdtFeed();
      if (digitalRead(BTN1)) {
        delay(300);
        return;
      }
      if (!digitalRead(BTN2)) {
        delay(300);
        return;
      }
      delay(50);
    }
  } else {
    Menu::drawQuickNotice("WiFi", "Not Connected", "", true);
    delay(2000);
  }
}

void actionBootSkip() {
  Serial.println("BOOT: Rotator Ctrl selected");
  // Apply saved WiFi mode (skip if already connected)
  if (deviceWifiMode == MODE_STA && hasWifiCredentials() && wifiState != WIFI_CONNECTED) {
    showWifiConnecting();
    connectWifiSta();
    waitForBootWifi();
  } else if (deviceWifiMode == MODE_AP) {
    startApMode();
  }
  // Go directly to rotator control
  actionRotatorControl();
}

void actionBootSetup() {
  Serial.println("BOOT: Setup selected");
  updateSetupLabels();
  updateSetupTimers();
  currentMenu = &setupMenu;
  currentMenu->init();
}

void actionSetupRotator() {
  Serial.println("SETUP: Rotator setup");
  setupRotatorModel();
  // Wait for Btn1 to be fully released after long-press selection in setupRotatorModel
  // (setupRotatorModel returns immediately on long-press without waiting for release)
  while (digitalRead(BTN1)) { delay(10); }
  delay(50);
  // Go back to setup menu
  updateSetupLabels();
  updateSetupTimers();
  currentMenu = &setupMenu;
  currentMenu->init();
  skipBtnProcessing = true;  // Skip next button cycle as safety
}

void actionSetupWifi() {
  Serial.println("SETUP: WiFi setup");
  currentMenu = &wifiModeMenu;
  currentMenu->init();
}

void actionSetupCalibrate() {
  Serial.println("SETUP: Calibrate");
  currentMenu = &calPotsMenu;
  currentMenu->init();
}

void actionSetupWipe() {
  Serial.println("SETUP: Wipe confirmation");

  // Show confirmation screen
  const char* lines[] = {
    "This will erase ALL",
    "saved settings.",
    "",
    "Continue?",
    ""
  };
  Menu::drawNotice("Wipe all settings", lines, 5, "1.Wipe  2.Back", true);

  // Wait for any button still held from the previous screen
  while (digitalRead(BTN1) || !digitalRead(BTN2)) delay(50);
  delay(100);

  // Wait for user choice
  while (true) {
    // Check for MODE:ROTATOR command to escape blocking menu
    if (checkModeCommand()) return;

    if (digitalRead(BTN1)) {
      delay(300);
      // Wipe all EEPROM settings
      Serial.println("SETUP: Wiping all settings...");
      for (int i = 0; i < EEPROM_SIZE; i++) {
        EEPROM.write(i, 0);
      }
      EEPROM.commit();

      // Reset in-memory state
      deviceWifiMode = MODE_DISABLED;
      savedSsid = "";
      savedPass = "";
      calAzValid = false;
      calElValid = false;

      // Show confirmation
      Menu::drawQuickNotice("Settings", "Wiped!", "", true);
      delay(2000);

      // Reboot to start fresh
      ESP.restart();
      while (true) delay(1000);  // Should not reach here
    }
    if (!digitalRead(BTN2)) {
      delay(300);
      // Go back to setup menu
      return;
    }
    delay(50);
  }
}

void actionWifiModeSta() {
  Serial.println("WIFI: Mode set to Station");
  
  // If currently in AP mode, clean up DNS and WiFi before switching
  if (wifiState == WIFI_AP_MODE) {
    Serial.println("WIFI: Stopping AP mode before switching to Station");
    stopApMode();
  }
  
  deviceWifiMode = MODE_STA;
  saveWifiMode();
  // Show Station Settings screen
  showStationSettings();
  // Back from station settings — return to setup
  updateSetupLabels();
  updateSetupTimers();
  currentMenu = &setupMenu;
  currentMenu->init();
  Serial.println("WIFI: SETUP menu initialized");
}

void actionWifiModeAp() {
  Serial.println("WIFI: Mode set to Access Point");
  deviceWifiMode = MODE_AP;
  saveWifiMode();
  startApMode();
  currentMenu = &apModeMenu;
  currentMenu->init();
}

void actionWifiModeDisable() {
  Serial.println("WIFI: Mode set to Disabled");
  
  // If currently in AP mode, clean up DNS and WiFi before switching
  if (wifiState == WIFI_AP_MODE) {
    stopApMode();
  }
  
  // Disconnect if currently connected
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(500);
    
    // Show disconnected confirmation
    Menu::drawQuickNotice("Dis-", "Connected", "", true);
    delay(2000);
  }
  
  // Clear saved credentials
  saveWifiCredentials("", "");
  
  deviceWifiMode = MODE_DISABLED;
  saveWifiMode();
  stopCaptivePortal();
  stopRotctlServer();
  // Go back to setup menu
  updateSetupLabels();
  updateSetupTimers();
  currentMenu = &setupMenu;
  currentMenu->init();
}

void showStationSettings() {
  Serial.println("WIFI: Showing Station Settings");

  int choice = 0;  // 0=Set via HTTP, 1=Connect/Disconnect
  bool btn1WasPressed = false;
  bool btn2WasPressed = false;
  unsigned long btn1PressStart = 0;
  bool btn1LongPressTriggered = false;
  bool firstDraw = true;

  while (true) {
    // Check for MODE:ROTATOR command to escape blocking menu
    if (checkModeCommand()) return;

    btn1WasPressed = false;
    btn2WasPressed = false;
    btn1PressStart = 0;
    btn1LongPressTriggered = false;

    // Determine second option text based on connection state
    const char* opt1;
    if (WiFi.status() == WL_CONNECTED) {
      opt1 = "Disconnect";
    } else {
      opt1 = "Connect";
    }
    const char* opts[2] = {"Set via HTTP", opt1};

    while (true) {
    // Check for MODE:ROTATOR command to escape blocking menu
    if (checkModeCommand()) return;

      display.clearDisplay();
      display.setTextSize(1);

      // Title bar
      display.fillRect(0, 0, 128, 8, SSD1306_BLACK);
      display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
      display.setCursor(0, 0);
      display.print("Wifi Station Settings        ");

      // Solid separator
      display.drawLine(0, 8, 127, 8, SSD1306_WHITE);

      // Options
      display.setTextColor(SSD1306_WHITE);
      int y = 12;
      for (int i = 0; i < 2; i++) {
        if (i == choice) {
          display.fillRect(0, y, 128, 8, SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        } else {
          display.setTextColor(SSD1306_WHITE);
        }
        display.setCursor(2, y);
        display.print(opts[i]);
        y += 11;
      }

      // Dotted separator (just below options at y=32)
      for (int x = 0; x < 128; x += 3) {
        display.drawPixel(x, 32, SSD1306_WHITE);
      }

      // SSID info
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(2, 35);
      if (savedSsid.length() > 0) {
        display.print("SSID:");
        display.println(savedSsid);
      } else {
        display.println("SSID: None");
      }

      // Connection info (IP if connected, password if not)
      display.setCursor(2, 44);
      if (WiFi.status() == WL_CONNECTED) {
        display.print("IP: "); display.println(WiFi.localIP().toString());
      } else if (savedPass.length() > 1) {
        String masked = String(savedPass[0]) + "*..*" + String(savedPass[savedPass.length() - 1]);
        display.print("Passwd: ");
        display.println(masked);
      } else if (savedPass.length() == 1) {
        display.print("Passwd: *");
      } else {
        display.println("Passwd: None");
      }

      // Footer
      for (int x = 0; x < 128; x += 3) {
        display.drawPixel(x, 53, SSD1306_WHITE);
      }
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 56);
      display.println("1.Nav  1|Sel  2.Back");

      display.display();

      // Skip button processing on first draw to prevent carryover from previous screen
      if (firstDraw) {
        firstDraw = false;
        // Wait for buttons released before first processing
        while (digitalRead(BTN1) || !digitalRead(BTN2)) delay(50);
        delay(100);
        continue;
      }

      unsigned long now = millis();
      bool btn1 = digitalRead(BTN1);
      bool btn2 = !digitalRead(BTN2);

      // Btn1: short-press = navigate, long-press = select
      if (btn1 && !btn1WasPressed) {
        resetOledCmdSuppress();
        btn1PressStart = now;
        btn1LongPressTriggered = false;
      }

      // Long press detected
      if (btn1 && (now - btn1PressStart >= 800) && !btn1LongPressTriggered) {
        btn1LongPressTriggered = true;
        Serial.print("WIFI: Selected option ");
        Serial.println(choice);

        if (choice == 0) {
          // Set via HTTP
          WiFi.disconnect();
          stopCaptivePortal();
          startCaptivePortal();

          const char* lines[] = {
            "Connect phone to:",
            apSsid,
            "",
            "IP: 192.168.4.1",
            ""
          };
          Menu::drawNotice("Captive Portal", lines, 5, "1.Done 2.Cancel", true);

          delay(500);
          while (digitalRead(BTN1) || !digitalRead(BTN2)) delay(50);
          delay(100);

          unsigned long btn2Hold = 0;
          while (true) {
    // Check for MODE:ROTATOR command to escape blocking menu
    if (checkModeCommand()) return;

            tickCaptivePortal();
            if (!captivePortalActive) {
              // Captive portal stopped (credentials saved or timeout)
              // Switch to Connect option so user can connect immediately
              loadWifiCredentials();
              choice = 1;
              break;
            }
            if (digitalRead(BTN1)) {
              delay(300);
              stopCaptivePortal();
              loadWifiCredentials();
              choice = 1;
              break;  // Return to outer loop with Connect option selected — avoid recursive call
            }
            if (!digitalRead(BTN2)) {
              if (btn2Hold == 0) btn2Hold = millis();
              else if (millis() - btn2Hold > 500) {
                stopCaptivePortal();
                showStationSettings();
                return;
              }
            } else {
              btn2Hold = 0;
            }
            delay(50);
          }
        } else {
          // Connect or Disconnect
          if (WiFi.status() == WL_CONNECTED) {
            // Disconnect
            Serial.println("WIFI: Disconnecting...");
            WiFi.disconnect();
            WiFi.mode(WIFI_OFF);
            delay(500);

            // Show Disconnected status for 3 seconds
            Menu::drawQuickNotice("Dis-", "Connected", "", true);
            delay(3000);

            // Return to station settings with choice on Connect
            choice = 1;
          } else {
            // Connect
            if (hasWifiCredentials()) {
              loadWifiCredentials();
              int result = waitForWifiConnection(true);

              if (result == 1) {
                // Connected - show connected status then return to station settings
                // waitForWifiConnection already shows the Connected screen
                choice = 1;  // Keep selection on Connect/Disconnect
              }
              // Cancelled or failed - stay on station settings
            }
          }
        }
        break;  // Exit inner loop to redraw
      }

      // Short press released (navigates)
      if (!btn1 && btn1WasPressed && !btn1LongPressTriggered && (now - btn1PressStart < 800)) {
        choice = (choice + 1) % 2;
        Serial.print("WIFI: Cycle to ");
        Serial.println(choice);
      }

      btn1WasPressed = btn1;

      // Btn2 - back
      if (btn2 && !btn2WasPressed) {
        resetOledCmdSuppress();
        Serial.println("WIFI: Back from Station Settings");
        while (!digitalRead(BTN2)) delay(50);
        return;
      }
      btn2WasPressed = btn2;

      delay(50);
    }
  }
}

// ====================== ROTATOR CONTROL DISPLAY ======================
int rotatorMenuSelection = 0;
const char* directions[6] = {"SETUP", "LEFT", "RIGHT", "UP", "DOWN", "ELF"};

// ====================== ELF ELEVATION PROMPT OVERLAY ======================
void showElfElevPrompt() {
  const char* options[] = {"Horizon (0)", "45", "90", "Skip"};
  int choice = 0;
  bool btn1WasPressed = false;
  bool btn2WasPressed = false;
  bool firstDraw = true;
  unsigned long promptStart = millis();
  const unsigned long PROMPT_TIMEOUT_MS = 5000;
  bool doSelect = false;
  bool userInteracted = false;

  while (true) {
    checkModeCommand();

    bool btn1 = digitalRead(BTN1);
    bool btn2 = !digitalRead(BTN2);

    // Auto-select default ONLY if user hasn't interacted and timeout expired
    if (!userInteracted && millis() - promptStart >= PROMPT_TIMEOUT_MS) {
      doSelect = true;
    }

    display.clearDisplay();
    display.setTextSize(1);

    display.fillRect(0, 0, 128, 8, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setCursor(0, 0);
    display.print("Elevate To?                ");

    display.drawLine(0, 8, 127, 8, SSD1306_WHITE);

    int y = 12;
    for (int i = 0; i < 4; i++) {
      if (i == choice) {
        display.fillRect(0, y, 128, 8, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }

      display.setCursor(2, y);
      display.print(options[i]);
      
      // Countdown [x] next to first item — disappears on any button press
      if (i == 0 && !userInteracted) {
        int remaining = max(0, (int)((PROMPT_TIMEOUT_MS - (millis() - promptStart)) / 1000));
        display.print(" [");
        display.print(remaining);
        display.print("]");
      }
      
      y += 11;
    }

    for (int x = 0; x < 128; x += 3) {
      display.drawPixel(x, 53, SSD1306_WHITE);
    }

    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 56);
    display.println("1.Nav  1|Sel  2.Back");

    display.display();

    if (firstDraw) {
      firstDraw = false;
      while (digitalRead(BTN1) || !digitalRead(BTN2)) delay(50);
      delay(100);
      btn1WasPressed = false;
      btn2WasPressed = false;
      continue;
    }

    unsigned long now = millis();

    // Btn1: short-press = cycle, long-press = select
    if (btn1 && !btn1WasPressed) {
      resetOledCmdSuppress();
      unsigned long pressStart = now;
      while (digitalRead(BTN1) && (millis() - pressStart < 800)) {
        delay(20);
      }
      if (millis() - pressStart >= 800) {
        doSelect = true;
      } else {
        choice = (choice + 1) % 4;
        userInteracted = true;
        promptStart = now;
      }
    }

    // Execute selection
    if (doSelect) {
      Serial.print("ELF: Elevate to choice "); Serial.println(choice);
      if (choice == 3) {
        Serial.println("ELF: Elevation change skipped");
      } else {
        int targetEl = (choice == 0) ? 0 : (choice == 1) ? 45 : 90;
        Serial.print("ELF: Setting effective elevation to "); Serial.println(targetEl);
        executeProtocolElMove((float)targetEl);
      }
      return;
    }

    // Btn2: edge-detect press = back (skip)
    if (btn2 && !btn2WasPressed) {
      resetOledCmdSuppress();
      delay(BTN_DEBOUNCE_MS);
      Serial.println("ELF: Back from elevation prompt (skipped)");
      while (!digitalRead(BTN2)) delay(50);
      delay(50);
      return;
    }

    btn1WasPressed = btn1;
    btn2WasPressed = btn2;

    delay(50);
  }
}



// Flatten a protocol line for the one-line-tall OLED RX/TX strip: CR/LF
// become '^' so multi-line replies stay on one line, and the result is
// hard-truncated to maxLen chars.
static void oledFlattenLine(const char* src, char* dst, int maxLen) {
  int n = 0;
  for (int i = 0; src[i] != '\0' && n < maxLen; i++) {
    char c = src[i];
    if (c == '\n' || c == '\r') c = '^';
    dst[n++] = c;
  }
  dst[n] = '\0';
}

void updateRotatorDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  
  // Network connection indicator — above footer dotted line at y=53
  // Dotted line is at y=53, footer text at y=56, so indicator at y=49 (3px gap)
  if (rotctlConnected) {
    display.fillRect(125, 49, 2, 2, SSD1306_WHITE);
  }
  
  if (isPausing) {
    // Title bar (white on black)
    display.fillRect(0, 0, 128, 8, SSD1306_BLACK);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setCursor(0, 0);
    display.println("SAFETY PAUSE");
    // Solid separator at row 8
    display.drawLine(0, 8, 127, 8, SSD1306_WHITE);
    
    // Content
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 10);
    if (pauseAz && pauseEl) display.println("Both axes");
    else if (pauseAz) display.println("AZ axis");
    else display.println("EL axis");
    
    display.print(adsPresent ? String(displayAzimuth(currentAz), azElDisplayDecimals ? 1 : 0) + "d" : "N/A");
    display.print(" El: ");
    display.println(adsPresent ? String(currentEl, azElDisplayDecimals ? 1 : 0) + "d" : "N/A");
    
    // Dotted separator above footer
    for (int x = 0; x < 128; x += 3) {
      display.drawPixel(x, 53, SSD1306_WHITE);
    }
    display.setCursor(0, 56);
    display.println("1.Cycle 2!Go");
    
  } else {
    // No title bar - use all space for values
    display.setTextColor(SSD1306_WHITE);
    
    // Compute both values first
    String azVal, elVal;
    if (adsPresent) {
      float azRaw = displayAzimuth(currentAz);
      float elRaw = currentEl;
      // When ELF mode is on, show effective satellite coords
      azRaw = elfAz(azRaw);
      elRaw = elfEl(elRaw);
      int dec = azElDisplayDecimals ? 1 : 0;
      int padTo = azElDisplayDecimals ? 6 : 4;
      azVal = String(azRaw, dec);
      elVal = String(elRaw, dec);
      // Left-pad so values right-align (max "-180.0" with decimals, "-180" without)
      while (azVal.length() < padTo) azVal = " " + azVal;
      while (elVal.length() < padTo) elVal = " " + elVal;
    }
    
    // Az line
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("Az:");
    if (adsPresent) {
      display.print(azVal);
      // Movement indicator — draw arrow near right edge
      int ax = 114;  // Near right edge (128px - 9px arrow - 2px margin)
      if (azDirection == DIR_CW || (currentMove.compareTo("AZ POT") == 0 && calDirection > 0)) {
        // Right arrow: → (9 wide, 12 tall)
        // Shaft x=0..3, arrowhead x=4..8 (triangular)
        display.drawPixel(ax, 5, SSD1306_WHITE);
        display.drawPixel(ax, 6, SSD1306_WHITE);
        display.drawPixel(ax+1, 5, SSD1306_WHITE);
        display.drawPixel(ax+1, 6, SSD1306_WHITE);
        display.drawPixel(ax+2, 5, SSD1306_WHITE);
        display.drawPixel(ax+2, 6, SSD1306_WHITE);
        display.drawPixel(ax+3, 5, SSD1306_WHITE);
        display.drawPixel(ax+3, 6, SSD1306_WHITE);
        display.drawPixel(ax+4, 1, SSD1306_WHITE);
        display.drawPixel(ax+4, 10, SSD1306_WHITE);
        display.drawPixel(ax+5, 2, SSD1306_WHITE);
        display.drawPixel(ax+5, 9, SSD1306_WHITE);
        display.drawPixel(ax+6, 3, SSD1306_WHITE);
        display.drawPixel(ax+6, 8, SSD1306_WHITE);
        display.drawPixel(ax+7, 4, SSD1306_WHITE);
        display.drawPixel(ax+7, 7, SSD1306_WHITE);
        display.drawPixel(ax+8, 5, SSD1306_WHITE);
        display.drawPixel(ax+8, 6, SSD1306_WHITE);
      } else if (azDirection == DIR_CCW || (currentMove.compareTo("AZ POT") == 0 && calDirection < 0)) {
        // Left arrow: ← (9 wide, 12 tall)
        // Arrowhead x=0..4 (triangular), shaft x=5..8
        display.drawPixel(ax, 5, SSD1306_WHITE);
        display.drawPixel(ax, 6, SSD1306_WHITE);
        display.drawPixel(ax+1, 4, SSD1306_WHITE);
        display.drawPixel(ax+1, 7, SSD1306_WHITE);
        display.drawPixel(ax+2, 3, SSD1306_WHITE);
        display.drawPixel(ax+2, 8, SSD1306_WHITE);
        display.drawPixel(ax+3, 2, SSD1306_WHITE);
        display.drawPixel(ax+3, 9, SSD1306_WHITE);
        display.drawPixel(ax+4, 1, SSD1306_WHITE);
        display.drawPixel(ax+4, 10, SSD1306_WHITE);
        display.drawPixel(ax+5, 5, SSD1306_WHITE);
        display.drawPixel(ax+5, 6, SSD1306_WHITE);
        display.drawPixel(ax+6, 5, SSD1306_WHITE);
        display.drawPixel(ax+6, 6, SSD1306_WHITE);
        display.drawPixel(ax+7, 5, SSD1306_WHITE);
        display.drawPixel(ax+7, 6, SSD1306_WHITE);
        display.drawPixel(ax+8, 5, SSD1306_WHITE);
        display.drawPixel(ax+8, 6, SSD1306_WHITE);
      }
    } else {
      display.print("  N/A");
    }
    
    // El line
    display.setCursor(0, 18);
    display.print("El:");
    if (adsPresent) {
      display.print(elVal);
      // Movement indicator — draw arrow near right edge
      int ex = 114;  // Near right edge (128px - 9px arrow - 2px margin)
      int ey = 20;
      // When ELF is on, swap arrow direction (physical UP = effective DOWN)
      bool elPhysMovingUp = (elDirection == DIR_UP || (currentMove.compareTo("EL POT") == 0 && calDirection > 0));
      bool elPhysMovingDown = (elDirection == DIR_DOWN || (currentMove.compareTo("EL POT") == 0 && calDirection < 0));
      bool elMovingUp = elPhysMovingUp;
      bool elMovingDown = elPhysMovingDown;
      if (elfMode) {
        // Swap: physical UP shows as effective DOWN and vice versa
        elMovingUp = elPhysMovingDown;
        elMovingDown = elPhysMovingUp;
      }
      
      if (elMovingUp) {
        // Up arrow: ↑ (9 wide, 12 tall)
        // Arrowhead y=0..4 (triangular), shaft y=5..8
        display.drawPixel(ex+4, ey+0, SSD1306_WHITE);
        display.drawPixel(ex+3, ey+1, SSD1306_WHITE);
        display.drawPixel(ex+5, ey+1, SSD1306_WHITE);
        display.drawPixel(ex+2, ey+2, SSD1306_WHITE);
        display.drawPixel(ex+6, ey+2, SSD1306_WHITE);
        display.drawPixel(ex+1, ey+3, SSD1306_WHITE);
        display.drawPixel(ex+7, ey+3, SSD1306_WHITE);
        display.drawPixel(ex+0, ey+4, SSD1306_WHITE);
        display.drawPixel(ex+8, ey+4, SSD1306_WHITE);
        display.drawPixel(ex+4, ey+5, SSD1306_WHITE);
        display.drawPixel(ex+4, ey+6, SSD1306_WHITE);
        display.drawPixel(ex+4, ey+7, SSD1306_WHITE);
        display.drawPixel(ex+4, ey+8, SSD1306_WHITE);
      } else if (elMovingDown) {
        // Down arrow: ↓ (9 wide, 12 tall)
        // Shaft y=0..4, arrowhead y=5..8 (triangular)
        display.drawPixel(ex+4, ey+0, SSD1306_WHITE);
        display.drawPixel(ex+4, ey+1, SSD1306_WHITE);
        display.drawPixel(ex+4, ey+2, SSD1306_WHITE);
        display.drawPixel(ex+4, ey+3, SSD1306_WHITE);
        display.drawPixel(ex+4, ey+4, SSD1306_WHITE);
        display.drawPixel(ex+0, ey+5, SSD1306_WHITE);
        display.drawPixel(ex+8, ey+5, SSD1306_WHITE);
        display.drawPixel(ex+1, ey+6, SSD1306_WHITE);
        display.drawPixel(ex+7, ey+6, SSD1306_WHITE);
        display.drawPixel(ex+2, ey+7, SSD1306_WHITE);
        display.drawPixel(ex+6, ey+7, SSD1306_WHITE);
        display.drawPixel(ex+3, ey+8, SSD1306_WHITE);
        display.drawPixel(ex+5, ey+8, SSD1306_WHITE);
        display.drawPixel(ex+4, ey+9, SSD1306_WHITE);
      }
    } else {
      display.print("  N/A");
    }
    
    // Check if we have a recent command to display (RX/TX mode)
    // Suppress overlay for 5s after any button press so menu stays visible
    bool suppressed = (oledCmdSuppressUntil > 0 && millis() < oledCmdSuppressUntil);
    bool showCmd = (protocolShowCommands && strlen(protocolLastCommand) > 0 && !suppressed);
    // Transient protocol error warning overrides the info strip
    bool showWarn = (oledWarnUntil > millis() && strlen(oledWarnText) > 0);
    
    if (showWarn) {
      // Protocol error banner (auto-clears after 5s)
      display.setTextSize(1);
      for (int x = 0; x < 128; x += 3) {
        display.drawPixel(x, 38, SSD1306_WHITE);
      }
      display.setCursor(0, 40);
      display.println(oledWarnText);
      for (int x = 0; x < 128; x += 3) {
        display.drawPixel(x, 53, SSD1306_WHITE);
      }
    } else if (showCmd) {
      // Switch to small font for RX/TX lines
      display.setTextSize(1);
      // Dotted separator below Az/El with breathing room
      for (int x = 0; x < 128; x += 3) {
        display.drawPixel(x, 38, SSD1306_WHITE);
      }
      // Tag: first letter = source (S=serial, N=network), second = direction
      // (R=received, T=transmitted). Replies travel back over the command's
      // own interface, so both lines carry the same source letter.
      const char* srcLetter = (protocolRxSource == RX_SRC_SER) ? "S" : "N";
      // Flatten multi-line text ('^' for CR/LF) and hard-truncate to fit the
      // 17-char payload budget (21 chars per line minus the 4-char tag)
      char rxLine[PROTOCOL_BUF_SIZE];
      char txLine[PROTOCOL_BUF_SIZE];
      oledFlattenLine(protocolLastCommand, rxLine, 17);
      oledFlattenLine(protocolLastResponse, txLine, 17);
      display.setCursor(0, 40);
      display.print(srcLetter);
      display.print("R: ");
      display.print(rxLine);
      display.setCursor(0, 51);
      display.print(srcLetter);
      display.print("T: ");
      display.print(txLine);
    } else if (currentMove.compareTo("IDLE") != 0) {
      display.setTextSize(1);
      display.setCursor(0, 40);
      display.print("Moving: ");
      // When ELF is on, show effective direction (swap UP/DOWN)
      String moveDir = currentMove;
      if (elfMode) {
        if (moveDir == "UP") moveDir = "DOWN";
        else if (moveDir == "DOWN") moveDir = "UP";
      }
      display.println(moveDir);
      for (int x = 0; x < 128; x += 3) {
        display.drawPixel(x, 53, SSD1306_WHITE);
      }
    } else {
      display.setTextSize(1);
      display.setCursor(0, 40);
      display.print("Sel: ");
      display.print(directions[rotatorMenuSelection]);
      
      // Show ELF indicator right-aligned when enabled
      if (elfMode) {
        // One space gap before "ELF" to avoid TCP dot at x=125
        int elfWidth = 18; // "ELF" in size 1 = 3 * 6 = 18px
        display.setCursor(128 - elfWidth - 1, 40);
        display.print("ELF");
      }
      display.println();
      for (int x = 0; x < 128; x += 3) {
        display.drawPixel(x, 53, SSD1306_WHITE);
      }
    }
    
    // Show button actions footer only when NOT in PC interface mode
    if (!showCmd) {
      display.setCursor(0, 56);
      if (rotatorMenuSelection == 0) {
        display.println("1.Cycle 2!Go 1+2!|RST");
      } else if (rotatorMenuSelection == 5) {
        // ELF mode toggle
        if (elfMode) {
          display.println("1.Cycle 2!Off");
        } else {
          display.println("1.Cycle 2!On");
        }
      } else {
        display.println("1.Cycle 2!Go");
      }
    }
  }
  
  display.display();
}

// ====================== BUTTON HANDLING ======================
// Button state tracking
bool btn1WasPressed = false;
bool btn2WasPressed = false;
unsigned long btn1PressStart = 0;
unsigned long btn2PressStart = 0;
bool btn1LongPressTriggered = false;
bool skipBtnProcessing = false;  // Skip one cycle of button processing after menu transition
bool btn2LongPressTriggered = false;
unsigned long btn1LastPress = 0;
// Track physical button state for web UI
bool btn2Physical = false;
// Flag: came from rotator control (AzEl screen) → Back should go there
bool cameFromRotatorControl = false;

void handleButtons() {
  bool btn1 = digitalRead(BTN1);      // HIGH = pressed
  btn2Physical = !digitalRead(BTN2);  // LOW = pressed
  bool btn2 = btn2Physical;
  
  unsigned long now = millis();
  
  // ============================================
  // MENU MODE BUTTON HANDLING
  // ============================================
  if (currentMenu) {
    // Skip one cycle after menu transition to avoid seeing held-down button
    if (skipBtnProcessing) {
      skipBtnProcessing = false;
    } else {
    // Btn1: short-press = navigate, long-press = select
    if (btn1 && !btn1WasPressed) {
      resetOledCmdSuppress();
      btn1PressStart = now;
      btn1LongPressTriggered = false;
    }
    
    // Long press detected
    if (btn1 && (now - btn1PressStart >= BTN_LONG_MS) && !btn1LongPressTriggered) {
      btn1LongPressTriggered = true;
      skipBtnProcessing = true;  // Skip button processing on next cycle
      currentMenu->select();
      return;  // Exit immediately so new screen sees fresh button state
    }
    
    // Short press released (navigates)
    if (!btn1 && btn1WasPressed && !btn1LongPressTriggered && (now - btn1PressStart < BTN_LONG_MS)) {
      if (now - btn1LastPress > BTN_DEBOUNCE_MS) {
        currentMenu->navigate();
        btn1LastPress = now;
      }
    }
    
    btn1WasPressed = btn1;
    
    // Btn2: back to parent menu
    if (btn2 && !btn2WasPressed) {
      resetOledCmdSuppress();
      if (now - btn2PressStart > BTN_DEBOUNCE_MS || btn2PressStart == 0) {
        btn2PressStart = now;
        skipBtnProcessing = true;  // Skip next cycle so held Btn2 doesn't trigger again
        // Disable Back on SETUP menu when mandatory items incomplete
        if (currentMenu == &setupMenu && !allSetupComplete()) {
          // Do nothing — Back is disabled until all mandatory setup is done
        } else if (cameFromRotatorControl && currentMenu == &setupMenu) {
          // Came from AzEl → go back there directly
          cameFromRotatorControl = false;
          currentMenu = nullptr;  // Rotator control mode
        } else {
          // When returning from calPotsMenu to setupMenu, update labels and timers
          if (currentMenu == &calPotsMenu && currentMenu->getParent() == &setupMenu) {
            updateSetupLabels();
            updateSetupTimers();
          }
          currentMenu->back();
        }
        return;  // Exit immediately so new menu sees fresh button state
      }
    }
    btn2WasPressed = btn2;
    }  // End of skipBtnProcessing else block

    return;  // Exit - menu mode handles its own buttons
  }
  
  // ============================================
  // CALIBRATION MODE BUTTON HANDLING
  // ============================================
  if (calState != CAL_IDLE) {
    unsigned long now = millis();
    
    // Btn1: exit calibration, retry on error, continue adjust
    if (btn1 && !btn1WasPressed && (now - btn1LastPress > BTN_DEBOUNCE_MS)) {
      if (calState == CAL_ERROR) {
        // Retry calibration
        calState = CAL_SWEEP_MIN;
        calPhaseStart = millis();
        calVSampleIdx = 0;
        calDirection = 0;
        calMinVoltage = 0;
        calMaxVoltage = 0;
        calStabilityMax = 0;
        calStabilityMin = 32767;
        calErrorMessage = "";
      } else if (calState == CAL_ADJUST_MAX) {
        // Save calibration with adjusted max voltage, then proceed
        saveCalibration();
        
        if (calCurrentAxis == 0) {
          // AZ: start return sweep
          calState = CAL_RETURN_MIN;
          calPhaseStart = millis();
          // N450/S450 start at index 0 (W=+270), N360/S360 start at index 2 (S=+180)
          calLastHeadingIndex = (rotModel == ROT_N450 || rotModel == ROT_S450) ? 0 : 2;
          calCurrentHeading = "";
          calHeadingPauseStart = 0;
          calVSampleIdx = 0;
          Serial.println("CAL: Starting return sweep to minimum");
        } else {
          // EL: start return sweep
          calState = CAL_RETURN_MIN;
          calPhaseStart = millis();
          calLastHeadingIndex = 0;
          calCurrentHeading = "";
          calHeadingPauseStart = 0;
          calVSampleIdx = 0;
          Serial.println("CAL: Starting EL return sweep to minimum");
        }
      } else if (calState == CAL_COMPLETE || calState == CAL_SWEEP_MIN || calState == CAL_SWEEP_MAX || calState == CAL_RETURN_MIN) {
        // Exit calibration
        exitCalibrationMode();
        currentMenu = &calPotsMenu;
        currentMenu->init();
        btn1LastPress = now;
        btn1WasPressed = btn1;
        btn2WasPressed = btn2;
        return;
      }
      btn1LastPress = now;
      btn1WasPressed = btn1;
      btn2WasPressed = btn2;
      return;
    }
    btn1WasPressed = btn1;
    
    // Btn2: stop movement, exit in terminal states
    if (btn2 && !btn2WasPressed) {
      resetOledCmdSuppress();
      btn2PressStart = now;
      btn2LongPressTriggered = false;
      
      if (calState == CAL_COMPLETE || calState == CAL_ERROR) {
        // Exit calibration in terminal states
        exitCalibrationMode();
        currentMenu = &calPotsMenu;
        currentMenu->init();
        btn2WasPressed = btn2;
        return;
      }
      
      if (calState == CAL_SWEEP_MIN || calState == CAL_SWEEP_MAX || calState == CAL_RETURN_MIN) {
        // Stop movement during sweep
        stopAll();
        calDirection = 0;
      }
      
      if (calState == CAL_ADJUST_MAX) {
        // Cancel calibration
        exitCalibrationMode();
        currentMenu = &calPotsMenu;
        currentMenu->init();
        btn2WasPressed = btn2;
        return;
      }
    }
    
    if (!btn2) {
      btn2PressStart = 0;
    }
    btn2WasPressed = btn2;
    
    return;  // Exit - calibration mode handles its own buttons
  }
  
  // ============================================
  // ROTATOR CONTROL MODE BUTTON HANDLING
  // ============================================
  
  // Btn1: cycle through directions (0-4: IDLE, LEFT, RIGHT, UP, DOWN)
  // ALSO: restart sequence - when IDLE selected, hold Btn2 + long-press Btn1
  if (btn1 && !btn1WasPressed) {
    resetOledCmdSuppress();
    // Any button press clears protocol command display (user is back in control)
    // Only clear if the overlay would actually be shown (not suppressed by timer)
    bool suppressed = (oledCmdSuppressUntil > 0 && now < oledCmdSuppressUntil);
    if (strlen(protocolLastCommand) > 0 && !suppressed) {
      protocolLastCommand[0] = '\0';
      protocolLastResponse[0] = '\0';
      // Reset movement state if motors have stopped
      if (azDirection == DIR_STOPPED && elDirection == DIR_STOPPED) {
        stopAll();
      }
      // Force immediate display update so user sees button mode right away
      updateRotatorDisplay();
      // Skip cycling on this press - user just wants to exit PC mode
      btn1WasPressed = btn1;
      return;
    }
    btn1PressStart = now;
    btn1LongPressTriggered = false;
  }
  
  // Long press Btn1 while Btn2 held + SETUP selected = reboot (handled in Btn2 section)
  if (btn1 && (now - btn1PressStart >= BTN_LONG_MS) && !btn1LongPressTriggered) {
    btn1LongPressTriggered = true;
  }
  
  // Short press Btn1 = cycle directions (only if not long-pressed)
  if (!btn1 && btn1WasPressed && !btn1LongPressTriggered && (now - btn1PressStart < BTN_LONG_MS)) {
    if (now - btn1LastPress > BTN_DEBOUNCE_MS) {
      rotatorMenuSelection = (rotatorMenuSelection + 1) % 6;
      btn1LastPress = now;
      // Serial.print("Menu: ");
      // Serial.println(directions[rotatorMenuSelection]);
    }
  }
  btn1WasPressed = btn1;
  
  // Btn2: press = start, release = stop (momentary)
  // Long-press Btn2 (1s+) = go to Setup menu
  if (btn2 && !btn2WasPressed) {
    resetOledCmdSuppress();
    btn2PressStart = now;
    btn2LongPressTriggered = false;
    // Button just pressed - start movement
    if (!isPausing) {
      if (rotatorMenuSelection == 1) startMove("LEFT");
      else if (rotatorMenuSelection == 2) startMove("RIGHT");
      // When ELF is on, UP/DOWN are swapped because Real El = 180 - Sat El
      // Physical UP decreases satellite El, and vice versa
      else if (rotatorMenuSelection == 3) {
        elfMode ? startMove("DOWN") : startMove("UP");
      }
      else if (rotatorMenuSelection == 4) {
        elfMode ? startMove("UP") : startMove("DOWN");
      }
    }
  }
  
  // Long-press Btn2 on ELF = toggle ELF mode and show elevation prompt
  if (btn2 && rotatorMenuSelection == 5 && (now - btn2PressStart >= 800) && !btn2LongPressTriggered) {
    btn2LongPressTriggered = true;
    elfMode = !elfMode;
    saveElfMode();
    // Serial.print("ELF: Mode toggled to ");
    // Serial.println(elfMode ? "ON" : "OFF");
    // Show elevation prompt (blocking — returns after selection)
    showElfElevPrompt();
  }

  // Long-press Btn2 on SETUP = go to Setup menu or reboot
  // For direction selections, Btn2 is momentary (press=start, release=stop) - no long-press action
  if (btn2 && rotatorMenuSelection == 0 && (now - btn2PressStart >= 1000) && !btn2LongPressTriggered) {
    btn2LongPressTriggered = true;
    
    // If SETUP selected + Btn1 held = reboot
    if (btn1) {
      stopAll();
      // Serial.println("RESTART: Btn1+Btn2 long-press - restarting");
      Menu::drawQuickNotice("Restarting", "...", "", true);
      delay(1000);
      ESP.restart();
    }
    
    // Normal: go to Setup
    stopAll();
    // Serial.println("ROT: Long-press Btn2 - going to Setup");
    updateSetupLabels();
    updateSetupTimers();
    cameFromRotatorControl = true;  // Remember we came from AzEl
    currentMenu = &setupMenu;
    currentMenu->init();
    skipBtnProcessing = true;
    return;
  }
  
  if (!btn2 && btn2WasPressed && !btn2LongPressTriggered) {
    // Button just released (short press) - stop movement
    stopAll();
  }
  
  if (!btn2) {
    btn2PressStart = 0;
  }
  
  btn2WasPressed = btn2;
}

// ====================== WEB SERVER ======================
// SatNOGS proxy: persistent client to avoid memory leaks
static WiFiClientSecure satNOGSClient;
static bool satNOGSClientInit = false;

void handleSatNOGSProxy() {
  String urlParam = server.arg("url");
  if (urlParam.isEmpty()) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(400, "application/json", "{\"error\":\"Missing 'url' parameter\"}");
    return;
  }

  String targetUrl = "https://db.satnogs.org/api" + urlParam;

  if (!satNOGSClientInit) {
    satNOGSClient.setInsecure();
    satNOGSClient.setBufferSizes(1024, 1024);
    satNOGSClientInit = true;
  }
  satNOGSClient.stop();

  HTTPClient http;
  http.begin(satNOGSClient, targetUrl);
  http.setTimeout(30000);

  int httpCode = http.GET();
  if (httpCode <= 0) {
    String error = http.errorToString(httpCode);
    http.end();
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(502, "application/json", "{\"error\":\"Request failed: " + error + "\"}");
    return;
  }

  int contentLength = http.getSize();
  Serial.print("[SATPROXY] Status: ");
  Serial.print(httpCode);
  Serial.print(", Size: ");
  Serial.println(contentLength);

  // Limit to small responses - ESP8266 has very limited RAM
  if (contentLength > 4096) {
    http.end();
    Serial.print("[SATPROXY] Response too large: ");
    Serial.println(contentLength);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(503, "application/json", "{\"error\":\"Response too large for ESP proxy. Use Direct or Custom proxy mode.\"}");
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  stream->setTimeout(30000);
  String body = stream->readString();
  http.end();

  Serial.print("[SATPROXY] Read ");
  Serial.print(body.length());
  Serial.println(" bytes");

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(httpCode, "application/json", body);
}

String webLastMove = "";
unsigned long webMoveTime = 0;

void setupWebServer() {
  // ABSOLUTE guard: ESP8266WebServer's server.on() APPENDS handlers to a linked
  // list and there is no API to remove them, so routes must be registered
  // exactly once per boot. Re-running this (e.g. after a captive-portal stop)
  // used to duplicate every handler - each request then re-ran the first
  // handler plus leaked N-1 unused nodes per cycle (unbounded RAM growth while
  // WiFi flaps AP<->STA). Mode switching (captive portal vs normal) is done
  // via runtime flags (captivePortalActive), not by swapping routes.
  static bool webServerStarted = false;
  if (webServerStarted) return;
  webServerStarted = true;
  
  // Disable keep-alive — Chrome opens speculative parallel connections that
  // overwhelm the ESP8266's lwIP stack, causing ERR_CONNECTION_RESET
  server.keepAlive(false);
  
  server.on("/", []() {
    sendHtml();
  });
  
  // Portal WiFi-save endpoint - registered once for ALL modes. Harmless when
  // the portal is inactive; only the captive-portal page POSTs to it.
  server.on("/save", handleSaveWifi);
  
  // Serve favicon from SPIFFS (catches automatic browser /favicon.ico request)
  server.on("/favicon.ico", []() {
    serveSpiffsFile("/files/favicon.ico");
  });
  
  server.on("/upload", HTTP_POST, []() {
    // POST response is sent from handleUpload() callback
  }, handleUpload);
  
  // Serve world coastline data from SPIFFS
  server.on("/world_coastline.js", []() {
    sendCoastlineData();
  });
  
  // Serve files from SPIFFS
  server.on("/files", HTTP_GET, []() {
    serveSpiffsFile(server.uri());
  });
  
  // List SPIFFS files
  server.on("/list-files", HTTP_GET, []() {
    listSpiffsFiles();
  });
  
  // Delete SPIFFS file
  server.on("/delete-file", HTTP_GET, []() {
    deleteSpiffsFile();
  });
  
  server.onNotFound([]() {
    // Captive portal active: every unknown URL gets the portal page (phones
    // probe hijacked DNS names like connect.apple.com - this is what shows
    // them the setup UI). Flag-based dispatch keeps the route single.
    if (captivePortalActive) {
      handlePortal();
      return;
    }
    // If the request starts with /files/, serve from SPIFFS
    if (server.uri().startsWith("/files/")) {
      serveSpiffsFile(server.uri());
      return;
    }
    // For any other unknown request, send a minimal redirect to main page
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  });
  
  server.on("/cmd", []() {
    String dir = server.arg("dir");
    Serial.print("Web command: ");
    Serial.println(dir);
    
    if (dir == "LEFT" || dir == "RIGHT" || dir == "UP" || dir == "DOWN") {
      startMove(dir);
      webLastMove = dir;
      webMoveTime = millis();
    } else if (dir == "STOP") {
      stopAll();
      webLastMove = "";
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/status", []() {
    String json = "{";
    json += "\"az\":" + String(displayAzimuth(currentAz), 1) + ",";
    json += "\"el\":" + String(currentEl, 1) + ",";
    json += "\"move\":\"" + currentMove + "\",";
    json += "\"pause\":" + String(isPausing ? "true" : "false");
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  // GET /diag — raw position pipeline state (field troubleshooting)
  // Exposes each stage: ADC detected? raw voltages? calibration valid?
  // final currentAz/currentEl. Lets you see WHERE the position chain
  // breaks without serial access.
  server.on("/diag", []() {
    String j = "{";
    j += "\"ads\":" + String(adsPresent) + ",";
    j += "\"azV\":" + String(azVoltage, 3) + ",";
    j += "\"elV\":" + String(elVoltage, 3) + ",";
    j += "\"calAz\":" + String((int)calAzValid) + ",";
    j += "\"azMin\":" + String(calAzMinV, 3) + ",";
    j += "\"azMax\":" + String(calAzMaxV, 3) + ",";
    j += "\"calEl\":" + String((int)calElValid) + ",";
    j += "\"elMin\":" + String(calElMinV, 3) + ",";
    j += "\"elMax\":" + String(calElMaxV, 3) + ",";
    j += "\"az\":" + String(currentAz, 2) + ",";
    j += "\"el\":" + String(currentEl, 2) + ",";
    j += "\"elf\":" + String((int)elfMode) + ",";
    j += "\"up\":" + String(millis() / 1000);
    j += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", j);
  });
  
  server.on("/btn", []() {
    String json = "{";
    json += "\"btn2\":" + String(btn2Physical ? "true" : "false") + ",";
    json += "\"move\":\"" + currentMove + "\",";
    json += "\"webMove\":\"" + webLastMove + "\"";
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  
  // ==================== PROTOCOL DEBUG LOG ====================
  // GET /debug-usb — live debug viewer page (opens in its own browser tab)
  server.on("/debug-usb", []() {
    server.keepAlive(false);
    sendProgmemHTML(html_debug_usb);
  });

  // GET /debug-log?since=N — new log lines since seq N (JSON). Any poll arms
  // capture; capture auto-disarms ~5 s after the last poll (tab closed).
  server.on("/debug-log", []() {
    unsigned long now = millis();
    if (!dbgLogArmed || now - dbgLogLastPollMs > DBGLOG_IDLE_DISARM_MS) {
      dbgLogArm(true);   // fresh capture session
    } else {
      dbgLogArm(false);  // keep session, refresh idle timer
    }
    int since = -1;
    if (server.hasArg("since")) since = server.arg("since").toInt();
    uint32_t oldest = (dbgLogSeq > DBGLOG_MAX_ENTRIES) ? (dbgLogSeq - DBGLOG_MAX_ENTRIES + 1) : 0;
    uint32_t start = (since < 0) ? oldest : (((uint32_t)since + 1 > oldest) ? (uint32_t)since + 1 : oldest);
    String json = "{\"armed\":true,\"seq\":" + String(dbgLogSeq) + ",\"time\":";
    {
      char devTime[24];
      json += (formatDeviceTime(devTime, sizeof(devTime))) ? ("\"" + String(devTime) + "\"") : "null";
    }
    json += ",\"lines\":[";
    bool first = true;
    int n = 0;
    // Scan written slots only: slot dbgLogSeq is the NEXT write target and
    // after a ring wrap it still holds the 96-entries-old entry (stale).
    for (uint32_t s = start; s < dbgLogSeq && n < 64; s++) {
      DbgLogEntry& e = dbgLogBuf[s % DBGLOG_MAX_ENTRIES];
      if (e.len == 0) continue;  // unwritten slot
      const char* d = (e.dir == 'R') ? "RX" : "TX";
      const char* sr = (e.src == 'S') ? "SER" : (e.src == 'T') ? "TCP" : (e.src == 'D') ? "DROP" : "CON";
      char tsbuf[40];
      formatLogPrefix(e.t, tsbuf, sizeof(tsbuf));
      char line[DBGLOG_MSG_MAX + 56];
      snprintf(line, sizeof(line), "%s%s %s %s", tsbuf, d, sr, e.msg);
      char jline[DBGLOG_MSG_MAX + 112];
      dbgLogJsonEscape(line, jline, sizeof(jline));
      if (!first) json += ",";
      json += "\"";
      json += jline;
      json += "\"";
      first = false;
      n++;
    }
    json += "]}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  // GET /set-time?ts=<epoch-ms>&off=<utc-offset-seconds> — one-click wall-clock
  // sync from the browser (the "Sync time" button on /debug-usb). Works in AP
  // mode where NTP cannot reach the device. Persists the display offset.
  server.on("/set-time", []() {
    String json = "{\"ok\":false}";
    if (server.hasArg("ts") && server.hasArg("off")) {
      uint64_t tsMs = strtoull(server.arg("ts").c_str(), nullptr, 10);
      int32_t off = server.arg("off").toInt();
      if (tsMs > 1000000000000ULL) {  // after year 2001
        timeOffsetSec = off;
        saveTimeOffset();
        setTimeRef((uint32_t)(tsMs / 1000), "SYNC");
        char devTime[24];
        formatDeviceTime(devTime, sizeof(devTime));
        json = "{\"ok\":true,\"time\":\"" + String(devTime) + "\"}";
        Serial.println("TIME: Synced from browser");
        dbgLogCapture("TX", "CON", "TIME: Synced from browser");
      }
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  // GET /debug-log-download — full ring buffer as a downloadable .log file
  server.on("/debug-log-download", []() {
    dbgLogArm(false);  // refresh idle timer without resetting the buffer
    const char* protoName = (currentProtocol == PROTO_GS232A) ? "GS-232A" :
                            (currentProtocol == PROTO_GS232B) ? "GS-232B" : "Easycom";
    String header = "AZELCtrl Protocol Debug Log\n";
    header += "Firmware: " + String(FIRMWARE_VERSION) + "\n";
    header += "Protocol: " + String(protoName) + " @ " + String(PROTOCOL_BAUD_RATES[protocolBaudIndex]) + " baud\n";
    header += "Hamlib TCP server (port 4533): " + String(hamlibServerEnabled ? "enabled" : "disabled") + "\n";
    header += "Entries: " + String(dbgLogSeq) + ((dbgLogSeq > DBGLOG_MAX_ENTRIES) ? " (ring wrapped - oldest dropped)" : "") + "\n";
    {
      char devTime[24];
      if (timeRefValid()) {
        char tzbuf[8];
        snprintf(tzbuf, sizeof(tzbuf), "%c%02d:%02d",
                 timeOffsetSec >= 0 ? '+' : '-',
                 abs(timeOffsetSec / 3600), abs((timeOffsetSec % 3600) / 60));
        header += "Timestamps: [ms since boot] YYYY-MM-DD HH:MM:SS.mmm (local, UTC"
                  + String(tzbuf) + ")\n";
        if (formatDeviceTime(devTime, sizeof(devTime))) {
          header += String("Time source: ") + String(timeSource) + " (now " + String(devTime) + ")\n";
        }
      } else {
        header += "Timestamps: milliseconds since device boot (no RTC)\n";
        header += "Time: not set - open the debug tab and click \"Sync time\"\n";
      }
    }
    header += "---\n";
    // Stream in chunks (setContentLength + sendContent) — no large heap blob
    uint32_t oldest = (dbgLogSeq > DBGLOG_MAX_ENTRIES) ? (dbgLogSeq - DBGLOG_MAX_ENTRIES + 1) : 0;
    size_t total = header.length();
    char tsbuf[40];
    char line[DBGLOG_MSG_MAX + 56];
    for (uint32_t s = oldest; s < dbgLogSeq; s++) {
      DbgLogEntry& e = dbgLogBuf[s % DBGLOG_MAX_ENTRIES];
      if (e.len == 0) continue;
      const char* d = (e.dir == 'R') ? "RX" : "TX";
      const char* sr = (e.src == 'S') ? "SER" : (e.src == 'T') ? "TCP" : (e.src == 'D') ? "DROP" : "CON";
      formatLogPrefix(e.t, tsbuf, sizeof(tsbuf));
      total += (size_t)snprintf(line, sizeof(line), "%s%s %s %s\n", tsbuf, d, sr, e.msg);
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"azelctrl-debug-usb.log\"");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.setContentLength(total);
    server.send(200, "text/plain");
    server.sendContent(header);
    for (uint32_t s = oldest; s < dbgLogSeq; s++) {
      DbgLogEntry& e = dbgLogBuf[s % DBGLOG_MAX_ENTRIES];
      if (e.len == 0) continue;
      const char* d = (e.dir == 'R') ? "RX" : "TX";
      const char* sr = (e.src == 'S') ? "SER" : (e.src == 'T') ? "TCP" : (e.src == 'D') ? "DROP" : "CON";
      formatLogPrefix(e.t, tsbuf, sizeof(tsbuf));
      snprintf(line, sizeof(line), "%s%s %s %s\n", tsbuf, d, sr, e.msg);
      server.sendContent(line);
    }
  });

  // ==================== SATELLITE TRACKING API ====================
  
  // GET /getpos — current position + movement directions
  // Response: {"az":181.9,"el":44.8,"maz":"cw","mel":"up"}
  // maz: "cw"|"ccw"|"idle", mel: "up"|"down"|"idle"
  server.on("/getpos", []() {
    // Report effective (satellite-space) coords when ELF is on
    float az = displayAzimuth(currentAz);
    float el = currentEl;
    az = elfAz(az);
    el = elfEl(el);
    
    // Movement direction strings
    const char* maz;
    switch (azDirection) {
      case DIR_CW:    maz = "cw"; break;
      case DIR_CCW:   maz = "ccw"; break;
      default:        maz = "idle"; break;
    }
    const char* mel;
    switch (elDirection) {
      case DIR_UP:    mel = "up"; break;
      case DIR_DOWN:  mel = "down"; break;
      default:        mel = "idle"; break;
    }
    
    String json = "{";
    json += "\"az\":" + String(az, 1) + ",";
    json += "\"el\":" + String(el, 1) + ",";
    json += "\"maz\":\"" + String(maz) + "\",";
    json += "\"mel\":\"" + String(mel) + "\"";
    json += "}";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  
  // OPTIONS /setpos — CORS preflight
  server.on("/setpos", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  
  // POST /setpos — set target position
  // Body: {"az":182.4,"el":45.1}
  // Incoming targets are in satellite space (ELF handles transformation)
  server.on("/setpos", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(400, "application/json", "{\"error\":\"no body\"}");
      return;
    }
    
    String body = server.arg("plain");
    
    // Simple JSON parsing without library: find "az": and "el": values
    float targetAz = -1, targetEl = -1;
    bool hasAz = false, hasEl = false;
    
    int azIdx = body.indexOf("\"az\"");
    int elIdx = body.indexOf("\"el\"");
    
    if (azIdx >= 0) {
      int colon = body.indexOf(':', azIdx);
      if (colon >= 0) {
        int start = colon + 1;
        while (start < body.length() && (body[start] == ' ' || body[start] == '\t')) start++;
        int end = start;
        while (end < body.length() && (isdigit(body[end]) || body[end] == '.' || body[end] == '-' || body[end] == '+' || body[end] == 'e' || body[end] == 'E')) end++;
        if (end > start) {
          String numStr = body.substring(start, end);
          targetAz = numStr.toFloat();
          hasAz = true;
        }
      }
    }
    
    if (elIdx >= 0) {
      int colon = body.indexOf(':', elIdx);
      if (colon >= 0) {
        int start = colon + 1;
        while (start < body.length() && (body[start] == ' ' || body[start] == '\t')) start++;
        int end = start;
        while (end < body.length() && (isdigit(body[end]) || body[end] == '.' || body[end] == '-' || body[end] == '+' || body[end] == 'e' || body[end] == 'E')) end++;
        if (end > start) {
          String numStr = body.substring(start, end);
          targetEl = numStr.toFloat();
          hasEl = true;
        }
      }
    }
    
    if (!hasAz && !hasEl) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(400, "application/json", "{\"error\":\"missing az and el\"}");
      return;
    }
    
    // Validate ranges
    if (hasAz && (targetAz < 0 || targetAz > 360)) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(400, "application/json", "{\"error\":\"az out of range\"}");
      return;
    }
    if (hasEl && (targetEl < 0 || targetEl > 180)) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(400, "application/json", "{\"error\":\"el out of range\"}");
      return;
    }
    
    // Execute moves via existing protocol functions (handles ELF transformation)
    if (hasAz) executeProtocolAzMove(targetAz);
    if (hasEl) executeProtocolElMove(targetEl);
    
    Serial.print("Web /setpos: az="); Serial.print(hasAz ? String(targetAz, 1) : "unchanged");
    Serial.print(" el="); Serial.println(hasEl ? String(targetEl, 1) : "unchanged");
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true}");
  });
  
  // OPTIONS /elf — CORS preflight
  server.on("/elf", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  
  // GET /elf — current ELF mode state
  // Response: {"enabled":true}
  server.on("/elf", HTTP_GET, []() {
    String json = "{\"enabled\":" + String(elfMode ? "true" : "false") + "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  
  // POST /elf — set ELF mode
  // Body: {"enabled":true} or {"enabled":false}
  server.on("/elf", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(400, "application/json", "{\"error\":\"no body\"}");
      return;
    }
    
    String body = server.arg("plain");
    
    // Simple parse: look for "enabled":true or "enabled":false
    bool hasEnabled = false;
    bool enabledValue = false;
    
    int enIdx = body.indexOf("\"enabled\"");
    if (enIdx >= 0) {
      int colon = body.indexOf(':', enIdx);
      if (colon >= 0) {
        int start = colon + 1;
        while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t')) start++;
        if (body.startsWith("true", start)) {
          enabledValue = true;
          hasEnabled = true;
        } else if (body.startsWith("false", start)) {
          enabledValue = false;
          hasEnabled = true;
        }
      }
    }
    
    if (!hasEnabled) {
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(400, "application/json", "{\"error\":\"missing enabled\"}");
      return;
    }
    
    elfMode = enabledValue;
    saveElfMode();
    
    Serial.print("Web /elf: mode = ");
    Serial.println(elfMode ? "ON" : "OFF");
    
    String json = "{\"enabled\":" + String(elfMode ? "true" : "false") + "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  // GET /api/rotator — rotator model (hard stops are direction-dependent, defined in UI)
  server.on("/api/rotator", []() {
    const char* modelNames[4] = {"N360", "S360", "N450", "S450"};
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"model\":\"" + String(modelNames[rotModel]) + "\"}");
  });

  // SatNOGS proxy endpoint
  server.on("/satnogs-proxy", HTTP_GET, handleSatNOGSProxy);

  // ====================== CONFIGURATION API ENDPOINTS ======================
  // All endpoints return JSON with CORS headers for use by the web UI.

  // CORS preflight handler for all /api/ routes
  server.on("/api/settings", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });

  // GET /api/settings — return all current settings as JSON
  server.on("/api/settings", HTTP_GET, []() {
    const char* modelNames[4] = {"N360", "S360", "N450", "S450"};
    const char* protocolNames[3] = {"GS232A", "GS232B", "EASYCOM"};
    const char* wifiModeNames[3] = {"STA", "AP", "DISABLED"};
    const char* calStatus = (calAzValid && calElValid) ? "calibrated" : "uncalibrated";

    String json = "{";
    // Rotator
    json += "\"rotator\":{\"model\":\"" + String(modelNames[rotModel]) + "\"},";
    // Calibration
    json += "\"calibration\":{\"status\":\"" + String(calStatus) + "\",";
    json += "\"azMinV\":" + String(calAzMinV, 3) + ",\"azMaxV\":" + String(calAzMaxV, 3) + ",";
    json += "\"elMinV\":" + String(calElMinV, 3) + ",\"elMaxV\":" + String(calElMaxV, 3) + "},";
    // WiFi
    json += "\"wifi\":{\"mode\":\"" + String(wifiModeNames[deviceWifiMode]) + "\",";
    json += "\"ssid\":\"" + savedSsid + "\",";
    json += "\"password\":\"" + maskPassword(savedPass) + "\",";
    json += "\"apSsid\":\"" + String(apSsid) + "\",";
    json += "\"apPassword\":\"" + String(apPassword) + "\",";
    json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + "},";
    // Protocol
    json += "\"protocol\":{\"variant\":\"" + String(protocolNames[currentProtocol]) + "\",";
    json += "\"baud\":" + String(getProtocolBaud()) + ",";
    json += "\"baudIndex\":" + String(protocolBaudIndex) + ",";
    json += "\"showCommands\":" + String(protocolShowCommands ? "true" : "false") + ",";
    json += "\"reverseDelay\":" + String(protocolReverseDelay) + "},";
    // Network
    json += "\"network\":{\"hamlibEnabled\":" + String(hamlibServerEnabled ? "true" : "false") + "},";
    // Display
    json += "\"display\":{\"azElDecimals\":" + String(azElDisplayDecimals ? "true" : "false") + "},";
    // ELF
    json += "\"elf\":" + String(elfMode ? "true" : "false") + ",";
    // Firmware
    json += "\"firmware\":{\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
    json += "\"date\":\"" + String(FIRMWARE_DATE) + "\"},";
    // Tracking position
    json += "\"position\":{\"az\":" + String(currentAz, 1) + ",\"el\":" + String(currentEl, 1) + "}";
    json += "}";

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache");
    server.send(200, "application/json", json);
  });

  // POST /api/settings/rotator — set rotator model
  // Body: {"model":"N360"} or {"model":"S360"} or {"model":"N450"} or {"model":"S450"}
  server.on("/api/settings/rotator", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
    String body = server.arg("plain");
    int idx = body.indexOf("\"model\"");
    if (idx < 0) { server.send(400, "application/json", "{\"error\":\"missing model\"}"); return; }
    int colon = body.indexOf(':', idx);
    int start = colon + 1;
    while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t' || body[start] == '\"')) start++;
    int end = start;
    while (end < (int)body.length() && body[end] != '\"') end++;
    String modelStr = body.substring(start, end);
    
    RotModel newModel = ROT_N360;
    if (modelStr == "S360") newModel = ROT_S360;
    else if (modelStr == "N450") newModel = ROT_N450;
    else if (modelStr == "S450") newModel = ROT_S450;
    else if (modelStr != "N360") {
      server.send(400, "application/json", "{\"error\":\"invalid model\"}");
      return;
    }
    
    if (newModel != rotModel) {
      rotModel = newModel;
      saveRotatorModel(true);  // Clear calibration on model change
      Serial.print("API: Rotator model set to "); Serial.println(modelStr);
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true,\"model\":\"" + modelStr + "\"}");
  });

  // POST /api/settings/wifi-mode — switch WiFi mode
  // Body: {"mode":"AP"} or {"mode":"STA"} or {"mode":"DISABLED"}
  server.on("/api/settings/wifi-mode", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
    String body = server.arg("plain");
    int idx = body.indexOf("\"mode\"");
    if (idx < 0) { server.send(400, "application/json", "{\"error\":\"missing mode\"}"); return; }
    int colon = body.indexOf(':', idx);
    int start = colon + 1;
    while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t' || body[start] == '\"')) start++;
    int end = start;
    while (end < (int)body.length() && body[end] != '\"') end++;
    String modeStr = body.substring(start, end);

    if (modeStr == "AP") {
      if (wifiState == WIFI_AP_MODE) {
        server.send(200, "application/json", "{\"ok\":true,\"mode\":\"AP\",\"notice\":\"already in AP mode\"}");
        return;
      }
      // Switch to AP mode
      if (wifiState != WIFI_AP_MODE) {
        stopApMode();
        WiFi.mode(WIFI_OFF);
        delay(100);
      }
      deviceWifiMode = MODE_AP;
      saveWifiMode();
      startApMode();
      Serial.println("API: WiFi mode switched to AP");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "application/json", "{\"ok\":true,\"mode\":\"AP\",\"notice\":\"Reconnect to AZELCTRL_XXXX SSID at 192.168.4.1\"}");
    } else if (modeStr == "STA") {
      if (wifiState == WIFI_AP_MODE) { stopApMode(); WiFi.mode(WIFI_OFF); delay(100); }
      deviceWifiMode = MODE_STA;
      saveWifiMode();
      if (hasWifiCredentials()) {
        connectWifiSta();
        Serial.println("API: WiFi mode switched to STA — connecting...");
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json", "{\"ok\":true,\"mode\":\"STA\",\"notice\":\"Connecting to home WiFi. Reconnect to your LAN and find the new IP.\"}");
      } else {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json", "{\"ok\":true,\"mode\":\"STA\",\"notice\":\"No WiFi credentials saved. Use Set SSID and Password.\"}");
      }
    } else if (modeStr == "DISABLED") {
      if (wifiState == WIFI_AP_MODE) { stopApMode(); }
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      deviceWifiMode = MODE_DISABLED;
      saveWifiMode();
      Serial.println("API: WiFi mode set to DISABLED");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "application/json", "{\"ok\":true,\"mode\":\"DISABLED\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"invalid mode\"}");
    }
  });

  // POST /api/settings/wifi-sta — set WiFi station SSID and password
  // Body: {"ssid":"MyNetwork","password":"MyPassword"}
  server.on("/api/settings/wifi-sta", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
    String body = server.arg("plain");
    
    // Extract ssid
    int ssidIdx = body.indexOf("\"ssid\"");
    if (ssidIdx < 0) { server.send(400, "application/json", "{\"error\":\"missing ssid\"}"); return; }
    int ssidColon = body.indexOf(':', ssidIdx);
    int ssidStart = ssidColon + 1;
    while (ssidStart < (int)body.length() && (body[ssidStart] == ' ' || body[ssidStart] == '\t' || body[ssidStart] == '\"')) ssidStart++;
    int ssidEnd = ssidStart;
    while (ssidEnd < (int)body.length() && body[ssidEnd] != '\"') ssidEnd++;
    String newSsid = body.substring(ssidStart, ssidEnd);
    
    // Extract password
    int passIdx = body.indexOf("\"password\"");
    if (passIdx < 0) { server.send(400, "application/json", "{\"error\":\"missing password\"}"); return; }
    int passColon = body.indexOf(':', passIdx);
    int passStart = passColon + 1;
    while (passStart < (int)body.length() && (body[passStart] == ' ' || body[passStart] == '\t' || body[passStart] == '\"')) passStart++;
    int passEnd = passStart;
    while (passEnd < (int)body.length() && body[passEnd] != '\"') passEnd++;
    String newPass = body.substring(passStart, passEnd);
    
    saveWifiCredentials(newSsid.c_str(), newPass.c_str());
    wifiConnectItems[2].visible = true;
    Serial.print("API: WiFi STA credentials saved (SSID: "); Serial.print(newSsid); Serial.println(")");
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // POST /api/settings/protocol — set protocol parameters
  // Body: {"variant":"GS232A","baudIndex":4,"showCommands":true,"reverseDelay":1}
  server.on("/api/settings/protocol", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
    String body = server.arg("plain");
    bool changed = false;
    
    // Protocol variant
    int varIdx = body.indexOf("\"variant\"");
    if (varIdx >= 0) {
      int colon = body.indexOf(':', varIdx);
      int start = colon + 1;
      while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t' || body[start] == '\"')) start++;
      int end = start;
      while (end < (int)body.length() && body[end] != '\"') end++;
      String varStr = body.substring(start, end);
      ProtocolVariant newVar = PROTO_GS232A;
      if (varStr == "GS232B") newVar = PROTO_GS232B;
      else if (varStr == "EASYCOM") newVar = PROTO_EASYCOM;
      else if (varStr != "GS232A") { server.send(400, "application/json", "{\"error\":\"invalid variant\"}"); return; }
      currentProtocol = newVar;
      changed = true;
    }
    
    // Baud index (0-4)
    int baudIdx = body.indexOf("\"baudIndex\"");
    if (baudIdx >= 0) {
      int colon = body.indexOf(':', baudIdx);
      int start = colon + 1;
      while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t')) start++;
      int newBaudIdx = body.substring(start).toInt();
      if (newBaudIdx < 0 || newBaudIdx >= PROTOCOL_BAUD_COUNT) {
        server.send(400, "application/json", "{\"error\":\"invalid baudIndex\"}"); return;
      }
      protocolBaudIndex = newBaudIdx;
      applyProtocolBaud();
      changed = true;
    }
    
    // Show commands
    int showIdx = body.indexOf("\"showCommands\"");
    if (showIdx >= 0) {
      int colon = body.indexOf(':', showIdx);
      int start = colon + 1;
      while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t')) start++;
      protocolShowCommands = (body.substring(start, start + 4) == "true");
      changed = true;
    }
    
    // Reverse delay (0-3)
    int revIdx = body.indexOf("\"reverseDelay\"");
    if (revIdx >= 0) {
      int colon = body.indexOf(':', revIdx);
      int start = colon + 1;
      while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t')) start++;
      int newDelay = body.substring(start).toInt();
      if (newDelay < 0 || newDelay > 3) {
        server.send(400, "application/json", "{\"error\":\"invalid reverseDelay\"}"); return;
      }
      protocolReverseDelay = newDelay;
      changed = true;
    }
    
    if (changed) {
      saveProtocolSettings();
      Serial.println("API: Protocol settings saved");
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // POST /api/settings/hamlib — enable/disable Hamlib TCP server
  // Body: {"enabled":true} or {"enabled":false}
  server.on("/api/settings/hamlib", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
    String body = server.arg("plain");
    int enIdx = body.indexOf("\"enabled\"");
    if (enIdx < 0) { server.send(400, "application/json", "{\"error\":\"missing enabled\"}"); return; }
    int colon = body.indexOf(':', enIdx);
    int start = colon + 1;
    while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t')) start++;
    bool enabled = (body.substring(start, start + 4) == "true");
    
    hamlibServerEnabled = enabled;
    saveNetworkSettings();
    if (enabled) {
      if (WiFi.status() == WL_CONNECTED) startRotctlServer();
    } else {
      stopRotctlServer();
    }
    Serial.print("API: Hamlib server "); Serial.println(enabled ? "enabled" : "disabled");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // POST /api/settings/display — set display options
  // Body: {"azElDecimals":true} — one decimal digit for Az/El (OLED + web UI)
  server.on("/api/settings/display", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
    String body = server.arg("plain");
    int decIdx = body.indexOf("\"azElDecimals\"");
    if (decIdx < 0) { server.send(400, "application/json", "{\"error\":\"missing azElDecimals\"}"); return; }
    int colon = body.indexOf(':', decIdx);
    int start = colon + 1;
    while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t')) start++;
    bool newVal = (body.substring(start, start + 4) == "true");

    if (newVal != azElDisplayDecimals) {
      azElDisplayDecimals = newVal;
      saveDisplaySettings();
      Serial.print("API: Decimal degrees "); Serial.println(newVal ? "enabled" : "disabled");
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true,\"azElDecimals\":" + String(azElDisplayDecimals ? "true" : "false") + "}");
  });

  // POST /api/settings/calibrate — set calibration values or skip
  // Body: {"action":"skip"} — use default 3.2V range
  // Body: {"action":"set","azMinV":0.0,"azMaxV":3.2,"elMinV":0.0,"elMaxV":3.2}
  server.on("/api/settings/calibrate", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
    String body = server.arg("plain");
    
    int actIdx = body.indexOf("\"action\"");
    if (actIdx < 0) { server.send(400, "application/json", "{\"error\":\"missing action\"}"); return; }
    int colon = body.indexOf(':', actIdx);
    int start = colon + 1;
    while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t' || body[start] == '\"')) start++;
    int end = start;
    while (end < (int)body.length() && body[end] != '\"') end++;
    String action = body.substring(start, end);
    
    if (action == "skip") {
      // Same as actionCalibrateSkip()
      calAzMinV = 0.0F; calAzMaxV = 3.2F;
      calElMinV = 0.0F; calElMaxV = 3.2F;
      calAzValid = true; calElValid = true;
      uint8_t* bytes;
      bytes = (uint8_t*)&calAzMinV;
      for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_AZ_MIN + i, bytes[i]);
      bytes = (uint8_t*)&calAzMaxV;
      for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_AZ_MAX + i, bytes[i]);
      bytes = (uint8_t*)&calElMinV;
      for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_EL_MIN + i, bytes[i]);
      bytes = (uint8_t*)&calElMaxV;
      for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_EL_MAX + i, bytes[i]);
      EEPROM.write(CAL_EEPROM_MAGIC, CAL_EEPROM_MAGIC_VAL);
      EEPROM.commit();
      Serial.println("API: Calibration skipped (default 3.2V range)");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "application/json", "{\"ok\":true,\"status\":\"skipped\",\"azMinV\":0.0,\"azMaxV\":3.2,\"elMinV\":0.0,\"elMaxV\":3.2}");
    } else if (action == "set") {
      // Parse voltage values from JSON
      auto parseFloat = [&](const String& key) -> float {
        int idx = body.indexOf("\"" + key + "\"");
        if (idx < 0) return -1.0F;
        int c = body.indexOf(':', idx);
        int s = c + 1;
        while (s < (int)body.length() && (body[s] == ' ' || body[s] == '\t')) s++;
        int e = s;
        while (e < (int)body.length() && (body[e] == '.' || body[e] == '-' || isdigit(body[e]))) e++;
        return body.substring(s, e).toFloat();
      };
      float azMin = parseFloat("azMinV");
      float azMax = parseFloat("azMaxV");
      float elMin = parseFloat("elMinV");
      float elMax = parseFloat("elMaxV");
      if (azMin < 0 || azMax < 0 || elMin < 0 || elMax < 0) {
        server.send(400, "application/json", "{\"error\":\"missing voltage values\"}"); return;
      }
      calAzMinV = azMin; calAzMaxV = azMax;
      calElMinV = elMin; calElMaxV = elMax;
      calAzValid = true; calElValid = true;
      uint8_t* bytes;
      bytes = (uint8_t*)&calAzMinV;
      for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_AZ_MIN + i, bytes[i]);
      bytes = (uint8_t*)&calAzMaxV;
      for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_AZ_MAX + i, bytes[i]);
      bytes = (uint8_t*)&calElMinV;
      for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_EL_MIN + i, bytes[i]);
      bytes = (uint8_t*)&calElMaxV;
      for (int i = 0; i < 4; i++) EEPROM.write(CAL_EEPROM_EL_MAX + i, bytes[i]);
      EEPROM.write(CAL_EEPROM_MAGIC, CAL_EEPROM_MAGIC_VAL);
      EEPROM.commit();
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "application/json", "{\"ok\":true,\"status\":\"set\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"unknown action\"}");
    }
  });

  // POST /api/settings/ap-password — change AP password
  // Body: {"password":"newpass"}
  server.on("/api/settings/ap-password", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
    String body = server.arg("plain");
    int passIdx = body.indexOf("\"password\"");
    if (passIdx < 0) { server.send(400, "application/json", "{\"error\":\"missing password\"}"); return; }
    int colon = body.indexOf(':', passIdx);
    int start = colon + 1;
    while (start < (int)body.length() && (body[start] == ' ' || body[start] == '\t' || body[start] == '\"')) start++;
    int end = start;
    while (end < (int)body.length() && body[end] != '\"') end++;
    String newPass = body.substring(start, end);
    if (newPass.length() < 8 || newPass.length() > 8) {
      server.send(400, "application/json", "{\"error\":\"password must be exactly 8 characters\"}"); return;
    }
    strncpy(apPassword, newPass.c_str(), 8);
    apPassword[8] = '\0';
    for (int i = 0; i < 8; i++) EEPROM.write(AP_PASS_EEPROM_START + i, apPassword[i]);
    EEPROM.write(AP_PASS_EEPROM_MAGIC, AP_PASS_EEPROM_MAGIC_VAL);
    EEPROM.commit();
    Serial.print("API: AP password changed to "); Serial.println(apPassword);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();
  Serial.println("HTTP server started on port 80");
}

// ====================== SETUP ======================
void setup() {
  // Initialize EEPROM first — needed for loading saved settings
  EEPROM.begin(EEPROM_SIZE);
  loadProtocolSettings();
  loadNetworkSettings();
  loadDisplaySettings();
  loadTimeOffset();
  Serial.begin(getProtocolBaud());
  delay(200);
  
  Serial.println("\n========================================");
  Serial.println("  Yaesu Rotator Controller");
  Serial.println("========================================");
  
  // Initialize SPIFFS filesystem
  if (SPIFFS.begin()) {
    Serial.println("SPIFFS: OK");
    // List files for debugging
    Serial.print("SPIFFS files: ");
    Dir root = SPIFFS.openDir("/");
    while (root.next()) {
      String filename = root.fileName();
      size_t size = root.fileSize();
      Serial.print(filename);
      Serial.print(" (");
      Serial.print(size);
      Serial.print("B) ");
    }
    Serial.println();
  } else {
    Serial.println("SPIFFS: NOT MOUNTED (using PROGMEM fallback)");
  }
  
  // Safe boot state
  pinMode(AZ_ENABLE, OUTPUT); digitalWrite(AZ_ENABLE, LOW);
  pinMode(AZ_DIR_CW, OUTPUT); digitalWrite(AZ_DIR_CW, HIGH);
  pinMode(EL_ENABLE, OUTPUT); digitalWrite(EL_ENABLE, LOW);
  pinMode(EL_DIR_UP, OUTPUT); digitalWrite(EL_DIR_UP, HIGH);
  
  // Buttons
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  
  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.print("I2C: SDA=D6, SCL=D7");
  
  // I2C scan
  byte i2cCount = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(" Found: 0x");
      Serial.println(addr, HEX);
      i2cCount++;
    }
  }
  Serial.print(" Total: ");
  Serial.println(i2cCount);
  
  // ADS1115
  adsPresent = ads.begin();
  if (adsPresent) {
    ads.setGain((adsGain_t)ADS1X15_REG_CONFIG_PGA_4_096V);  // ±4.096V range for better resolution
    Serial.println("ADS1115: OK (±4.096V range)");
  } else {
    Serial.println("ADS1115: NOT FOUND");
  }
  
  // OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED: OK");
  } else {
    Serial.println("OLED: NOT FOUND");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  // Load saved WiFi credentials
  loadWifiCredentials();
  loadRotatorModel();
  loadElfMode();
  loadCalibration();
  Serial.print("Saved SSID: ");
  Serial.println(savedSsid.length() > 0 ? savedSsid : "(none)");
  
  // Update menu visibility based on credentials
  wifiConnectItems[2].visible = hasWifiCredentials();
  
  // Boot screen with version info (display for 2 seconds)
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  // Center the product name
  const char* productName = "AZELCtrl";
  display.setCursor(0, 0);
  display.println(productName);
  display.setTextSize(1);
  display.setCursor(0, 22);
  display.print("Ver: "); display.println(FIRMWARE_VERSION);
  display.setCursor(0, 34);
  display.print("Date: "); display.println(FIRMWARE_DATE);
  display.setCursor(0, 46);
  display.println("By: ZS6TW");
  display.display();
  delay(3000);
  
  // Load saved WiFi mode
  loadWifiMode();
  loadApPassword();
  initApSsid();

  // If mandatory settings missing, show "Missing Settings" then go straight to setup
  if (!allSetupComplete()) {
    Serial.println("BOOT: Mandatory settings incomplete - auto-starting AP mode");
    
    // Auto-start AP mode so the user can connect via WiFi immediately to
    // upload supporting files (index.html.gz, favicon.ico, world_coastline.js)
    // via the web interface even before completing the OLED setup menu.
    startApMode();
    
    Serial.println("BOOT: Showing missing settings notice");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    // Center justify each line
    const char* line1 = "Missing";
    const char* line2 = "Settings!";
    int x1 = (int)max((int)0, (int)((128 - strlen(line1) * 12) / 2));
    int x2 = (int)max((int)0, (int)((128 - strlen(line2) * 12) / 2));
    display.setCursor(x1, 12);
    display.println(line1);
    display.setCursor(x2, 28);
    display.println(line2);
    display.display();
    delay(3000);

    // Go directly to setup menu
    updateSetupLabels();
    updateSetupTimers();
    currentMenu = &setupMenu;
    currentMenu->init();
  } else {
    // Show boot menu: Skip / Setup
    currentMenu = &bootMenu;
    currentMenu->init();
  }

  Serial.println("\nMenu system initialized");
  Serial.println("Btn1: Navigate/Select");
  Serial.println("Btn2: Back");
}

// ====================== MAIN LOOP ======================
void loop() {
  // Handle protocol commands
  processProtocolInput();
  
  // Flush a stalled partial command (unterminated/malformed input) so the
  // parser state can't wedge
  checkProtocolStall();
  
  // Check for TCP client connections
  checkRotctlClient();
  
  // Pick up NTP time (debug-log wall-clock timestamps)
  checkTimeSync();
  
  // Handle web server
  if (!captivePortalActive) {
    server.handleClient();
  } else {
    tickCaptivePortal();
  }
  
  // Process DNS for captive portal effect (both captive portal and AP mode)
  if (captivePortalActive || wifiState == WIFI_AP_MODE) {
    dnsServer.processNextRequest();
  }
  
  // Tick WiFi connection
  tickWifi();
  
  // Check pause state
  checkPauseComplete();
  
  // Handle buttons
  handleButtons();
  
  // Calibration mode
  if (calState != CAL_IDLE) {
    readPositions();  // Keep ADC readings fresh during calibration
    updateCalibrationState();
    displayCalibrationScreen();
    return;
  }
  
  // Update positions regularly regardless of mode (OLED + TCP both need fresh data)
  static unsigned long lastPositionUpdate = 0;
  if (millis() - lastPositionUpdate >= 200) {
    readPositions();
    lastPositionUpdate = millis();
  }
  
  // Menu mode
  if (currentMenu) {
    currentMenu->tick();
    // tick() may have changed currentMenu (e.g., timer auto-fired actionRotatorControl)
    if (currentMenu) {
      currentMenu->draw();
    }
  } else {
    // Rotator control mode
    static unsigned long lastUpdate = 0;
    const unsigned long UPDATE_INTERVAL = 200;
    
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
      readPositions();
      // Update mechanical offset for N450/S450 variants based on position change
      if ((rotModel == ROT_N450 || rotModel == ROT_S450) && azDirection != DIR_STOPPED) {
        // Calculate position change from previous reading
        float azDelta = currentAz - lastAzReading;
        // Handle wrap-around
        if (azDelta > 180.0) azDelta -= 360.0;
        if (azDelta < -180.0) azDelta += 360.0;
        // Update mechanical offset based on direction
        if (azDirection == DIR_CW) {
          mechanicalAzOffset += azDelta;  // CW increases offset
        } else if (azDirection == DIR_CCW) {
          mechanicalAzOffset += azDelta;  // CCW decreases offset (delta is negative)
        }
        // Clamp to mechanical limits
        if (mechanicalAzOffset > 225.0) mechanicalAzOffset = 225.0;
        if (mechanicalAzOffset < -225.0) mechanicalAzOffset = -225.0;
        lastAzReading = currentAz;
      }
      // Handle direction reversal delay
      if (protocolAzReversing) {
        if (millis() - protocolAzReverseStart >= (unsigned long)(protocolReverseDelay * 1000)) {
          protocolAzReversing = false;
          executeAzimuthMove(protocolAzPendingDir);
        }
      }
      if (protocolElReversing) {
        if (millis() - protocolElReverseStart >= (unsigned long)(protocolReverseDelay * 1000)) {
          protocolElReversing = false;
          executeElevationMove(protocolElPendingDir);
        }
      }
      // Auto-stop protocol targets when reached (after position is updated)
      if (protocolTargetAzSet && !protocolAzReversing) {
        float currentAzDisplay = displayAzimuth(currentAz);
        float diff = protocolTargetAz - currentAzDisplay;
        if (diff > 180.0) diff -= 360.0;
        if (diff < -180.0) diff += 360.0;
        if (abs(diff) <= 1.0) {
          stopAzimuth();
          protocolTargetAzSet = false;
        }
      }
      if (protocolTargetElSet && !protocolElReversing) {
        float diff = protocolTargetEl - currentEl;
        if (abs(diff) <= 1.0) {
          stopElevation();
          protocolTargetElSet = false;
        }
      }
      updateRotatorDisplay();
      lastUpdate = millis();
    }
  }
  // Auto-stop protocol targets when reached (always run, even in menu mode)
  // Use direction-based cross-target detection
  // Read cached currentAz/currentEl updated by readPositions() every 200ms
  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint >= 500) {
    lastDebugPrint = millis();
    #if DEBUG_ENABLED
    Serial.print("DBG:AUTO_CHECK:azSet="); Serial.print(protocolTargetAzSet);
    Serial.print(" elSet="); Serial.print(protocolTargetElSet);
    Serial.print(" azRev="); Serial.print(protocolAzReversing);
    Serial.print(" ads="); Serial.println(adsPresent);
    #endif
  }
  if (protocolTargetAzSet || protocolTargetElSet) {
    // Rate-limit auto-stop checks to match position update rate (100ms)
    static unsigned long lastAutoStopCheck = 0;
    if (millis() - lastAutoStopCheck >= 100) {
      lastAutoStopCheck = millis();
      
      if (protocolTargetAzSet && !protocolAzReversing) {
        float curAzDisplay = displayAzimuth(currentAz);
        float targetAz = protocolTargetAz;
        float diff = targetAz - curAzDisplay;
        if (diff > 180.0) diff -= 360.0;
        if (diff < -180.0) diff += 360.0;
        #if DEBUG_ENABLED
        Serial.print("DBG:AUTO_AZ:cur="); Serial.print(curAzDisplay);
        Serial.print(" tgt="); Serial.print(protocolTargetAz);
        Serial.print(" diff="); Serial.print(diff);
        Serial.print(" dir="); Serial.println(azDirection);
        #endif
        bool crossed = false;
        static bool inCWWrap = false;
        static bool inCCWWrap = false;
        
        if (!inCWWrap && !inCCWWrap) {
          if (azDirection == DIR_CW && curAzDisplay > 180.0 && protocolTargetAz < 180.0) {
            inCWWrap = true;
          } else if (azDirection == DIR_CCW && curAzDisplay < 180.0 && protocolTargetAz > 180.0) {
            inCCWWrap = true;
          }
        }
        
        if (inCWWrap) {
          float unwrappedTarget = protocolTargetAz + 360.0;
          float compareCurrent = curAzDisplay;
          if (compareCurrent < 180.0) compareCurrent += 360.0;
          if (compareCurrent >= unwrappedTarget) crossed = true;
          if (crossed || !protocolTargetAzSet) inCWWrap = false;
        }
        else if (inCCWWrap) {
          float unwrappedTarget = protocolTargetAz - 360.0;
          float compareCurrent = curAzDisplay;
          if (compareCurrent > 180.0) compareCurrent -= 360.0;
          if (compareCurrent <= unwrappedTarget) crossed = true;
          if (crossed || !protocolTargetAzSet) inCCWWrap = false;
        }
        
        float normDiff = diff;
        if (normDiff > 180.0) normDiff -= 360.0;
        if (normDiff < -180.0) normDiff += 360.0;
        // Tolerance accounts for mechanical overshoot: motor keeps moving after power cut.
        // 3.0° stops early enough to avoid ±3-5° oscillation observed in tracking.
        if (abs(normDiff) <= 3.0) crossed = true;
        
        if (crossed) {
          #if DEBUG_ENABLED
          Serial.println("DBG:AUTO_AZ:STOP!");
          #endif
          stopAzimuth();
          protocolTargetAzSet = false;
        }
      }
      
      if (protocolTargetElSet && !protocolElReversing) {
        float diff = protocolTargetEl - currentEl;
        #if DEBUG_ENABLED
        Serial.print("DBG:AUTO_EL:cur="); Serial.print(currentEl);
        Serial.print(" tgt="); Serial.print(protocolTargetEl);
        Serial.print(" diff="); Serial.print(diff);
        Serial.print(" dir="); Serial.println(elDirection);
        #endif
        bool crossed = false;
        if (elDirection == DIR_UP && diff < 0.0) crossed = true;
        if (elDirection == DIR_DOWN && diff > 0.0) crossed = true;
        // Same tolerance logic as azimuth - account for mechanical overshoot
        if (crossed || abs(diff) <= 3.0) {
          #if DEBUG_ENABLED
          Serial.println("DBG:AUTO_EL:STOP!");
          #endif
          stopElevation();
          protocolTargetElSet = false;
        }
      }
    }
  }
  
  // Elevation limit protection — always run when elevation is moving,
  // regardless of whether movement came from protocol or buttons
  // Use cached currentEl updated by readPositions() every 200ms
  if (elDirection != DIR_STOPPED) {
    static float lastElReading = -1;
    static unsigned long lastElCheck = 0;
    static int stallCount = 0;
    
    if (millis() - lastElCheck >= 500) {
      lastElCheck = millis();
      bool shouldStop = false;
      
      // Hard limit: DOWN at bottom (< 0.5°) or UP at top (> 179.5°)
      if (elDirection == DIR_DOWN && currentEl < 0.5F) {
        shouldStop = true;
      }
      if (elDirection == DIR_UP && currentEl > 179.5F) {
        shouldStop = true;
      }
      
      // Stall detection: elevation hasn't changed by 0.5° in the timeout period
      unsigned long stallTimeout = (elDirection == DIR_UP && currentEl < 1.0F) ? 3000 : 500;
      if (!shouldStop && millis() - lastElCheck >= stallTimeout) {
        if (lastElReading >= 0) {
          float elChange = abs(currentEl - lastElReading);
          if (elChange < 0.5F) {
            stallCount++;
            if (stallCount >= 3) {
              shouldStop = true;
            }
          } else {
            stallCount = 0;
          }
        }
        lastElReading = currentEl;
      }
      
      if (shouldStop) {
        #if DEBUG_ENABLED
        Serial.print("DBG:EL_LIMIT:STOP:el="); Serial.print(currentEl);
        Serial.print(" dir="); Serial.println(elDirection == DIR_UP ? "UP" : "DOWN");
        #endif
        stopElevation();
        protocolTargetElSet = false;
        lastElReading = -1;
      }
    }
  }
  
  delay(10);
}
