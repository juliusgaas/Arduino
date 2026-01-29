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
#include <time.h>

#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC (8 * 3600)
#define DAYLIGHT_OFFSET_SEC 0

#define EEPROM_SIZE 512
#define MAGIC_ADDR 0
#define SSID_ADDR 1
#define PASS_ADDR 33
#define DEVNAME_ADDR 97
#define LED_PIN 2

#define COIN_STOCK 98
#define COIN_LOG_ADDR 150
#define COIN_LOG_SIZE 300


#define ADMIN_USER "admin"
#define ADMIN_PASS "admin"   // pwede nimo ilisan

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

// ===== Active Users Storage =====
String activeUsersJSON = "{\"total\":0,\"users\":[]}";

struct StoredCreds {
  char ssid[32];
  char coin[32];
  char pass[64];
  char devName[64];
};

struct CoinLog {
  char date[11];   // YYYY-MM-DD
  int amount;      // converted coins
};

StoredCreds creds;

void clearCredsInMemory() { memset(&creds, 0, sizeof(creds)); }
bool connectAndStartSTA(unsigned long timeoutMs = 15000);

bool readCredsFromEEPROM() {
  if (EEPROM.read(MAGIC_ADDR) != 0x42) return false;
  for (int i=0;i<32;i++) creds.coin[i] = EEPROM.read(COIN_STOCK + i);
  for (int i=0;i<32;i++) creds.ssid[i] = EEPROM.read(SSID_ADDR + i);
  for (int i=0;i<64;i++) creds.pass[i] = EEPROM.read(PASS_ADDR + i);
  for (int i=0;i<64;i++) creds.devName[i] = EEPROM.read(DEVNAME_ADDR + i);
  creds.coin[31] = 0; creds.ssid[31] = 0; creds.pass[63] = 0; creds.devName[63] = 0;
  return true;
}

