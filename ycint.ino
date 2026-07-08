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

// ==================== COMPILE-TIME DEBUG FLAGS ====================
// Set to 1 to enable debug output (prefixed with 'DBG:' for easy filtering)
// Set to 0 to disable all debug output (zero overhead)
#define DEBUG_ENABLED 0
#define DEBUG_PROTOCOL 0  // Debug protocol parsing details
#define DEBUG_MOVEMENT 0  // Debug movement commands

// Debug helper macros - output prefixed with 'DBG:' so Python tests can filter
#if DEBUG_ENABLED
  #define DBG_PRINT(msg) Serial.print("DBG:" msg)
  #define DBG_PRINTLN(msg) Serial.println("DBG:" msg)
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
const int AZ_ENABLE  = D2;   // GPIO4
const int AZ_DIR_CW  = D3;   // GPIO0
const int EL_ENABLE  = D1;   // GPIO5
const int EL_DIR_UP  = D4;   // GPIO2

const int SDA_PIN = D6;      // GPIO12
const int SCL_PIN = D7;      // GPIO13

const int BTN1 = D8;         // GPIO15 - active HIGH
const int BTN2 = D5;         // GPIO14 - active LOW

// ==================== BUTTON TIMING ====================
const unsigned long BTN_DEBOUNCE_MS = 200;   // Debounce between presses
const unsigned long BTN_LONG_MS = 800;       // Long press threshold for menu select
const unsigned long BTN2_LONG_MS = 500;      // Btn2 long press for calibration reverse

// ==================== WiFi AP SETTINGS ====================
const char* AP_SSID = "AZELCTRL";
const char* AP_PASS = "yaesu";
const char* AP_IP   = "192.168.4.1";

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

