/* ESP8266 Admin Portal - AP captive + STA LAN access (with mDNS)
   - If credentials saved and connect succeeds -> starts admin web server on STA IP
   - If not -> starts AP + captive portal (DNS redirect)
   - Access via IP (http://192.168.x.x/) or mDNS (http://<devname>.local/) if supported
*/
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <Servo.h>

#define EEPROM_SIZE 512
#define MAGIC_ADDR 0
#define SSID_ADDR 1
#define PASS_ADDR 33
#define DEVNAME_ADDR 97
#define LED_PIN 2

const byte DNS_PORT = 53;

SoftwareSerial nanoSerial(14, 15); // D5=TX to Nano RX, D6=RX from Nano

DNSServer dnsServer;
ESP8266WebServer server(80);

const char* apSSID = "ESP_Setup_AP";
const char* apPass = "";

const int RESET_BUTTON_PIN = 3; // hold low to force AP
bool forceAP = false;
int coin = 0;
int pos = 0;

struct StoredCreds {
  char ssid[32];
  char pass[64];
  char devName[64];
};
StoredCreds creds;

void clearCredsInMemory() { memset(&creds, 0, sizeof(creds)); }
bool connectAndStartSTA(unsigned long timeoutMs = 15000);

bool readCredsFromEEPROM() {
  if (EEPROM.read(MAGIC_ADDR) != 0x42) return false;
  for (int i=0;i<32;i++) creds.ssid[i] = EEPROM.read(SSID_ADDR + i);
  for (int i=0;i<64;i++) creds.pass[i] = EEPROM.read(PASS_ADDR + i);
  for (int i=0;i<64;i++) creds.devName[i] = EEPROM.read(DEVNAME_ADDR + i);
  creds.ssid[31] = 0; creds.pass[63] = 0; creds.devName[63] = 0;
  return true;
}

void saveCredsToEEPROM(const StoredCreds &c) {
  EEPROM.write(MAGIC_ADDR, 0x42);
  for (int i=0;i<32;i++){
    char ch = (i < (int)strlen(c.ssid)) ? c.ssid[i] : 0;
    EEPROM.write(SSID_ADDR + i, ch);
  }
  for (int i=0;i<64;i++){
    char ch = (i < (int)strlen(c.pass)) ? c.pass[i] : 0;
    EEPROM.write(PASS_ADDR + i, ch);
  }
  for (int i=0;i<64;i++){
    char ch = (i < (int)strlen(c.devName)) ? c.devName[i] : 0;
    EEPROM.write(DEVNAME_ADDR + i, ch);
  }
  EEPROM.commit();
}

void eraseSavedCreds() {
  EEPROM.write(MAGIC_ADDR, 0xFF);
  EEPROM.commit();
  clearCredsInMemory();
}

String portalPage(String curSsid, String curPass, String curDev) {
  String s="";
  s += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>";
  s += "<title>ESP Admin</title><style>body{font-family:Arial;padding:10px;}input{width:100%;padding:8px;margin:6px 0}button{padding:10px;width:100%}</style></head><body>";
  s += "<h2>ESP Admin Portal</h2>";
  s += "<form method='POST' action='/save'>";
  s += "<label>WiFi SSID</label>";
  s += "<input name='ssid' placeholder='SSID' value='" + (curSsid.length() ? curSsid : "") + "' required/>";
  s += "<label>WiFi Password</label>";
  s += "<input name='pass' type='password' placeholder='Password' value='" + (curPass.length() ? curPass : "") + "' />";
  s += "<label>Device Name (for LAN access)</label>";
  s += "<input name='devname' placeholder='esp-device' value='" + (curDev.length() ? curDev : "") + "' />";
  s += "<button type='submit'>Save & Connect</button></form><hr>";
  s += "<p>Tip: if device already connected to your router, access it at <b>http://" + String(WiFi.localIP().toString()) + "/</b> or <b>http://" + String(curDev.length() ? curDev : "esp-device") + ".local/</b> (if your PC/phone supports mDNS).</p>";
  s += "<p><a href='/erase'>Erase saved credentials</a></p></body></html>";
  return s;
}

