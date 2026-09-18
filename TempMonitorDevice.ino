/*
  Temperature Monitoring Device with Web Dashboard + Laptop API Push
  --------------------------------------------------------------------
  Hardware:
    - DHT11               -> GPIO 4
    - KY-038 mic (DO pin) -> GPIO 34
    - KY-012 active buzzer-> GPIO 5
    - Yellow LED          -> GPIO 25 (LOW temp indicator)
    - Orange LED          -> GPIO 26 (HIGH temp indicator, stands in for "red")

  Logic:
    Temp < LOW_TEMP_THRESHOLD            -> Yellow LED ON, buzzer ON
    LOW_TEMP_THRESHOLD..HIGH_TEMP_THRESHOLD -> both LEDs OFF, buzzer OFF
    Temp > HIGH_TEMP_THRESHOLD           -> Orange LED ON, buzzer ON

    The KY-038 mic listens for the buzzer's own sound. When it detects sound,
    it forces an immediate re-check of the DHT11 and refreshes all outputs.

    Every reading is also POSTed to the laptop's Node/Express API, which
    broadcasts it over WebSocket to the React dashboard (and the built-in
    ESP32 page below still works too, as a local fallback).

  Libraries required (install via Library Manager):
    - "DHT sensor library" by Adafruit
    - "Adafruit Unified Sensor" (dependency of the above)

  Board package: install "esp32" boards via Boards Manager (Espressif Systems)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DHT.h>

// ---------- WiFi credentials ----------
const char* WIFI_SSID     = "Student WI-FI";
const char* WIFI_PASSWORD = "Stud3nt!@!";

// ---------- Laptop API (Node/Express) ----------
// Run `node server.js` on your laptop first, it will print its LAN IP.
// Replace the IP below with that address, and keep API_KEY matching your .env file.
const char* LAPTOP_API_URL = "http://192.168.41.121:3000/api/data"; // <-- CHANGE THIS
const char* API_KEY        = "vexguard-2026-hack";              // <-- MUST MATCH .env

// ---------- Pin definitions ----------
#define DHTPIN        4
#define DHTTYPE       DHT11
#define MIC_DO_PIN    34
#define BUZZER_PIN    5
#define YELLOW_LED_PIN 25
#define ORANGE_LED_PIN 26

// ---------- Thresholds ----------
const float LOW_TEMP_THRESHOLD  = 25.0;  // below this => "low" zone
const float HIGH_TEMP_THRESHOLD = 30.0;  // above this => "high" zone

// ---------- Objects ----------
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

// ---------- Shared state (used by both loop() and the web handlers) ----------
float g_temperature   = NAN;
float g_humidity      = NAN;
String g_zone         = "normal";   // "low", "normal", "high"
bool   g_buzzerOn     = false;
bool   g_soundHeard   = false;
unsigned long g_lastReadMillis = 0;
const unsigned long READ_INTERVAL_MS = 2000; // DHT11 needs >=1-2s between reads

// Forward declaration
void updateSensorsAndOutputs();

// ---------------------------------------------------------------------------
// Web page (served at "/"). Polls "/data" every 2s to refresh live values.
// This still works even if the laptop API is offline - useful as a fallback.
// ---------------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Temperature Monitor</title>
  <style>
    body { font-family: Arial, sans-serif; background:#f4f6f8; margin:0; padding:20px; color:#222; }
    .card { background:#fff; border-radius:10px; padding:20px; max-width:420px; margin:0 auto 16px auto;
            box-shadow:0 2px 8px rgba(0,0,0,0.08); }
    h1 { font-size:1.3em; text-align:center; }
    .value { font-size:2em; font-weight:bold; text-align:center; }
    .label { text-align:center; color:#666; margin-bottom:10px; }
    .row { display:flex; justify-content:space-between; padding:6px 0; border-bottom:1px solid #eee; }
    .zone-low    { color:#c9a400; }
    .zone-normal { color:#2e7d32; }
    .zone-high   { color:#c62828; }
    .dot { display:inline-block; width:12px; height:12px; border-radius:50%; margin-right:6px; }
    .on  { background:#4caf50; }
    .off { background:#bbb; }
  </style>
</head>
<body>
  <div class="card">
    <h1>ESP32 Temperature Monitor</h1>
    <div class="label">Temperature</div>
    <div class="value" id="temp">--</div>
    <div class="label">Humidity: <span id="hum">--</span>%</div>
  </div>

  <div class="card">
    <div class="row"><span>Zone</span><span id="zone">--</span></div>
    <div class="row"><span>Yellow LED (low)</span><span><span class="dot" id="yledDot"></span><span id="yled">--</span></span></div>
    <div class="row"><span>Orange LED (high)</span><span><span class="dot" id="oledDot"></span><span id="oled">--</span></span></div>
    <div class="row"><span>Buzzer</span><span><span class="dot" id="buzzDot"></span><span id="buzz">--</span></span></div>
    <div class="row"><span>Sound confirmed</span><span id="sound">--</span></div>
  </div>

  <script>
    async function refresh() {
      try {
        const res = await fetch('/data');
        const d = await res.json();
        document.getElementById('temp').textContent = d.temperature.toFixed(1) + ' °C';
        document.getElementById('hum').textContent = d.humidity.toFixed(0);

        const zoneEl = document.getElementById('zone');
        zoneEl.textContent = d.zone;
        zoneEl.className = 'zone-' + d.zone;

        document.getElementById('yled').textContent = d.yellowLed ? 'ON' : 'OFF';
        document.getElementById('yledDot').className = 'dot ' + (d.yellowLed ? 'on' : 'off');

        document.getElementById('oled').textContent = d.orangeLed ? 'ON' : 'OFF';
        document.getElementById('oledDot').className = 'dot ' + (d.orangeLed ? 'on' : 'off');

        document.getElementById('buzz').textContent = d.buzzer ? 'ON' : 'OFF';
        document.getElementById('buzzDot').className = 'dot ' + (d.buzzer ? 'on' : 'off');

        document.getElementById('sound').textContent = d.soundHeard ? 'Yes' : 'No';
      } catch (e) {
        console.error('refresh failed', e);
      }
    }
    setInterval(refresh, 2000);
    refresh();
  </script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleData() {
  bool yellowOn = (g_zone == "low");
  bool orangeOn = (g_zone == "high");

  String json = "{";
  json += "\"temperature\":" + String(isnan(g_temperature) ? 0 : g_temperature, 1) + ",";
  json += "\"humidity\":" + String(isnan(g_humidity) ? 0 : g_humidity, 1) + ",";
  json += "\"zone\":\"" + g_zone + "\",";
  json += "\"yellowLed\":" + String(yellowOn ? "true" : "false") + ",";
  json += "\"orangeLed\":" + String(orangeOn ? "true" : "false") + ",";
  json += "\"buzzer\":" + String(g_buzzerOn ? "true" : "false") + ",";
  json += "\"soundHeard\":" + String(g_soundHeard ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// Pushes the current reading to the laptop's Node/Express API, which then
// broadcasts it to the React dashboard over WebSocket.
// ---------------------------------------------------------------------------
void sendDataToLaptopAPI() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(LAPTOP_API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", API_KEY);

  bool yellowOn = (g_zone == "low");
  bool orangeOn = (g_zone == "high");

  String payload = "{";
  payload += "\"temperature\":" + String(g_temperature, 1) + ",";
  payload += "\"humidity\":" + String(g_humidity, 1) + ",";
  payload += "\"zone\":\"" + g_zone + "\",";
  payload += "\"yellowLed\":" + String(yellowOn ? "true" : "false") + ",";
  payload += "\"orangeLed\":" + String(orangeOn ? "true" : "false") + ",";
  payload += "\"buzzer\":" + String(g_buzzerOn ? "true" : "false") + ",";
  payload += "\"soundHeard\":" + String(g_soundHeard ? "true" : "false");
  payload += "}";

  int httpResponseCode = http.POST(payload);
  if (httpResponseCode > 0) {
    Serial.printf("POST to laptop API -> HTTP %d\n", httpResponseCode);
  } else {
    Serial.printf("POST to laptop API failed: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

// ---------------------------------------------------------------------------
// Reads the DHT11, decides the zone, and drives LEDs / buzzer.
// ---------------------------------------------------------------------------
void updateSensorsAndOutputs() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    Serial.println("DHT11 read failed, keeping previous values.");
    return;
  }

  g_temperature = t;
  g_humidity = h;

  if (t < LOW_TEMP_THRESHOLD) {
    g_zone = "low";
  } else if (t > HIGH_TEMP_THRESHOLD) {
    g_zone = "high";
  } else {
    g_zone = "normal";
  }

  bool yellowOn = (g_zone == "low");
  bool orangeOn = (g_zone == "high");
  g_buzzerOn = (g_zone != "normal"); // reverted to spec: buzzer only OFF in the normal zone

  digitalWrite(YELLOW_LED_PIN, yellowOn ? HIGH : LOW);
  digitalWrite(ORANGE_LED_PIN, orangeOn ? HIGH : LOW);
  digitalWrite(BUZZER_PIN, g_buzzerOn ? HIGH : LOW);

  Serial.printf("Temp: %.1fC  Hum: %.1f%%  Zone: %s  Buzzer: %s\n",
                g_temperature, g_humidity, g_zone.c_str(),
                g_buzzerOn ? "ON" : "OFF");

  sendDataToLaptopAPI();
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(ORANGE_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MIC_DO_PIN, INPUT);

  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(ORANGE_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();

  // First reading before anything connects, so dashboard has real data ASAP
  delay(2000);
  updateSensorsAndOutputs();

  // ---- WiFi ----
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  // ---- Web server routes ----
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Web server started.");

  // Send an initial reading to the laptop API right away
  sendDataToLaptopAPI();
}

// ---------------------------------------------------------------------------
void loop() {
  server.handleClient();

  unsigned long now = millis();

  // Regular DHT11 poll (respecting its minimum read interval)
  if (now - g_lastReadMillis >= READ_INTERVAL_MS) {
    g_lastReadMillis = now;
    updateSensorsAndOutputs();
  }

  // Microphone: sound confirmation loop
  int soundState = digitalRead(MIC_DO_PIN);
  if (soundState == HIGH) {
    if (!g_soundHeard) {
      g_soundHeard = true;
      Serial.println("Sound detected by KY-038 -> re-checking DHT11.");
      updateSensorsAndOutputs();
    }
  } else {
    g_soundHeard = false;
  }
}