// Helper: send a protocol response string over serial
void protocolSend(const String& response) {
  Serial.println(response);
  DBG_PRINT("PROTOCOL:TX:"); DBG_PRINTLN(response);
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
      cursor(0), timerStart(0), timerActive(false), userInteracted(false), footerText(nullptr) {}

  void init() {
    cursor = 0;
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
    
    // Menu items - start 1 row below separator
    int y = 10;
    for (int i = 0; i < itemCount && y < 54; i++) {
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
  unsigned long timerStart;
  bool timerActive;
  bool userInteracted;  // True when user presses a button — disables auto-timer
  const char* footerText;  // Custom footer text (nullptr = default "1.Nav 1|Sel 2.Back")

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
extern Menu setupMenu;

// ==================== WIFI CREDENTIAL MANAGEMENT ====================
// EEPROM layout: byte 0=ssid_len, bytes 1-32=ssid, byte 33=pass_len, bytes 34-65=pass, byte 66=magic
#define EEPROM_SSID_LEN   0
#define EEPROM_SSID_DATA  1
#define EEPROM_PASS_LEN   33
#define EEPROM_PASS_DATA  34
#define EEPROM_MAGIC_BYTE 66
#define EEPROM_SIZE       67
#define EEPROM_MAGIC      0xA5

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
  for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0);
  
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
  deviceWifiMode = MODE_AP;
  wifiState = WIFI_AP_MODE;
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  
  setupWebServer();
  
  Serial.println("AP Mode: " + String(AP_SSID));
  Serial.println("IP: " + String(AP_IP));
}

void stopApMode() {
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
    // Show connected screen with signal strength
    int rssi = WiFi.RSSI();
    String quality;
    if (rssi >= -50) quality = "Excellent";
    else if (rssi >= -60) quality = "Good";
    else if (rssi >= -70) quality = "Fair";
    else quality = "Poor";

    String rssiLine = String(rssi) + " dBm";
    Menu::drawQuickNotice("Connected", rssiLine.c_str(), quality.c_str(), true);

    // Wait 2 seconds so user can see
    for (int i = 0; i < 20; i++) {
      ESP.wdtFeed();
      delay(100);
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
  WiFi.softAP(AP_SSID);  // Open for captive portal setup
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  
  dnsServer.start(53, "*", apIP);
  
  server.on("/", handlePortal);
  server.on("/save", handleSaveWifi);
  server.onNotFound(handlePortal);
  server.begin();
  
  Serial.println("Captive portal started on " + String(AP_SSID));
}

void stopCaptivePortal() {
  captivePortalActive = false;
  dnsServer.stop();
  server.stop();
  WiFi.mode(WIFI_OFF);
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

const char html_rotator[] PROGMEM = R"RAWHTML(
<!DOCTYPE html><html><head>
<title>Rotator Control</title>
<meta name='viewport' content='width=device-width,initial-scale=1.0'>
<style>
body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;text-align:center;margin:0;padding:20px}
h1{color:#00d4ff}
.info{background:#16213e;padding:15px;border-radius:8px;margin:10px auto;max-width:300px}
.info div{margin:5px 0;font-size:18px}
.controls{margin:20px auto;max-width:300px}
button{background:#0f3460;color:#fff;border:2px solid #00d4ff;border-radius:8px;padding:15px 25px;font-size:16px;cursor:pointer;margin:5px;min-width:70px;transition:padding 0.05s ease,background 0.05s ease,box-shadow 0.05s ease;box-shadow:0 4px 0 #0a2445}
button:active{background:#00b8d4;color:#fff;padding:17px 25px 13px 25px;box-shadow:0 1px 0 #0a2445}
.grid{display:grid;grid-template-columns:70px 70px 70px;grid-template-rows:70px 70px;gap:5px;justify-content:center}
.stop{background:#e94560;color:white;border-color:#e94560}
.stop:active{background:#ff6b6b}
</style></head><body>
<h1>Rotator Control</h1>
<div class='info'>
<div>Az: <span id='az'>--</span>&deg;</div>
<div>El: <span id='el'>--</span>&deg;</div>
<div>Move: <span id='move'>IDLE</span></div>
<div>Status: <span id='status'>OK</span></div>
</div>
<div class='controls'>
<div class='grid'>
<div></div>
<button class='dir-btn' onmousedown='sendCmd("UP")' onmouseup='sendCmd("STOP")' ontouchstart='sendCmd("UP")' ontouchend='sendCmd("STOP")'>UP</button>
<div></div>
<button class='dir-btn' onmousedown='sendCmd("LEFT")' onmouseup='sendCmd("STOP")' ontouchstart='sendCmd("LEFT")' ontouchend='sendCmd("STOP")'>LEFT</button>
<button class='stop' onmousedown='sendCmd("STOP")' onmouseup='sendCmd("STOP")' ontouchstart='sendCmd("STOP")' ontouchend='sendCmd("STOP")'>STOP</button>
<button class='dir-btn' onmousedown='sendCmd("RIGHT")' onmouseup='sendCmd("STOP")' ontouchstart='sendCmd("RIGHT")' ontouchend='sendCmd("STOP")'>RIGHT</button>
<div></div>
<button class='dir-btn' onmousedown='sendCmd("DOWN")' onmouseup='sendCmd("STOP")' ontouchstart='sendCmd("DOWN")' ontouchend='sendCmd("STOP")'>DOWN</button>
<div></div>
</div></div>
<script>
function sendCmd(c){fetch('/cmd?dir='+c)}
setInterval(function(){
fetch('/status').then(r=>r.json()).then(d=>{
document.getElementById('az').textContent=d.az;
document.getElementById('el').textContent=d.el;
document.getElementById('move').textContent=d.move;
document.getElementById('status').textContent=d.pause?'PAUSE':'OK';});
fetch('/btn').then(r=>r.json()).then(d=>{
var btns=document.querySelectorAll('.dir-btn');
var active=null;
if(d.btn2&&d.move!=='IDLE'){if(d.move==='LEFT')active='LEFT';else if(d.move==='RIGHT')active='RIGHT';else if(d.move==='UP')active='UP';else if(d.move==='DOWN')active='DOWN';}
else if(d.webMove!=='IDLE'&&d.webMove!='')active=d.webMove;
for(var i=0;i<btns.length;i++){if(btns[i].classList.contains('dir-btn')){if(btns[i].textContent===active)btns[i].style.background='#00b8d4';else btns[i].style.background='#0f3460';}}
},300);</script>
</body></html>
)RAWHTML";

// Helper: send PROGMEM content via web server
void sendProgmemHTML(const char* pgmHtml) {
  server.setContentLength(strlen_P(pgmHtml));
  server.send(200, "text/html");
  server.sendContent(pgmHtml);
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
#define CAL_EEPROM_AZ_MIN    67
#define CAL_EEPROM_AZ_MAX    71
#define CAL_EEPROM_EL_MIN    75
#define CAL_EEPROM_EL_MAX    79
#define CAL_EEPROM_MAGIC     83
#define CAL_EEPROM_MAGIC_VAL 0xCA

// WiFi mode config in EEPROM
#define WIFI_MODE_EEPROM    84
#define WIFI_MODE_MAGIC     85
#define WIFI_MODE_MAGIC_VAL 0x4D

// Rotator model config in EEPROM
#define ROT_EEPROM_MODEL     86
#define ROT_EEPROM_MAGIC     87
#define ROT_EEPROM_MAGIC_VAL 0x5A
#define CAL_EEPROM_SIZE      88

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
}

void stopElevation() {
  digitalWrite(EL_ENABLE, LOW);
  digitalWrite(EL_DIR_UP, HIGH);
  elDirection = DIR_STOPPED;
}

void stopAll() {
  stopAzimuth();
  stopElevation();
  moving = false;
  currentMove = "IDLE";
}

void executeAzimuthMove(String dir) {
  stopAzimuth();
  
  // Check voltage safety before moving
  if (adsPresent && calAzValid) {
    float azVoltage = (float)ads.readADC_SingleEnded(0) / 32767.0 * 4.096F;
    if (dir == "RIGHT" && azVoltage >= CAL_MAX_SAFE_V) {
      Serial.println("SAFETY: AZ voltage at limit, cannot move RIGHT");
      return;
    }
  }
  
  if (currentMove == "IDLE" || currentMove == "UP" || currentMove == "DOWN") {
    currentMove = dir;
  }
  moving = true;
  
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
  stopElevation();
  
  // Check voltage safety before moving
  if (adsPresent && calElValid) {
    float elVoltage = (float)ads.readADC_SingleEnded(1) / 32767.0 * 4.096F;
    if (dir == "UP" && elVoltage >= CAL_MAX_SAFE_V) {
      Serial.println("SAFETY: EL voltage at limit, cannot move UP");
      return;
    }
  }
  
  if (currentMove == "IDLE" || currentMove == "LEFT" || currentMove == "RIGHT") {
    currentMove = dir;
  }
  moving = true;
  
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
// S(+180), SW(+135), W(+90), NW(+45), N(0), NE(-45), E(-90), SE(-135), S(-180)
const char* calCardinalNames[] = {"S", "SW", "W", "NW", "N", "NE", "E", "SE", "S"};
const float calCardinalAngles[] = {180, 135, 90, 45, 0, -45, -90, -135, -180};
const int calCardinalCount = 9;

// Get the next cardinal heading index we should pause at during return sweep
// Returns -1 when no more headings to pause at
int calNextCardinalHeading(float currentHeading) {
  // We're sweeping CCW from max toward min, so headings decrease
  // Find the next heading we've reached or passed (within 1° tolerance)
  // Last heading (S at -180°) uses 0° tolerance since hard stop follows
  for (int i = calLastHeadingIndex + 1; i < calCardinalCount; i++) {
    float tolerance = (i == calCardinalCount - 1) ? 0.0F : 1.0F;
    if (currentHeading <= calCardinalAngles[i] + tolerance) {
      Serial.print("CAL: Heading ");
      Serial.print(currentHeading, 1);
      Serial.print(" reached cardinal ");
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
    Serial.println("CAL: No calibration data in EEPROM");
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
  
  Serial.print("CAL: Loaded AZ min=");
  Serial.print(calAzMinV, 3);
  Serial.print(" max=");
  Serial.print(calAzMaxV, 3);
  Serial.print(" EL min=");
  Serial.print(calElMinV, 3);
  Serial.println(" max=");
  Serial.println(calElMaxV, 3);
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

void saveRotatorModel() {
  EEPROM.write(ROT_EEPROM_MODEL, (uint8_t)rotModel);
  EEPROM.write(ROT_EEPROM_MAGIC, ROT_EEPROM_MAGIC_VAL);
  EEPROM.commit();
  Serial.println("ROT: Rotator model saved to EEPROM");
  
  // Clear calibration when model changes — azimuth range may have changed
  clearCalibration();
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

  const char* modelLabels[4] = {"N360 (-180 to 180)", "S360 (0 to 360)", "N450 (-180 to 270)", "S450 (0 to 450)"};
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
      unsigned long pressStart = now;
      while (digitalRead(BTN1) && (millis() - pressStart < 800)) {
        delay(20);
      }
      if (millis() - pressStart >= 800) {
        // Long press - select and save
        Serial.print("ROT: Selected model ");
        Serial.println(choice);
        rotModel = (RotModel)choice;
        saveRotatorModel();
        // Return immediately — do NOT wait for button release here.
        // The caller (actionSetupRotator) will wait for release on firstDraw.
        return;
      }
      // Short press - cycle choice
      choice = (choice + 1) % 4;
    }

    // Btn2: edge-detect press = back (short or long)
    if (btn2 && !btn2WasPressed) {
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

// Process GS-232A/B commands
void processGs232Command(const char* cmd, int len) {
  char upper = toupper(cmd[0]);
  
  switch (upper) {
    case 'H': {
      float az = displayAzimuth(currentAz);
      int azDeg = (int)round(az);
      if (azDeg < 0) azDeg += 360;
      if (azDeg >= 360) azDeg -= 360;
      protocolSend(String("H") + String(azDeg, DEC));
      break;
    }
    case 'V': {
      float el = currentEl;
      int elDeg = (int)round(el);
      protocolSend(String("V") + String(elDeg, DEC));
      break;
    }
    case 'C': {
      float az = displayAzimuth(currentAz);
      float el = currentEl;
      protocolSend(String("C") + String(az, 1) + "." + String(el, 1) + ".0");
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
      startMove("UP");
      protocolSend("OK");
      break;
    }
    case 'D': {
      startMove("DOWN");
      protocolSend("OK");
      break;
    }
    case 'S': {
      stopAll();
      protocolSend("OK");
      break;
    }
    case 'X':
    case 'T':
    case 'P':
    case 'N':
    case 'F':
      protocolSend("OK");
      break;
    default:
      protocolSend("ERR");
      break;
  }
}

// Process Easycom commands
void processEasycomCommand(const char* cmd, int len) {
  while (len > 0 && (cmd[len-1] == ' ' || cmd[len-1] == '\t')) len--;
  
  if (len > 0 && cmd[0] == '\\') {
    if (len == 2 && cmd[1] == '?') {
      protocolSend("\\?GS-232A/B,EASYCOM,ROTATOR");
      return;
    }
    protocolSend("ERR");
    return;
  }
  
  String cmdStr = String(cmd);
  cmdStr.toUpperCase();
  
  if (cmdStr == "AZEL") {
    // Combined AZEL query - MUST check before AZ
    float az = displayAzimuth(currentAz);
    protocolSend(String("AZEL") + String(az, 1) + "." + String(currentEl, 1) + ".0");
  } else if (cmdStr.startsWith("AZ")) {
    if (cmdStr.length() == 2) {
      float az = displayAzimuth(currentAz);
      protocolSend(String("AZ") + String(az, 1));
    } else {
      int azDeg = 0;
      int consumed = 0;
      if (parseNumber(cmd, 2, &azDeg, &consumed) == 0) {
        protocolSend("OK");
      } else {
        protocolSend("ERR");
      }
    }
  } else if (cmdStr.startsWith("EL")) {
    if (cmdStr.length() == 2) {
      protocolSend(String("EL") + String(currentEl, 1));
    } else {
      int elDeg = 0;
      int consumed = 0;
      if (parseNumber(cmd, 2, &elDeg, &consumed) == 0) {
        protocolSend("OK");
      } else {
        protocolSend("ERR");
      }
    }
  } else if (cmdStr == "CW") {
    startMove("RIGHT");
    protocolSend("OK");
  } else if (cmdStr == "CCW") {
    startMove("LEFT");
    protocolSend("OK");
  } else if (cmdStr == "UP") {
    startMove("UP");
    protocolSend("OK");
  } else if (cmdStr == "DOWN") {
    startMove("DOWN");
    protocolSend("OK");
  } else if (cmdStr == "STOP") {
    stopAll();
    protocolSend("OK");
  } else if (cmdStr.startsWith("TIMEOUT") || cmdStr.startsWith("SPEED") || cmdStr == "PARK") {
    protocolSend("OK");
  } else if (cmdStr == "HELP") {
    protocolSend("HELP:GS-232A/B,EASYCOM,ROTATOR");
  } else {
    protocolSend("ERR");
  }
}

// Main protocol input handler - called from loop()
void processProtocolInput() {
  while (Serial.available()) {
    char ch = Serial.read();
    
    if (ch == '\n' || ch == '\r') {
      if (protocolBufIdx > 0) {
        protocolBuf[protocolBufIdx] = '\0';
        DBG_PRINT("PROTOCOL:RX:"); DBG_PRINTLN(protocolBuf);
        
        // Special command: MODE:ROTATOR (force rotator control mode)
        if (strncmp(protocolBuf, "MODE:ROTATOR", 12) == 0) {
          stopAll();
          exitCalibrationMode();
          currentMenu = nullptr;
          Serial.println("MODE:ROTATOR");
          protocolBufIdx = 0;
          protocolLastCmd = millis();
          return;
        }
        
        if (protocolBuf[0] == '\\') {
          processEasycomCommand(protocolBuf, protocolBufIdx);
        } else if (protocolBufIdx == 1 && protocolBuf[0] >= 'A' && protocolBuf[0] <= 'Z') {
          // Single uppercase letter - GS-232
          processGs232Command(protocolBuf, protocolBufIdx);
        } else if (protocolBufIdx == 1 && protocolBuf[0] >= 'a' && protocolBuf[0] <= 'z') {
          // Single lowercase letter - treat as case-insensitive GS-232
          processGs232Command(protocolBuf, protocolBufIdx);
        } else if (protocolBufIdx > 1) {
          // Multi-letter: check if it's a GS-232 command with params (Xnnn, Tnnn, Pnnn, Nnnn, Fnnn)
          char first = toupper(protocolBuf[0]);
          if (first == 'X' || first == 'T' || first == 'P' || first == 'N' || first == 'F') {
            // GS-232 command with parameters
            processGs232Command(protocolBuf, protocolBufIdx);
          } else {
            // Easycom command (AZ, EL, AZEL, CW, CCW, UP, DOWN, STOP, etc.)
            processEasycomCommand(protocolBuf, protocolBufIdx);
          }
        } else {
          protocolSend("ERR");
        }
        
        protocolBufIdx = 0;
        protocolLastCmd = millis();
      }
    } else {
      if (protocolBufIdx < PROTOCOL_BUF_SIZE - 1) {
        protocolBuf[protocolBufIdx++] = ch;
      }
    }
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
      if (strncmp(modeBuf, "MODE:ROTATOR", 12) == 0) {
        stopAll();
        exitCalibrationMode();
        currentMenu = nullptr;
        Serial.println("MODE:ROTATOR");
        modeIdx = 0;
        return true;
      }
      modeIdx = 0;
    } else if (modeIdx < 31) {
      modeBuf[modeIdx++] = ch;
    }
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
  const unsigned long ADC_INTERVAL = 200;
  
  if (now - lastAzRead >= ADC_INTERVAL) {
    lastAzRead = now;
    int16_t adcAz = ads.readADC_SingleEnded(0);
    adcAz = constrain(adcAz, 0, 32767);
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
    int16_t adcEl = ads.readADC_SingleEnded(1);
    adcEl = constrain(adcEl, 0, 32767);
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
MenuItem bootItems[] = {
  {"Rotator Ctrl", actionBootSkip, 3, true, nullptr, nullptr},    // 3s timeout auto-select
  {"Setup", actionBootSetup, 0, true, nullptr, nullptr}
};

// Setup Menu
char setupRotatorLabel[24] = "Rot Variant";
char setupCalLabel[24] = "Rot Calibrate";
char setupWifiLabel[24] = "Wifi(None)";

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
  {setupWifiLabel, actionSetupWifi, 0, true, nullptr, nullptr},
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

  // WiFi mode label
  if (deviceWifiMode == MODE_STA) {
    strcpy(setupWifiLabel, "Wifi(Station)");
  } else if (deviceWifiMode == MODE_AP) {
    strcpy(setupWifiLabel, "Wifi(AP)");
  } else if (EEPROM.read(WIFI_MODE_MAGIC) == WIFI_MODE_MAGIC_VAL) {
    strcpy(setupWifiLabel, "Wifi(Disabled)");
  } else {
    strcpy(setupWifiLabel, "Wifi(None)");
  }
  setupItems[2].label = setupWifiLabel;

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
  {"AP Mode (Field)", actionApMode, 0, true, nullptr, nullptr},
  {"Rotator Model", actionRotatorModel, 0, true, nullptr, nullptr}
};

// AP Mode Menu (shown when STA fails or user selects field mode)
MenuItem apModeItems[] = {
  {"Rotator Control", actionRotatorControl, 5, true, nullptr, nullptr},
  {"AP Status", actionApStatus, 0, true, nullptr, nullptr},
  {"Retry Home WiFi", actionRetryWifi, 0, true, nullptr, nullptr},
  {"Calibrate Pots", actionCalibratePots, 0, true, nullptr, nullptr}
};

// Voltage Calibration Menu
MenuItem calPotsItems[] = {
  {"Azimuth", actionCalibrateAz, 0, true, nullptr, nullptr},
  {"Elevation", actionCalibrateEl, 0, true, nullptr, nullptr}
};

// Menu instances
Menu bootMenu("Boot", bootItems, 2, nullptr);
Menu setupMenu("SETUP", setupItems, 4, &bootMenu);
Menu wifiModeMenu("WiFi Mode", wifiModeItems, 3, &setupMenu);
Menu wifiConnectMenu("WiFi Connect", wifiConnectItems, 5, nullptr);
Menu apModeMenu("AP Mode", apModeItems, 4, nullptr);
Menu calPotsMenu("Voltage Calibration", calPotsItems, 2, &setupMenu);

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
    AP_SSID,
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
    // Show connected screen with signal strength
    int rssi = WiFi.RSSI();
    String quality;
    if (rssi >= -50) quality = "Excellent";
    else if (rssi >= -60) quality = "Good";
    else if (rssi >= -70) quality = "Fair";
    else quality = "Poor";
    
    String rssiLine = String(rssi) + " dBm";
    Menu::drawQuickNotice("Connected", rssiLine.c_str(), quality.c_str(), true);
    
    // Wait 5 seconds
    for (int i = 0; i < 50; i++) {
      ESP.wdtFeed();
      delay(100);
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
    String apSsidLine = "SSID: " + String(AP_SSID);
    String apPassLine = "Pass: " + String(AP_PASS);
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
  Serial.println("Action: Rotator Control");
  // Stop any active movement before switching to rotator mode
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
  
  // Keep String objects alive to avoid dangling pointers
  String ssidLine = "SSID: " + String(AP_SSID);
  String passLine = "Pass: " + String(AP_PASS);
  String ipLine = "IP: " + String(AP_IP);
  const char* lines[] = {
    "AP Mode Active",
    "",
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

void actionBootSkip() {
  Serial.println("BOOT: Rotator Ctrl selected");
  // Apply saved WiFi mode
  if (deviceWifiMode == MODE_STA && hasWifiCredentials()) {
    connectWifiSta();
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
  // Go back to setup menu
  updateSetupLabels();
  updateSetupTimers();
  currentMenu = &setupMenu;
  currentMenu->init();
  skipBtnProcessing = true;  // Skip next button cycle — Btn1 may still be held from long-press
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
      for (int i = 0; i < CAL_EEPROM_SIZE; i++) {
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
  deviceWifiMode = MODE_STA;
  saveWifiMode();
  // Show Station Settings screen
  showStationSettings();
  // Back from station settings — return to setup
  updateSetupLabels();
  updateSetupTimers();
  currentMenu = &setupMenu;
  currentMenu->init();
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

      // Password info
      display.setCursor(2, 44);
      if (savedPass.length() > 1) {
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
            AP_SSID,
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
              showStationSettings();
              return;
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
const char* directions[7] = {"IDLE", "LEFT", "RIGHT", "UP", "DOWN", "AZ POT", "EL POT"};

void updateRotatorDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  
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
    
    display.print(adsPresent ? String(displayAzimuth(currentAz), 1) + "d" : "N/A");
    display.print(" El: ");
    display.println(adsPresent ? String(currentEl, 1) + "d" : "N/A");
    
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
      azVal = String(displayAzimuth(currentAz), 1);
      elVal = String(currentEl, 1);
      // Left-pad to 6 chars so decimal points align (max "-180.0")
      while (azVal.length() < 6) azVal = " " + azVal;
      while (elVal.length() < 6) elVal = " " + elVal;
    }
    
    // Az line
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("Az:");
    if (adsPresent) {
      display.print(azVal);
      // Movement indicator — draw arrow near right edge
      int ax = 114;  // Near right edge (128px - 9px arrow - 2px margin)
      if (currentMove.compareTo("RIGHT") == 0 || (currentMove.compareTo("AZ POT") == 0 && calDirection > 0)) {
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
      } else if (currentMove.compareTo("LEFT") == 0 || (currentMove.compareTo("AZ POT") == 0 && calDirection < 0)) {
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
      if (currentMove.compareTo("UP") == 0 || (currentMove.compareTo("EL POT") == 0 && calDirection > 0)) {
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
      } else if (currentMove.compareTo("DOWN") == 0 || (currentMove.compareTo("EL POT") == 0 && calDirection < 0)) {
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
    
    // Moving/Selected just above footer
    display.setTextSize(1);
    display.setCursor(0, 40);
    if (currentMove.compareTo("IDLE") != 0) {
      display.print("Moving: ");
      display.println(currentMove);
    } else {
      display.print("Selected: ");
      display.println(directions[rotatorMenuSelection]);
    }
    
    // Dotted separator above footer
    for (int x = 0; x < 128; x += 3) {
      display.drawPixel(x, 53, SSD1306_WHITE);
    }
    display.setCursor(0, 56);
    display.println("1.Cycle 2!Go I2!1|RST");
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
      if (now - btn2PressStart > BTN_DEBOUNCE_MS || btn2PressStart == 0) {
        btn2PressStart = now;
        skipBtnProcessing = true;  // Skip next cycle so held Btn2 doesn't trigger again
        // Disable Back on SETUP menu when mandatory items incomplete
        if (currentMenu == &setupMenu && !allSetupComplete()) {
          // Do nothing — Back is disabled until all mandatory setup is done
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
          calLastHeadingIndex = 0;
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
    btn1PressStart = now;
    btn1LongPressTriggered = false;
  }
  
  // Long press Btn1 while Btn2 held + IDLE selected = restart
  if (btn1 && (now - btn1PressStart >= BTN_LONG_MS) && !btn1LongPressTriggered) {
    if (rotatorMenuSelection == 0 && btn2Physical) {
      btn1LongPressTriggered = true;
      Serial.println("RESTART: Btn2 held + Btn1 long-press - restarting from scratch");
      Menu::drawQuickNotice("Restarting", "...", "", true);
      delay(1000);
      ESP.restart();
    }
  }
  
  // Short press Btn1 = cycle directions (only if not long-pressed)
  if (!btn1 && btn1WasPressed && !btn1LongPressTriggered && (now - btn1PressStart < BTN_LONG_MS)) {
    if (now - btn1LastPress > BTN_DEBOUNCE_MS) {
      rotatorMenuSelection = (rotatorMenuSelection + 1) % 5;
      btn1LastPress = now;
      Serial.print("Menu: ");
      Serial.println(directions[rotatorMenuSelection]);
    }
  }
  btn1WasPressed = btn1;
  
  // Btn2: press = start, release = stop (momentary)
  if (btn2 && !btn2WasPressed) {
    // Button just pressed - start movement
    if (!isPausing) {
      if (rotatorMenuSelection == 1) startMove("LEFT");
      else if (rotatorMenuSelection == 2) startMove("RIGHT");
      else if (rotatorMenuSelection == 3) startMove("UP");
      else if (rotatorMenuSelection == 4) startMove("DOWN");
      else if (rotatorMenuSelection == 5) enterCalibrationMode(0);  // AZ
      else if (rotatorMenuSelection == 6) enterCalibrationMode(1);  // EL
    }
  }
  
  if (!btn2 && btn2WasPressed) {
    // Button just released - stop movement
    stopAll();
  }
  
  btn2WasPressed = btn2;
}

// ====================== WEB SERVER ======================
String webLastMove = "";
unsigned long webMoveTime = 0;

void setupWebServer() {
  server.on("/", []() {
    sendProgmemHTML(html_rotator);
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
  
  server.on("/btn", []() {
    String json = "{";
    json += "\"btn2\":" + String(btn2Physical ? "true" : "false") + ",";
    json += "\"move\":\"" + currentMove + "\",";
    json += "\"webMove\":\"" + webLastMove + "\"";
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  
  server.begin();
  Serial.println("HTTP server started on port 80");
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  delay(200);
  
  Serial.println("\n========================================");
  Serial.println("  Yaesu Rotator Controller");
  Serial.println("========================================");
  
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
  EEPROM.begin(CAL_EEPROM_SIZE);
  loadWifiCredentials();
  loadRotatorModel();
  loadCalibration();
  Serial.print("Saved SSID: ");
  Serial.println(savedSsid.length() > 0 ? savedSsid : "(none)");
  
  // Update menu visibility based on credentials
  wifiConnectItems[2].visible = hasWifiCredentials();
  
  // Boot screen
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("AZELCTRL");
  display.println("----------");
  display.println("Booting...");
  display.display();
  delay(1000);
  
  // Load saved WiFi mode
  loadWifiMode();

  // Update menu visibility based on credentials
  wifiConnectItems[2].visible = hasWifiCredentials();

  // Boot screen
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("AZELCTRL");
  display.println("----------");
  display.println("Booting...");
  display.display();
  delay(1000);

  // If mandatory settings missing, show "Missing Settings" then go straight to setup
  if (!allSetupComplete()) {
    Serial.println("BOOT: Mandatory settings incomplete - showing missing settings notice");
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
  // Handle web server
  if (!captivePortalActive) {
    server.handleClient();
  } else {
    tickCaptivePortal();
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
    const unsigned long UPDATE_INTERVAL = 400;
    
    if (millis() - lastUpdate >= UPDATE_INTERVAL) {
      readPositions();
      updateRotatorDisplay();
      lastUpdate = millis();
    }
  }
  
  delay(10);
}