void startAPandCaptive() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPass);
  IPAddress apIP = WiFi.softAPIP(); // usually 192.168.4.1
  Serial.print("AP IP: "); Serial.println(apIP);
  dnsServer.start(DNS_PORT, "*", apIP);

  // Web handlers (same handlers used for STA mode)
  server.on("/", HTTP_GET, [](){
    String html = portalPage(String(creds.ssid), String(creds.pass), String(creds.devName));
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, [](){
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    String dev  = server.arg("devname");

    ssid = ssid.substring(0, 31);
    pass = pass.substring(0, 63);
    dev  = dev.substring(0, 63);

    memset(&creds, 0, sizeof(creds));
    ssid.toCharArray(creds.ssid, sizeof(creds.ssid));
    pass.toCharArray(creds.pass, sizeof(creds.pass));
    dev.toCharArray(creds.devName, sizeof(creds.devName));

    saveCredsToEEPROM(creds);

    String body = "<h3>Saved. Trying to connect to network...</h3><p>Check Serial Monitor for status.</p>";
    body += "<p><a href='/'>Back</a></p>";
    server.send(200, "text/html", body);
    delay(300);
    // stop AP components so radios free for STA connect
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    server.close(); // stop to re-init below
    // try connect
    if (connectAndStartSTA()) {
      // connected - server already started in STA mode inside function
    } else {
      // failed -> restart AP captive
      delay(500);
      startAPandCaptive();
    }
  });

  server.on("/erase", HTTP_GET, [](){
    eraseSavedCreds();
    String b = "<h3>Saved credentials erased.</h3><p>Reboot the device to start AP again.</p>";
    server.send(200, "text/html", b);
  });

  server.onNotFound([](){
    // redirect all requests to root to create captive effect
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("Captive portal started (AP mode).");
}

void startServerHandlers() {
  // Define or re-define handlers (safe to call both modes)
  server.on("/", HTTP_GET, [](){
    String html = portalPage(String(creds.ssid), String(creds.pass), String(creds.devName));
    server.send(200, "text/html", html);
  });

  server.on("/erase", HTTP_GET, [](){
    eraseSavedCreds();
    String b = "<h3>Saved credentials erased.</h3><p>Reboot device to start AP again.</p>";
    server.send(200, "text/html", b);
  });

  server.on("/save", HTTP_POST, [](){
    // same as AP handler but in STA mode we can reuse code: save & reboot into STA connect
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    String dev  = server.arg("devname");

    ssid = ssid.substring(0, 31);
    pass = pass.substring(0, 63);
    dev  = dev.substring(0, 63);

    memset(&creds, 0, sizeof(creds));
    ssid.toCharArray(creds.ssid, sizeof(creds.ssid));
    pass.toCharArray(creds.pass, sizeof(creds.pass));
    dev.toCharArray(creds.devName, sizeof(creds.devName));
    saveCredsToEEPROM(creds);

    String body = "<h3>Saved. Reconnecting...</h3><p>Device will try to reconnect with new credentials.</p>";
    server.send(200, "text/html", body);
    delay(300);
    // reconnect logic - simple restart
    ESP.restart();
  });

  // other API endpoints for your device can be added here
  server.on("/convertToCash", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST,GET, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  // Handle /convertToCash
  server.on("/convertToCash", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
   
    // Check kung naa ba ang payload "convertCash"
    if (server.hasArg("convertCash")) {
      String scoin = server.arg("convertCash");
      
      coin = scoin.toInt();
      Serial.println(scoin);
      sendToNano(coin);
      // Pasigahun ang LED
      digitalWrite(LED_PIN, LOW); // Active-LOW LED ON
      // Send response balik sa AJAX
      String res = "{\"status\":\"success\",\"message\":\"LED turned ON\",\"coin\":\"" + scoin + "\"}";
      server.send(200, "application/json", res);
    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing convertCash\"}");
    }
  });
}


bool connectAndStartSTA(unsigned long timeoutMs) {

IPAddress local_IP(10, 0, 0, 64);
IPAddress gateway(10, 0, 0, 1);       // Usually router IP
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

WiFi.config(local_IP, gateway, subnet, dns);
Serial.println(WiFi.macAddress());
  if (strlen(creds.ssid) == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(creds.ssid, creds.pass);
  Serial.printf("Trying to connect to SSID '%s' ...\n", creds.ssid);
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    delay(250);
    Serial.print(".");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected! IP: " + WiFi.localIP().toString());
      // start mDNS with device name (fallback if empty)
      String dev = String(creds.devName);
      if (dev.length() == 0) dev = "esp-device";
      if (MDNS.begin(dev.c_str())) {
        Serial.printf("mDNS responder started: http://%s.local/\n", dev.c_str());
        MDNS.addService("http", "tcp", 80);
        // Pasigahun ang LED
        digitalWrite(LED_PIN, LOW); // Active-LOW LED ON
      } else {
        Serial.println("mDNS failed to start (may not be supported on network).");
      }
      // start webserver handlers and server
      startServerHandlers();
      server.begin();
      Serial.println("Admin portal available on LAN.");
      Serial.printf("Access at: http://%s/  or  http://%s.local/\n", WiFi.localIP().toString().c_str(), dev.c_str());
      return true;
    }
  }
  Serial.println("\nConnection timeout/failed.");
  return false;
}

void sendToNano(int value) {
  nanoSerial.println(value); // send number to Nano  // send number with newline
}


void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(9600);
  delay(50);


  Serial.println("\n=== ESP Admin Portal Boot ===");
  digitalWrite(LED_PIN, HIGH);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    Serial.println("Reset button held -> forcing AP mode");
    forceAP = true;
  }

  EEPROM.begin(EEPROM_SIZE);
  clearCredsInMemory();
  bool have = readCredsFromEEPROM();
  if(have) {
    Serial.printf("Found saved SSID: '%s' DevName: '%s'\n", creds.ssid, creds.devName);
  } else {
    Serial.println("No saved credentials found.");
  }

  if (!forceAP && have) {
    if (connectAndStartSTA(10000)) {
      // Connected and server running on STA IP
      return;
    } else {
      Serial.println("Saved creds failed -> starting AP captive.");
      delay(300);
      startAPandCaptive();
    }
  } else {
    startAPandCaptive();
  }
}

void loop() {
  // Handle whichever network mode is active
  if (WiFi.getMode() == WIFI_AP) {
    dnsServer.processNextRequest();
    server.handleClient();
  } else {
    // STA mode: handle web server and mDNS updates
    server.handleClient();
    MDNS.update();
  }

}