void saveCredsToEEPROM(const StoredCreds &c) {
  EEPROM.write(MAGIC_ADDR, 0x42);
  for (int i=0;i<32;i++){
    char ch = (i < (int)strlen(c.ssid)) ? c.ssid[i] : 0;
    EEPROM.write(SSID_ADDR + i, ch);
  }
  for (int i=0;i<32;i++){
    char ch = (i < (int)strlen(c.coin)) ? c.coin[i] : 0;
    EEPROM.write(COIN_STOCK + i, ch);
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

void saveCoinToEEPROM(const StoredCreds &c) {
  EEPROM.write(MAGIC_ADDR, 0x42);
  for (int i=0;i<32;i++){
    char ch = (i < (int)strlen(c.coin)) ? c.coin[i] : 0;
    EEPROM.write(COIN_STOCK + i, ch);
  }

  EEPROM.commit();
}

void eraseSavedCreds() {
  EEPROM.write(MAGIC_ADDR, 0xFF);
  EEPROM.commit();
  clearCredsInMemory();
}

String loginPage() {
  return R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP Login</title>
    <style>
    body{font-family:Arial;background:#f4f6f8}
    .card{max-width:320px;margin:80px auto;background:#fff;padding:20px;border-radius:8px}
    input,button{width:100%;padding:10px;margin:6px 0}
    button{background:#222;color:#fff;border:0}
    </style>
    </head>
    <body>
    <div class="card">
    <h3>ESP Admin Login</h3>
    <form method="POST" action="/login">
    <input name="user" placeholder="Username" required>
    <input name="pass" type="password" placeholder="Password" required>
    <button type="submit">Login</button>
    </form>
    </div>
    </body>
    </html>
    )rawliteral";
}


String portalPage(String curSsid, String curPass, String curDev, String curCoin) {
  String s="";
  s += "<!DOCTYPE html><html><head>";
  s += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  s += "<title>ESP Admin</title>";
  s += "<style>";
  s += "body{margin:0;font-family:Arial;background:#f4f6f8}input{width:100%;padding:8px;margin:6px 0}button{padding:10px;width:100%}";
  s += ".sidebar{width:220px;background:#222;color:#fff;height:100vh;position:fixed;padding:15px}";
  s += ".sidebar a{display:block;color:#fff;text-decoration:none;padding:10px;border-radius:4px;margin-bottom:6px}";
  s += ".sidebar a:hover{background:#444}";
  s += ".badge{float:right;background:red;color:#fff;padding:2px 8px;border-radius:12px;font-size:12px}";
  s += ".main{margin-left:240px;padding:15px}";
  s += ".card{background:#fff;padding:10px;border-radius:6px;margin-bottom:8px}";
  s += "<style>";
  s += "body{margin:0;font-family:Arial;background:#f4f6f8}";
  s += "input,button{width:100%;padding:10px;margin:6px 0;font-size:16px}";
  s += "button{border:0;border-radius:6px;background:#222;color:#fff}";
  s += ".sidebar{width:220px;background:#222;color:#fff;height:100vh;position:fixed;padding:15px}";
  s += ".sidebar a{display:block;color:#fff;text-decoration:none;padding:12px;border-radius:6px;margin-bottom:6px}";
  s += ".sidebar a:hover{background:#444}";
  s += ".badge{float:right;background:red;color:#fff;padding:2px 8px;border-radius:12px;font-size:12px}";
  s += ".main{margin-left:240px;padding:15px}";
  s += ".card{background:#fff;padding:15px;border-radius:10px;margin-bottom:10px;box-shadow:0 2px 6px rgba(0,0,0,.08)}";

  /* ===== MOBILE FIX ===== */
  s += "@media(max-width:768px){";
  s += ".sidebar{position:relative;width:100%;height:auto}";
  s += ".main{margin-left:0}";
  s += "}";
  s += "</style>";
  s += "</head><body>";

  s += "<div class='sidebar'>";
  s += "<h3>MENU</h3>";
  s += "<a href='/'>Dashboard</a>";
  s += "<a href='/dashboard'>Reports</a>";
  s += "<a href='/logout'>Logout</a>";
  s += "</div>";

  s += "<div class='main'>";
  //   curSsid.length() ? "" : 
  // s +=  "<div class='card'><form method='POST' action='/save'>";
  // s += "<label>WiFi SSID</label>";
  // s += "<input name='ssid' placeholder='SSID' value='" + (curSsid.length() ? curSsid : "") + "' required/>";
  // s += "<label>WiFi Password</label>";
  // s += "<input name='pass' type='password' placeholder='Password' value='" + (curPass.length() ? curPass : "") + "' />";
  // s += "<label>Device Name (for LAN access)</label>";
  // s += "<input name='devname' placeholder='esp-device' value='" + (curDev.length() ? curDev : "") + "' />";

  // s += "<button type='submit'>Save & Connect</button></form><hr>";
  // s += "<p>Tip: if device already connected to your router, access it at <b>http://" + String(WiFi.localIP().toString()) + "/</b> or <b>http://" + String(curDev.length() ? curDev : "esp-device") + ".local/</b> (if your PC/phone supports mDNS).</p>";
  // s += "<p><a href='/erase'>Erase saved credentials</a></p>"'
  //s += "</div>";

  s += "<div class='card'><form method='POST' action='/saveCoin'>";
  s += "<label>COIN STOCK</label>";
  s += "<input name='coin' placeholder='COIN STOCK' value='" + (curCoin.length() ? curCoin : "") + "' required/>";
  s += "<label>DATE</label>";
  s += "<input type='date' name='date' placeholder='DATE'  required/>";
  s += "<button type='submit'>Save</button></form><hr>";
  s += "</div>";

  s += "<div class='card'>";
  s += "<h1 id='cnt'></h1>";
  s += "<h3>Active Users</h3>";
  s += "</div>";

  s += "</div>";
  s += "<script>";
  s += "function load(){";
  s += "fetch('/data')";
  s += ".then(r=>r.json())";
  s += ".then(d=>{";
  s += "document.getElementById('cnt').innerHTML=d.total;";
  s += "let h='';";
  s += "d.users.forEach(u=>{";
  s += "h+=`<div class='card'><b>User:</b> ${u.user}<br><b>IP:</b> ${u.ip}<br><b>Time Left:</b> ${u.time}</div>`;";
  s += "});";
  s += "document.getElementById('list').innerHTML=h||'No active users';";
  s += "});";
  s += "}";
  s += "load();";
  s += "setInterval(load,3000);";
  s += "</script>";
  s += "</body></html>";

  return s;
}

String portalDashboardPage(){
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP Admin</title>
  <style>
    body{
      margin:0;
      font-family:Arial;
      background:#f4f6f8;
    }

    /* ===== SIDEBAR ===== */
    .sidebar{
      width:220px;
      background:#222;
      color:#fff;
      height:100vh;
      position:fixed;
      padding:15px;
    }

    .sidebar a{
      display:block;
      color:#fff;
      text-decoration:none;
      padding:12px;
      border-radius:6px;
      margin-bottom:6px;
    }

    .sidebar a:hover{
      background:#444;
    }

    /* ===== BADGE ===== */
    .badge{
      float:right;
      background:red;
      color:#fff;
      padding:2px 8px;
      border-radius:12px;
      font-size:12px;
    }

    /* ===== MAIN CONTENT ===== */
    .main{
      margin-left:240px;
      padding:15px;
    }

    .card{
      background:#fff;
      padding:15px;
      border-radius:10px;
      margin-bottom:10px;
      box-shadow:0 2px 6px rgba(0,0,0,.08);
    }

    /* ===== MOBILE FIX ===== */
    @media (max-width:768px){
      .sidebar{
        position:relative;
        width:100%;
        height:auto;
      }
      .main{
        margin-left:0;
      }
    }
    </style>

  </head>
  <body>

  <div class="sidebar">
    <h3>MENU</h3>
    <a href="/">Dashboard</a>
    <a href="/dashboard">
      Reports<span class="badge" id="cnt">0</span>
    </a>
    <a href="/logout">Logout</a>
  </div>

  <div class="main">
    <h2>Coin Reports</h2>

<select id="type">
  <option value="A">ADD COIN</option>
  <option value="C">CONVERTED</option>
</select>

<select id="period" onchange="toggle()">
  <option value="monthly">Monthly</option>
  <option value="weekly">Weekly</option>
</select>

<input type="month" id="month">
<input type="week" id="week" style="display:none">

<button onclick="load()">Load</button>
<div style='margin-top:2em;'>
<div id="list"></div>
</div>


  </div>

  <script>
function toggle(){
  let p=document.getElementById('period').value;
  month.style.display = p==='monthly'?'block':'none';
  week.style.display  = p==='weekly'?'block':'none';
}

function load(){
  let t=type.value;
  let p=period.value;
  let k=p==='monthly'?month.value:week.value;

  fetch(`/coinReport?type=${t}&period=${p}&key=${k}`)
  .then(r=>r.json())
  .then(d=>{
    let h='';
    let total=0;
    d.forEach(x=>{
      total+=x.coin;
      h+=`<div class="card">DATE: ${x.date} - COIN: ${x.coin}</div>`;
    });
    list.innerHTML = `<b>Total: ${total}</b>` + h;
  });
}
</script>

  </body>
  </html>
  )rawliteral";

  return html;
}

bool isAuthenticated() {
  if (server.hasHeader("Cookie")) {
    String cookie = server.header("Cookie");
    if (cookie.indexOf("ESPSESSION=1") >= 0) return true;
  }
  return false;
}

void redirectToLogin() {
  server.sendHeader("Location", "/login", true);
  server.send(302, "text/plain", "");
}

void startAPandCaptive() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPass);
  IPAddress apIP = WiFi.softAPIP(); // usually 192.168.4.1
  Serial.print("AP IP: "); Serial.println(apIP);
  dnsServer.start(DNS_PORT, "*", apIP);

  // Web handlers (same handlers used for STA mode)
  server.on("/login", HTTP_GET, [](){
    server.send(200, "text/html", loginPage());
  });

  server.on("/", HTTP_GET, [](){
    if (!isAuthenticated()) {
      redirectToLogin();
      return;
    }
    String html = portalPage(String(creds.ssid), String(creds.pass), String(creds.devName), String(creds.coin) );
    server.send(200, "text/html", html);
  });

  server.on("/dashboard", HTTP_GET, [](){
    if (!isAuthenticated()) {
      redirectToLogin();
      return;
    }
    String html = portalDashboardPage();
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, [](){
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    String dev  = server.arg("devname");
    String coin = server.arg("coin");

    ssid = ssid.substring(0, 31);
    pass = pass.substring(0, 63);
    dev  = dev.substring(0, 63);
    coin = coin.substring(0, 31);

    memset(&creds, 0, sizeof(creds));
    ssid.toCharArray(creds.ssid, sizeof(creds.ssid));
    pass.toCharArray(creds.pass, sizeof(creds.pass));
    dev.toCharArray(creds.devName, sizeof(creds.devName));
    coin.toCharArray(creds.coin, sizeof(creds.coin));

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

  // server.onNotFound([](){
  //   // redirect all requests to root to create captive effect
  //   server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  //   server.send(302, "text/plain", "");
  // });

  server.begin();
  Serial.println("Captive portal started (AP mode).");
}

String getDate() {
  time_t now = time(nullptr);
  if (now < 100000) return "No Date";  // wala pa ka sync

  struct tm *t = localtime(&now);
  char buf[15];
  sprintf(buf, "%04d-%02d-%02d",
          t->tm_year + 1900,
          t->tm_mon + 1,
          t->tm_mday);
  return String(buf);
}

void saveCoinLog(const char* date, char type, int amount) {
  int pos = EEPROM.read(COIN_LOG_ADDR);
  if (pos < 1 || pos > COIN_LOG_SIZE - 15) pos = 1;

  int base = COIN_LOG_ADDR + pos;

  for (int i = 0; i < 10; i++)
    EEPROM.write(base + i, date[i]);

  EEPROM.write(base + 10, '|');
  EEPROM.write(base + 11, type); // 'A' or 'C'
  EEPROM.write(base + 12, '|');
  EEPROM.write(base + 13, amount);

  pos += 15;
  EEPROM.write(COIN_LOG_ADDR, pos);
  EEPROM.commit();
}


void startServerHandlers() {
  // login page
  server.on("/login", HTTP_GET, [](){
    server.send(200, "text/html", loginPage());
  });

  server.on("/login", HTTP_POST, [](){
    String user = server.arg("user");
    String pass = server.arg("pass");

    if (user == ADMIN_USER && pass == ADMIN_PASS) {
      server.sendHeader("Set-Cookie", "ESPSESSION=1; Path=/");
      server.send(200, "text/html","<script>window.location.href='/'</script>");
    } else {
      server.send(401, "text/html",
        "<h3>Invalid login</h3><a href='/login'>Try again</a>");
    }
  });

  server.on("/logout", HTTP_GET, [](){
    server.sendHeader("Set-Cookie", "ESPSESSION=0; Max-Age=0; Path=/");
    redirectToLogin();
  });

  // Define or re-define handlers (safe to call both modes)
  server.on("/", HTTP_GET, [](){
    if (!isAuthenticated()) {
      redirectToLogin();
      return;
    }
    String html = portalPage(String(creds.ssid), String(creds.pass), String(creds.devName), String(creds.coin));
    server.send(200, "text/html", html);
  });

  server.on("/dashboard", HTTP_GET, []() {
    if (!isAuthenticated()) {
      redirectToLogin();
      return;
    }
    String html = portalDashboardPage();
    server.send(200, "text/html", html);
  });

  // ===== JSON data for dashboard =====
  server.on("/data", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", activeUsersJSON);
  });

  server.on("/saveCoin", HTTP_POST, [](){
    String coin = server.arg("coin");
    String date = server.arg("date");
    coin = coin.substring(0, 31);
   
    int addcoin = coin.toInt();
    
    // 🔑 IMPORTANT: reload existing creds first
    readCredsFromEEPROM();

    // update coin only
    coin.toCharArray(creds.coin, sizeof(creds.coin));

    saveCoinToEEPROM(creds);

    Serial.print("Coin saved: ");
    Serial.println(creds.coin);

    // Save report
    saveCoinLog(date.c_str(), 'A', addcoin);
    
    server.send(200, "text/html","<h3>Coin stock saved</h3><p><a href='/'>Back</a></p>");
    
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
    String coin = server.arg("coin");

    ssid = ssid.substring(0, 31);
    pass = pass.substring(0, 63);
    dev  = dev.substring(0, 63);
    coin = coin.substring(0, 31);

    memset(&creds, 0, sizeof(creds));
    ssid.toCharArray(creds.ssid, sizeof(creds.ssid));
    pass.toCharArray(creds.pass, sizeof(creds.pass));
    dev.toCharArray(creds.devName, sizeof(creds.devName));
    coin.toCharArray(creds.coin, sizeof(creds.coin));

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
      String date = server.arg("date");

      // Load existing coin stock
      readCredsFromEEPROM();
      int stock = atoi(creds.coin);

      coin = scoin.toInt();
      
      Serial.println(scoin);
      sendToNano(coin);
      // Pasigahun ang LED
      digitalWrite(LED_PIN, LOW); // Active-LOW LED ON
      // Send response balik sa AJAX
      String res = "{\"status\":\"success\",\"message\":\"LED turned ON\",\"coin\":\"" + scoin + "\"}";
      server.send(200, "application/json", res);

      stock -= coin;

      itoa(stock, creds.coin, 10);
      saveCoinToEEPROM(creds);

      saveCoinLog(date.c_str(), 'C', coin);

    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing convertCash\"}");
    }
  });

  // ===== Receive active users from MikroTik =====
  server.on("/active", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");

    String total = server.arg("total");
    String list  = server.arg("list");

    String json = "{\"total\":" + total + ",\"users\":[";
    int start = 0;

    while (true) {
      int comma = list.indexOf(',', start);
      if (comma == -1) break;

      String item = list.substring(start, comma);
      int p1 = item.indexOf('|');
      int p2 = item.lastIndexOf('|');

      if (p1 > 0 && p2 > p1) {
        String user = item.substring(0, p1);
        String ip   = item.substring(p1 + 1, p2);
        String time = item.substring(p2 + 1);

        json += "{\"user\":\"" + user + "\",\"ip\":\"" + ip + "\",\"time\":\"" + time + "\"},";
      }
      start = comma + 1;
    }

    if (json.endsWith(",")) json.remove(json.length() - 1);
    json += "]}";

    activeUsersJSON = json;
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/coinReport", HTTP_GET, [](){

    String type = server.arg("type");   // A or C
    String period = server.arg("period"); // weekly | monthly
    String key = server.arg("key");     // 2026-01 or 2026-W02

    String res = "[";
    int pos = EEPROM.read(COIN_LOG_ADDR);

    for (int i = 1; i < pos; i += 15) {
      int base = COIN_LOG_ADDR + i;

      char date[11];
      for (int j=0;j<10;j++) date[j]=EEPROM.read(base+j);
      date[10]=0;

      char t = EEPROM.read(base+11);
      int amt = EEPROM.read(base+13);

      if (String(t) != type) continue;

      // MONTH FILTER
      if (period == "monthly" && !String(date).startsWith(key)) continue;

      // WEEK FILTER (simple YYYY-WW sent from frontend)
      if (period == "weekly" && key.length()==7) {
        // frontend handles week grouping
      }

      res += "{\"date\":\"" + String(date) +
            "\",\"type\":\"" + String(t) +
            "\",\"coin\":" + String(amt) + "},";
    }

    if (res.endsWith(",")) res.remove(res.length()-1);
    res += "]";

    server.send(200,"application/json",res);
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

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  // 🔑 ENABLE COOKIE SUPPORT
  server.collectHeaders("Cookie");

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

