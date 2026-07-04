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
DeviceWifiMode deviceWifiMode = MODE_STA;

// ==================== MENU SYSTEM ====================
typedef void (*MenuAction)();

struct MenuItem {
  const char* label;
  MenuAction action;
  int timerSeconds;     // 0 = no timer, >0 = countdown
  bool visible;         // Dynamic visibility
};

class Menu {
public:
  Menu(const char* title, MenuItem* items, int itemCount, Menu* parent = nullptr)
    : title(title), items(items), itemCount(itemCount), parent(parent),
      cursor(0), timerStart(0), timerActive(false), userInteracted(false) {}

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
      items[cursor].action();
    }
  }

  void back() {
    userInteracted = true;
    timerActive = false;
    if (parent) {
      parent->cursor = cursor;
      parent->draw();
    } else {
      init();
    }
  }
  
  Menu* getParent() {
    return parent;
  }

  void tick() {
    // Don't auto-fire if user has interacted
    if (timerActive && !userInteracted && cursor >= 0 && cursor < itemCount) {
      int remaining = items[cursor].timerSeconds - (int)((millis() - timerStart) / 1000);
      if (remaining <= 0) {
        timerActive = false;
        if (items[cursor].visible && items[cursor].action) {
          items[cursor].action();
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
    display.setCursor(1, 0);
    String titleLine = " " + String(title) + " ";
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
        line += " [" + String(timerRemaining()) + "s]";
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
    display.println("1.Nav 1|Sel 2.Back");
    
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
    display.setCursor(1, 0);
    String titleLine = " " + String(title) + " ";
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
<p>The device will now restart its menu.</p>
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
    Serial.println("Rebooting after WiFi save...");
    ESP.restart();
    // Does not return
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
#define ROT_EEPROM_FACING    86
#define ROT_EEPROM_EXTENDED  87
#define ROT_EEPROM_MAGIC     88
#define ROT_EEPROM_MAGIC_VAL 0x5A
#define CAL_EEPROM_SIZE      89

// Rotator model
enum RotFacing { FACING_NORTH, FACING_SOUTH };
enum RotExtended { EXT_NO, EXT_YES };

RotFacing rotFacing = FACING_NORTH;
RotExtended rotExtended = EXT_NO;

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
  CAL_CHECK_STABILITY,  // Measure voltage for 1s to detect unplugged cable
  CAL_SWEEP_MIN,        // Move CCW/DOWN until voltage stabilizes
  CAL_WAIT_TIMEOUT_MIN, // Timeout waiting for min - ask user
  CAL_SWEEP_MAX,        // Move CW/UP until voltage stabilizes or hits safety limit
  CAL_WAIT_TIMEOUT_MAX, // Timeout waiting for max - ask user
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

// Voltage samples for stability detection
float calVoltageSamples[CAL_V_SAMPLES];
int calVSampleIdx = 0;

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
    Serial.println("WIFI: No WiFi mode in EEPROM - defaulting to STA");
    deviceWifiMode = MODE_STA;
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
  rotFacing = (RotFacing)EEPROM.read(ROT_EEPROM_FACING);
  rotExtended = (RotExtended)EEPROM.read(ROT_EEPROM_EXTENDED);
  Serial.print("ROT: Loaded model - Facing=");
  Serial.println(rotFacing == FACING_NORTH ? "NORTH" : "SOUTH");
  Serial.print("ROT: Extended=");
  Serial.println(rotExtended == EXT_YES ? "YES (0-450)" : "NO");
}

void saveRotatorModel() {
  EEPROM.write(ROT_EEPROM_FACING, (uint8_t)rotFacing);
  EEPROM.write(ROT_EEPROM_EXTENDED, (uint8_t)rotExtended);
  EEPROM.write(ROT_EEPROM_MAGIC, ROT_EEPROM_MAGIC_VAL);
  EEPROM.commit();
  Serial.println("ROT: Rotator model saved to EEPROM");
}

String azRangeLabel() {
  if (rotFacing == FACING_NORTH) return "-180 to +180";
  if (rotExtended == EXT_YES) return "0 to 450";
  return "0 to 360";
}

float azSpan() {
  if (rotFacing == FACING_NORTH) return 360.0F;
  if (rotExtended == EXT_YES) return 450.0F;
  return 360.0F;
}

float azOffset() {
  if (rotFacing == FACING_NORTH) return -180.0F;
  return 0.0F;
}

void setupRotatorModel() {
  Serial.println("ROT: Starting rotator model setup");

  // States: 0=Q1 Facing, 1=Q2 Extended (if South), 2=Confirm
  int state = 0;
  int facingChoice = 0;  // 0=N, 1=S
  int extChoice = 0;     // 0=No, 1=Yes
  unsigned long lastBtn1 = 0;
  unsigned long lastBtn2 = 0;

  while (true) {
    lastBtn1 = 0;
    lastBtn2 = 0;

    // Wait for both buttons to be released before drawing new screen
    while (digitalRead(BTN1) || !digitalRead(BTN2)) delay(50);
    delay(100);

    if (state == 0) {
      // --- Q1: Az Orientation ---
      facingChoice = (rotFacing == FACING_NORTH) ? 0 : 1;

      while (true) {
        display.clearDisplay();
        display.setTextSize(1);

        display.fillRect(0, 0, 128, 8, SSD1306_BLACK);
        display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
        display.setCursor(1, 0);
        display.print(" ROTATOR SETUP              ");

        display.drawLine(0, 8, 127, 8, SSD1306_WHITE);

        display.setTextColor(SSD1306_WHITE);
        display.setCursor(2, 10);
        display.println("Az Orientation ?");

        int y = 24;
        for (int i = 0; i < 2; i++) {
          if (i == facingChoice) {
            display.fillRect(0, y, 128, 8, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          } else {
            display.setTextColor(SSD1306_WHITE);
          }

          display.setCursor(2, y);
          if (i == 0) {
            display.print("N  (-180 to +180)");
          } else {
            display.print("S  (0 to 360)");
          }
          y += 11;
        }

        for (int x = 0; x < 128; x += 3) {
          display.drawPixel(x, 53, SSD1306_WHITE);
        }

        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 56);
        display.println("1.Cycle  2.Select");

        display.display();

        unsigned long now = millis();

        if (digitalRead(BTN1) && now - lastBtn1 >= BTN_DEBOUNCE_MS) {
          lastBtn1 = now;
          facingChoice = 1 - facingChoice;
          Serial.print("ROT: Facing choice = ");
          Serial.println(facingChoice == 0 ? "NORTH" : "SOUTH");
        }

        if (!digitalRead(BTN2) && now - lastBtn2 >= BTN_DEBOUNCE_MS) {
          lastBtn2 = now;
          rotFacing = (RotFacing)facingChoice;
          Serial.println("ROT: Facing confirmed");
          while (!digitalRead(BTN2)) delay(50);
          break;
        }

        delay(50);
      }

      // Move to Q2 if South, else to Confirm
      if (rotFacing == FACING_SOUTH) {
        state = 1;
      } else {
        extChoice = 0;
        rotExtended = EXT_NO;
        state = 2;
      }

    } else if (state == 1) {
      // --- Q2: Extended Range? ---
      extChoice = (rotExtended == EXT_NO) ? 0 : 1;

      while (true) {
        display.clearDisplay();
        display.setTextSize(1);

        display.fillRect(0, 0, 128, 8, SSD1306_BLACK);
        display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
        display.setCursor(1, 0);
        display.print(" ROTATOR SETUP              ");

        display.drawLine(0, 8, 127, 8, SSD1306_WHITE);

        display.setTextColor(SSD1306_WHITE);
        display.setCursor(2, 10);
        display.println("Extended range?");

        int y = 24;
        for (int i = 0; i < 2; i++) {
          if (i == extChoice) {
            display.fillRect(0, y, 128, 8, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          } else {
            display.setTextColor(SSD1306_WHITE);
          }

          display.setCursor(2, y);
          if (i == 0) {
            display.print("No  (0 to 360)");
          } else {
            display.print("Yes (0 to 450)");
          }
          y += 11;
        }

        for (int x = 0; x < 128; x += 3) {
          display.drawPixel(x, 53, SSD1306_WHITE);
        }

        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 56);
        display.println("1.Cycle  2.Done");

        display.display();

        unsigned long now = millis();

        if (digitalRead(BTN1) && now - lastBtn1 >= BTN_DEBOUNCE_MS) {
          lastBtn1 = now;
          extChoice = 1 - extChoice;
          Serial.print("ROT: Extended choice = ");
          Serial.println(extChoice == 0 ? "NO" : "YES");
        }

        if (!digitalRead(BTN2) && now - lastBtn2 >= BTN_DEBOUNCE_MS) {
          lastBtn2 = now;
          rotExtended = (RotExtended)extChoice;
          Serial.println("ROT: Extended confirmed");
          while (!digitalRead(BTN2)) delay(50);
          break;
        }

        delay(50);
      }

      state = 2;

    } else if (state == 2) {
      // --- Confirm ---
      saveRotatorModel();

      while (true) {
        display.clearDisplay();
        display.setTextSize(1);

        display.fillRect(0, 0, 128, 8, SSD1306_BLACK);
        display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
        display.setCursor(1, 0);
        display.print(" ROTATOR SETUP              ");

        display.drawLine(0, 8, 127, 8, SSD1306_WHITE);

        display.setTextColor(SSD1306_WHITE);
        display.setCursor(2, 10);
        display.print("Facing: ");
        display.println(rotFacing == FACING_NORTH ? "NORTH" : "SOUTH");
        display.setCursor(2, 21);
        display.print("Range:  ");
        display.println(azRangeLabel());

        for (int x = 0; x < 128; x += 3) {
          display.drawPixel(x, 53, SSD1306_WHITE);
        }

        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 56);
        display.println("1.OK  2.Retry");
        display.display();

        unsigned long now = millis();

        // Btn1 - OK, done
        if (digitalRead(BTN1) && now - lastBtn1 >= BTN_DEBOUNCE_MS) {
          lastBtn1 = now;
          Serial.println("ROT: Setup confirmed, proceeding to boot");
          while (digitalRead(BTN1)) delay(50);
          return;  // Exit setupRotatorModel entirely
        }

        // Btn2 - Retry: go back to Q1
        if (!digitalRead(BTN2) && now - lastBtn2 >= BTN_DEBOUNCE_MS) {
          lastBtn2 = now;
          Serial.println("ROT: Retrying from Q1");
          while (!digitalRead(BTN2)) delay(50);
          state = 0;
          break;
        }

        delay(50);
      }
    }
  }
}

void enterCalibrationMode(int axis) {
  calState = CAL_CHECK_STABILITY;
  calCurrentAxis = axis;
  calDirection = -1;  // Start CCW/DOWN
  calPhaseStart = millis();
  calMinVoltage = 0;
  calMaxVoltage = 0;
  calStabilityMax = 0;
  calStabilityMin = 32767;
  calStabilityStart = millis();
  calVSampleIdx = 0;
  calErrorMessage = "";
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
    case CAL_CHECK_STABILITY:
      // Move motor in both directions to detect cable connection.
      // Phase 1: CCW/DOWN for 3s, Phase 2: CW/UP for 3s
      // If voltage doesn't change in either direction, cable is unplugged.
      if (calVSampleIdx <= 1) {
        // First iteration: capture initial voltage, start moving CCW
        calStabilityMax = vRaw;
        calStabilityMin = vRaw;
        setCalDirection(-1);
        Serial.println("CAL: Checking cable (moving CCW)...");
      }
      // Track voltage range
      if (vRaw > calStabilityMax) calStabilityMax = vRaw;
      if (vRaw < calStabilityMin) calStabilityMin = vRaw;
      
      // After 3s, reverse direction
      if (millis() - calPhaseStart >= 3000 && calDirection == -1) {
        setCalDirection(1);
        Serial.println("CAL: Checking cable (moving CW)...");
      }
      
      if (millis() - calPhaseStart >= CAL_DETECT_TIME) {
        // Check if voltage changed enough to confirm cable is connected
        float change = (calStabilityMax - calStabilityMin) * 1000;  // mV
        Serial.print("CAL: Voltage change = ");
        Serial.print(change);
        Serial.println(" mV");
        
        if (change < CAL_DETECT_MIN_CHANGE) {
          // Voltage didn't change in either direction — cable unplugged
          calState = CAL_ERROR;
          char buf[30];
          snprintf(buf, sizeof(buf), "dV (%.0fmV) too small", change);
          calErrorMessage = String(buf);
          stopAll();
          calDirection = 0;
          Serial.println("CAL: ERROR - No voltage change detected, check cable");
        } else {
          // Voltage changed — cable is connected, proceed to sweep min
          calState = CAL_SWEEP_MIN;
          calPhaseStart = millis();
          calVSampleIdx = 0;
          stopAll();
          calDirection = 0;
          // Start fresh moving CCW/DOWN
          setCalDirection(-1);
          Serial.println("CAL: Cable confirmed, starting sweep to minimum");
        }
      }
      break;
      
    case CAL_SWEEP_MIN:
      // Monitor for voltage stabilization or timeout
      if (isVoltageStable(vRaw)) {
        // Voltage stabilized - we hit the minimum limit
        calMinVoltage = vRaw;
        stopAll();
        calDirection = 0;
        Serial.print("CAL: Min voltage found: ");
        Serial.println(calMinVoltage, 3);
        
        // Proceed to sweep max
        calState = CAL_SWEEP_MAX;
        calPhaseStart = millis();
        calVSampleIdx = 0;
        setCalDirection(1);
        Serial.println("CAL: Starting sweep to maximum");
      } else if (millis() - calPhaseStart >= CAL_SWEEP_TIMEOUT) {
        // Timeout - ask user
        calState = CAL_WAIT_TIMEOUT_MIN;
        calMinVoltage = vRaw;  // Use current reading as best guess
        stopAll();
        calDirection = 0;
        Serial.println("CAL: Timeout sweeping to minimum");
      }
      break;
      
    case CAL_WAIT_TIMEOUT_MIN:
      // Wait for user input - handled in button handler
      break;
      
    case CAL_SWEEP_MAX:
      // Monitor for voltage stabilization, safety limit, or timeout
      if (vRaw >= CAL_MAX_SAFE_V) {
        // Hit safety voltage limit
        calMaxVoltage = vRaw;
        stopAll();
        calDirection = 0;
        Serial.print("CAL: Max voltage hit safety limit: ");
        Serial.println(calMaxVoltage, 3);
        
        // Save calibration
        saveCalibration();
        calState = CAL_COMPLETE;
      } else if (isVoltageStable(vRaw)) {
        // Voltage stabilized - we hit the maximum limit
        calMaxVoltage = vRaw;
        stopAll();
        calDirection = 0;
        Serial.print("CAL: Max voltage found: ");
        Serial.println(calMaxVoltage, 3);
        
        // Save calibration
        saveCalibration();
        calState = CAL_COMPLETE;
      } else if (millis() - calPhaseStart >= CAL_SWEEP_TIMEOUT) {
        // Timeout - ask user
        calState = CAL_WAIT_TIMEOUT_MAX;
        calMaxVoltage = vRaw;  // Use current reading as best guess
        stopAll();
        calDirection = 0;
        Serial.println("CAL: Timeout sweeping to maximum");
      }
      break;
      
    case CAL_WAIT_TIMEOUT_MAX:
      // Wait for user input - handled in button handler
      break;
      
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
  display.setCursor(1, 0);
  display.print("Cal-");
  display.print(calCurrentAxis == 0 ? "AZ:" : "EL:");
  
  // Status text right after prefix
  display.print(" ");
  switch (calState) {
    case CAL_CHECK_STABILITY: display.print("Check Cable"); break;
    case CAL_SWEEP_MIN:
      if (calCurrentAxis == 0) display.print("Sweep CCW");
      else display.print("Sweep DOWN");
      break;
    case CAL_WAIT_TIMEOUT_MIN: display.print("Timeout"); break;
    case CAL_SWEEP_MAX:
      if (calCurrentAxis == 0) display.print("Sweep CW");
      else display.print("Sweep UP");
      break;
    case CAL_WAIT_TIMEOUT_MAX: display.print("Timeout"); break;
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
    case CAL_CHECK_STABILITY:
      display.setCursor(2, y);
      if (calDirection == -1) {
        if (calCurrentAxis == 0) display.println("Trying CCW...");
        else display.println("Trying DOWN...");
      } else if (calDirection == 1) {
        if (calCurrentAxis == 0) display.println("Trying CW...");
        else display.println("Trying UP...");
      } else {
        display.println("Stopped");
      }
      break;
      
    case CAL_SWEEP_MIN:
      break;
      
    case CAL_WAIT_TIMEOUT_MIN:
      display.setCursor(2, y);
      display.println("Not at limit");
      break;
      
    case CAL_SWEEP_MAX:
      display.setCursor(2, y);
      display.print("Min: ");
      display.print(calMinVoltage, 3);
      display.println("V");
      break;
      
    case CAL_WAIT_TIMEOUT_MAX:
      display.setCursor(2, y);
      display.println("Not at limit");
      y += 11;
      display.setCursor(2, y);
      display.print("Min: ");
      display.print(calMinVoltage, 3);
      display.println("V");
      break;
      
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
    String azStr = String(currentAz, 1);
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
    String elStr = String(currentEl, 1);
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
  else if (calState == CAL_WAIT_TIMEOUT_MIN || calState == CAL_WAIT_TIMEOUT_MAX) display.println("1.Proceed 2.Cancel");
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
      currentEl = fraction * 90.0F;
    } else {
      float elFraction = (float)adcEl / 32767.0;
      currentEl = elFraction * 90.0;
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
void actionSetupWifi();
void actionWifiModeSta();
void actionWifiModeAp();
void actionWifiModeDisable();
void actionWifiModeSelect();

// Boot Menu (first screen after boot)
MenuItem bootItems[] = {
  {"Skip", actionBootSkip, 3, true},    // 3s timeout auto-select
  {"Setup", actionBootSetup, 0, true}
};

// Setup Menu
MenuItem setupItems[] = {
  {"Rotator", actionSetupRotator, 5, true},  // 5s timeout if not yet configured
  {"WiFi", actionSetupWifi, 0, true}
};

// WiFi Mode Menu
MenuItem wifiModeItems[] = {
  {"Station", actionWifiModeSta, 0, true},
  {"Access Point", actionWifiModeAp, 0, true},
  {"Disable", actionWifiModeDisable, 0, true}
};

// WiFi Connect Menu
MenuItem wifiConnectItems[] = {
  {"Set WiFi Creds", actionSetWifi, 0, true},
  {"Review Creds", actionReviewWifi, 0, true},
  {"Connect WiFi", actionConnectWifi, 5, false},  // Hidden until SSID saved
  {"AP Mode (Field)", actionApMode, 0, true},
  {"Rotator Model", actionRotatorModel, 0, true}
};

// WiFi Status Menu (shown after successful STA connection)
MenuItem wifiStatusItems[] = {
  {"Rotator Ctrl", actionRotatorControl, 5, true},
  {"Calibrate Pots", actionCalibratePots, 0, true}
};

// AP Mode Menu (shown when STA fails or user selects field mode)
MenuItem apModeItems[] = {
  {"Rotator Control", actionRotatorControl, 5, true},
  {"AP Status", actionApStatus, 0, true},
  {"Retry Home WiFi", actionRetryWifi, 0, true},
  {"Calibrate Pots", actionCalibratePots, 0, true}
};

// Calibrate Pots Menu
MenuItem calPotsItems[] = {
  {"Calibrate AZ Pot", actionCalibrateAz, 0, true},
  {"Calibrate EL Pot", actionCalibrateEl, 0, true}
};

// Menu instances
Menu bootMenu("Boot", bootItems, 2, nullptr);
Menu setupMenu("Setup", setupItems, 2, nullptr);
Menu wifiModeMenu("WiFi Mode", wifiModeItems, 3, &setupMenu);
Menu wifiConnectMenu("WiFi Connect", wifiConnectItems, 5, nullptr);
Menu wifiStatusMenu("Ctrl or Calibration?", wifiStatusItems, 2, nullptr);
Menu apModeMenu("AP Mode", apModeItems, 4, nullptr);
Menu calPotsMenu("Calibrate Pots", calPotsItems, 2, &wifiStatusMenu);

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
  
  while (true) {  // Wait indefinitely for user input
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
    
    currentMenu = &wifiStatusMenu;
    currentMenu->init();
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
  // Check which menu to return to
  if (wifiState == WIFI_AP_MODE || wifiState == WIFI_FAILED) {
    currentMenu = &apModeMenu;
  } else {
    currentMenu = &wifiStatusMenu;
  }
  currentMenu->init();
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
  
  while (true) {  // Wait indefinitely for user input
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
  String facingStr = rotFacing == FACING_NORTH ? "NORTH" : "SOUTH";
  String rangeStr = azRangeLabel();
  
  String facingLine = "Facing: " + facingStr;
  String rangeLine = "Range: " + rangeStr;
  
  const char* lines[] = {
    facingLine.c_str(),
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
    if (digitalRead(BTN1)) {
      delay(300);
      // Re-enter setup
      EEPROM.write(ROT_EEPROM_MAGIC, 0);
      EEPROM.commit();
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
  Serial.println("BOOT: Skip selected - going to main menu");
  // Apply saved WiFi mode
  if (deviceWifiMode == MODE_STA && hasWifiCredentials()) {
    connectWifiSta();
    currentMenu = &wifiStatusMenu;
  } else if (deviceWifiMode == MODE_AP) {
    startApMode();
    currentMenu = &apModeMenu;
  } else {
    // Disabled or no credentials
    currentMenu = &wifiStatusMenu;
  }
  currentMenu->init();
}

void actionBootSetup() {
  Serial.println("BOOT: Setup selected");
  // Auto-select Rotator if not yet configured
  if (!hasRotatorModel()) {
    setupItems[0].timerSeconds = 5;
  } else {
    setupItems[0].timerSeconds = 0;
  }
  currentMenu = &setupMenu;
  currentMenu->init();
}

void actionSetupRotator() {
  Serial.println("SETUP: Rotator setup");
  // Wait for button release after long press
  delay(300);
  while (digitalRead(BTN1) || !digitalRead(BTN2)) delay(50);
  setupRotatorModel();
  // Rotator is now configured - clear timer
  setupItems[0].timerSeconds = 0;
  // Go back to setup menu
  currentMenu = &setupMenu;
  currentMenu->init();
}

void actionSetupWifi() {
  Serial.println("SETUP: WiFi setup");
  currentMenu = &wifiModeMenu;
  currentMenu->init();
}

void actionWifiModeSta() {
  Serial.println("WIFI: Mode set to Station");
  deviceWifiMode = MODE_STA;
  saveWifiMode();
  // Now go to WiFi connect menu to configure credentials
  currentMenu = &wifiConnectMenu;
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
  deviceWifiMode = MODE_DISABLED;
  saveWifiMode();
  stopCaptivePortal();
  WiFi.disconnect(true);
  // Go back to setup menu
  currentMenu = &setupMenu;
  currentMenu->init();
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
    
    display.print(adsPresent ? String(currentAz, 1) + "d" : "N/A");
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
      azVal = String(currentAz, 1);
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
    display.println("1.Cycle 2!Go I2|1!RST");
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
    // Btn1: short-press = navigate, long-press = select
    if (btn1 && !btn1WasPressed) {
      btn1PressStart = now;
      btn1LongPressTriggered = false;
    }
    
    // Long press detected
    if (btn1 && (now - btn1PressStart >= BTN_LONG_MS) && !btn1LongPressTriggered) {
      btn1LongPressTriggered = true;
      currentMenu->select();
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
        currentMenu->back();
      }
    }
    btn2WasPressed = btn2;
    
    return;  // Exit - menu mode handles its own buttons
  }
  
  // ============================================
  // CALIBRATION MODE BUTTON HANDLING
  // ============================================
  if (calState != CAL_IDLE) {
    unsigned long now = millis();
    
    // Btn1: exit calibration, retry on error, or proceed on timeout
    if (btn1 && !btn1WasPressed && (now - btn1LastPress > BTN_DEBOUNCE_MS)) {
      if (calState == CAL_ERROR) {
        // Retry calibration
        calState = CAL_CHECK_STABILITY;
        calPhaseStart = millis();
        calVSampleIdx = 0;
        calDirection = 0;  // Reset so cable check starts fresh
        calMinVoltage = 0;
        calMaxVoltage = 0;
        calStabilityMax = 0;
        calStabilityMin = 32767;
        calErrorMessage = "";
      } else if (calState == CAL_WAIT_TIMEOUT_MIN) {
        // Proceed to sweep max with current reading as min
        calState = CAL_SWEEP_MAX;
        calPhaseStart = millis();
        calVSampleIdx = 0;
        setCalDirection(1);
      } else if (calState == CAL_WAIT_TIMEOUT_MAX) {
        // Proceed to complete with current reading as max
        saveCalibration();
        calState = CAL_COMPLETE;
      } else if (calState == CAL_COMPLETE || calState == CAL_CHECK_STABILITY || 
                 calState == CAL_SWEEP_MIN || calState == CAL_SWEEP_MAX) {
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
    
    // Btn2: stop movement, exit in terminal states, or cancel on timeout
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
      
      if (calState == CAL_WAIT_TIMEOUT_MIN || calState == CAL_WAIT_TIMEOUT_MAX) {
        // Cancel calibration
        exitCalibrationMode();
        currentMenu = &calPotsMenu;
        currentMenu->init();
        btn2WasPressed = btn2;
        return;
      }
      
      if (calState == CAL_SWEEP_MIN || calState == CAL_SWEEP_MAX) {
        // Stop movement during sweep
        stopAll();
        calDirection = 0;
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
    json += "\"az\":" + String(currentAz, 1) + ",";
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

  // If no settings at all, show "No Settings..." then go straight to setup
  if (!hasAnySettings()) {
    Serial.println("BOOT: No settings found - showing first-time notice");
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    // Center justify each line
    const char* line1 = "No";
    const char* line2 = "Settings!";
    int x1 = (int)max((int)0, (int)((128 - strlen(line1) * 12) / 2));
    int x2 = (int)max((int)0, (int)((128 - strlen(line2) * 12) / 2));
    display.setCursor(x1, 12);
    display.println(line1);
    display.setCursor(x2, 28);
    display.println(line2);
    display.display();
    delay(3000);

    // Go directly to setup menu with Rotator auto-timer
    setupItems[0].timerSeconds = 5;
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
