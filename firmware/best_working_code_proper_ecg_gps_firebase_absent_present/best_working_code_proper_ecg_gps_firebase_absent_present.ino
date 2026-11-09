// Public source: configure locally using config.example.h; see docs/CONFIGURATION.md.
#include <Arduino.h>
#include "config.local.h"

#include <WiFi.h>
#include <WebServer.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

// ================== WIFI ==================

// ================== FIREBASE CONFIGURATION ==================
// RTDB URL: https://YOUR_FIREBASE_PROJECT-default-rtdb.firebaseio.com
// We will use REST calls like: https://<host>/<path>.json

// ================== WEB SERVER ==================
WebServer server(80);

// ================== GPS ==================
TinyGPSPlus gps;
HardwareSerial GPSserial(2);  // UART2

#define GPS_RX 16
#define GPS_TX 17

// ---- College geofence as CIRCLE (center + radius) ----
const double RADIUS  = 50.0; // meters

// Hold last valid fix for a few seconds (prevents NO_FIX flicker)
volatile double lastLat = 0.0, lastLng = 0.0;
volatile bool gpsValid = false;
String gpsStatus = "NO_FIX";  // "INSIDE" / "OUTSIDE" / "NO_FIX"
volatile unsigned long lastFixMs = 0;
bool haveGpsFix = false;
const unsigned long GPS_HOLD_MS = 5000;

// ================== ECG PINS (AD8232 -> ESP32) ==================
#define ECG_PIN   36   // VP (GPIO36)
#define LO_PLUS   34
#define LO_MINUS  35

// ================== ECG SAMPLING / WINDOW ==================
#define SAMPLE_INTERVAL_MS 4
#define WINDOW_MS          10000
#define REFRACTORY_MS      300

// ================== ECG PEAK DETECTOR ==================
#define BASELINE_ALPHA 0.01f
#define THRESH_MARGIN  250

// ================== VIRAJ TEMPLATE ==================



// ================== ECG STATUS ==================
enum StatusState { ST_NONE, ST_DISCONNECTED, ST_AUTHORIZED, ST_UNAUTHORIZED };
volatile StatusState lastStatus = ST_NONE;

const char* ecgStatusStr(StatusState s) {
  if (s == ST_DISCONNECTED) return "ELECTRODES_NOT_CONNECTED";
  if (s == ST_AUTHORIZED)   return "VIRAJ";
  if (s == ST_UNAUTHORIZED) return "UNAUTHORIZED";
  return "UNKNOWN";
}

// ECG values for web
volatile float lastScore = 999.0f;
volatile int lastBeats = 0;
volatile float lastMeanRR = -1, lastStdRR = -1;
volatile float lastMeanPeak = -1, lastStdPeak = -1;
volatile float lastEnergy = 0;
volatile unsigned long lastWindowMs = 0;

// ================== ECG INTERNAL STATE ==================
unsigned long lastSample = 0;
unsigned long windowStart = 0;
unsigned long lastPeakTime = 0;

int beatCount = 0;
unsigned int rrCount = 0;
float rrSum = 0, rrSqSum = 0;
float peakSum = 0, peakSqSum = 0;
float energySum = 0;
unsigned long sampleCount = 0;

float baseline = 2000.0f;
int prev1 = 0, prev2 = 0;
bool armed = true;

// ================== ATTENDANCE STATE ==================
enum AttendState { AS_PRESENT, AS_ABSENT };

AttendState currentAttend = AS_ABSENT;
unsigned long stateStartMs = 0;

// daily totals (seconds)
unsigned long todayPresentSec = 0;
unsigned long todayAbsentSec  = 0;

// last day string
String currentDay = "";
String stateStartIso = "";
String stateReason = "UNKNOWN";
bool attendanceStarted = false;
unsigned long lastAccountMs = 0;
uint64_t presentRemainderMs = 0;
uint64_t absentRemainderMs = 0;

void accountElapsedUntil(unsigned long nowMs) {
  if (!attendanceStarted) return;
  unsigned long delta = nowMs - lastAccountMs;
  uint64_t total = (currentAttend == AS_PRESENT ? presentRemainderMs : absentRemainderMs) + (uint64_t)delta;
  if (currentAttend == AS_PRESENT) {
    todayPresentSec += (unsigned long)(total / 1000ULL);
    presentRemainderMs = total % 1000ULL;
  } else {
    todayAbsentSec += (unsigned long)(total / 1000ULL);
    absentRemainderMs = total % 1000ULL;
  }
  lastAccountMs = nowMs;
}


// Periodic database writes, separate from elapsed-time accounting.
unsigned long lastFirebaseHeartbeatMs = 0;
const unsigned long FIREBASE_HEARTBEAT_MS = 15000;


// ================== HELPERS ==================
double distanceMeters(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double dLat = radians(lat2 - lat1);
  double dLon = radians(lon2 - lon1);

  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(radians(lat1)) * cos(radians(lat2)) *
             sin(dLon / 2) * sin(dLon / 2);

  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c;
}

static inline bool electrodesConnected() {
  return !(digitalRead(LO_PLUS) == 1 || digitalRead(LO_MINUS) == 1);
}
static inline bool validSample(int ecg) {
  return (ecg > 0 && ecg < 4095);
}
static inline float z2(float x, float mu, float sd) {
  if (sd < 1e-6f) sd = 1e-6f;
  float z = (x - mu) / sd;
  return z * z;
}
void resetWindow() {
  beatCount = 0;
  rrCount = 0;
  lastPeakTime = 0;
  prev1 = prev2 = 0;
  armed = true;
  rrSum = rrSqSum = 0;
  peakSum = peakSqSum = 0;
  energySum = 0;
  sampleCount = 0;
  windowStart = millis();
}

// ================== TIME (NTP) ==================
String isoNow() {
  // returns "YYYY-MM-DDTHH:MM:SS"
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) return "";
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return String(buf);
}
String dayNow() {
  // returns "YYYY-MM-DD"
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0)) return "";
  char buf[12];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

// ================== FIREBASE REST (ID TOKEN + VERIFIED TLS) ==================
WiFiClientSecure fbClient;

bool beginFirebaseRequest(HTTPClient& https, const String& path) {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!String(FIREBASE_ID_TOKEN).startsWith("eyJ") ||
      String(FIREBASE_ROOT_CA).indexOf("-----BEGIN CERTIFICATE-----") < 0) {
    Serial.println("Firebase request refused: configure a current ID token and trusted CA certificate.");
    return false;
  }
  fbClient.setCACert(FIREBASE_ROOT_CA);
  https.setTimeout(2000);
  String url = String("https://") + FIREBASE_HOST + path + ".json?auth=" + FIREBASE_ID_TOKEN;
  return https.begin(fbClient, url);
}


bool firebasePOST(const String& path, const String& jsonBody) {
  HTTPClient https;
  if (!beginFirebaseRequest(https, path)) return false;

  https.addHeader("Content-Type", "application/json");
  int code = https.POST(jsonBody);
  https.end();
  return (code >= 200 && code < 300);
}

bool firebasePATCH(const String& path, const String& jsonBody) {
  HTTPClient https;
  if (!beginFirebaseRequest(https, path)) return false;

  https.addHeader("Content-Type", "application/json");
  int code = https.sendRequest("PATCH", (uint8_t*)jsonBody.c_str(), jsonBody.length());
  https.end();
  return (code >= 200 && code < 300);
}

bool firebasePUT(const String& path, const String& jsonBody) {
  HTTPClient https;
  if (!beginFirebaseRequest(https, path)) return false;

  https.addHeader("Content-Type", "application/json");
  int code = https.PUT(jsonBody);
  https.end();
  return (code >= 200 && code < 300);
}

// Read stored summary values through the authenticated REST helper.
bool firebaseGET(const String& path, String &out) {
  HTTPClient https;
  if (!beginFirebaseRequest(https, path)) return false;
  int code = https.GET();
  if (code > 0) out = https.getString();
  https.end();
  return (code >= 200 && code < 300);
}

// Parse the numeric fields used by the summary response.
long extractJsonNumber(const String& json, const String& key) {
  String k = "\"" + key + "\"";
  int i = json.indexOf(k);
  if (i < 0) return -1;
  i = json.indexOf(":", i);
  if (i < 0) return -1;
  i++;
  while (i < (int)json.length() && (json[i] == ' ' || json[i] == '\n' || json[i] == '\r')) i++;
  bool neg = false;
  if (i < (int)json.length() && json[i] == '-') { neg = true; i++; }
  long val = 0;
  bool any = false;
  while (i < (int)json.length()) {
    char c = json[i];
    if (c < '0' || c > '9') break;
    val = val * 10 + (c - '0');
    any = true;
    i++;
  }
  if (!any) return -1;
  return neg ? -val : val;
}

// Restore this device's stored totals for the current day.
void loadTodayTotalsFromFirebase() {
  String path = String("/attendance/") + STUDENT_NAME + "/" + currentDay + "/summary";
  String resp;
  if (!firebaseGET(path, resp)) {
    Serial.println("Firebase GET failed (summary). Starting from 0.");
    return;
  }
  resp.trim();
  if (resp == "null" || resp.length() < 4) {
    Serial.println("No summary found for today. Starting from 0.");
    return;
  }
  long p = extractJsonNumber(resp, "present_sec");
  long a = extractJsonNumber(resp, "absent_sec");
  if (p >= 0) todayPresentSec = (unsigned long)p;
  if (a >= 0) todayAbsentSec  = (unsigned long)a;

  Serial.print("Loaded totals: present_sec=");
  Serial.print(todayPresentSec);
  Serial.print(" absent_sec=");
  Serial.println(todayAbsentSec);
}

// ================== ATTENDANCE LOGIC ==================
bool isPresentNow() {
  // PRESENT only if GPS is INSIDE + ECG authorized
  bool gpsOk = haveGpsFix && gpsValid && (millis() - lastFixMs < GPS_HOLD_MS) && (gpsStatus == "INSIDE");
  bool ecgOk = (lastStatus == ST_AUTHORIZED) && electrodesConnected() && lastWindowMs != 0 && (millis() - lastWindowMs <= WINDOW_MS + 2000UL);
  return gpsOk && ecgOk;
}

void startNewDayIfNeeded() {
  String d = dayNow();
  if (d.length() == 0) return; // Do not write records with a fabricated 1970 date.
  unsigned long nowMs = millis();
  if (!attendanceStarted) {
    currentDay = d;
    loadTodayTotalsFromFirebase();
    nowMs = millis();
    stateStartIso = isoNow();
    if (stateStartIso.length() == 0) return;
    attendanceStarted = true;
    currentAttend = isPresentNow() ? AS_PRESENT : AS_ABSENT;
    stateReason = currentAttend == AS_PRESENT ? "OK" : absentReason();
    stateStartMs = lastAccountMs = nowMs;
    return;
  }
  if (d == currentDay) return;

  // Split at local midnight when it falls within the unaccounted interval.
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm local;
  localtime_r(&tv.tv_sec, &local);
  unsigned long sinceMidnightMs = (unsigned long)(local.tm_hour * 3600UL + local.tm_min * 60UL + local.tm_sec) * 1000UL + tv.tv_usec / 1000UL;
  unsigned long boundaryMs = nowMs;
  String boundaryIso = isoNow();
  if (sinceMidnightMs <= nowMs - lastAccountMs) {
    boundaryMs = nowMs - sinceMidnightMs;
    boundaryIso = d + "T00:00:00";
  }
  accountElapsedUntil(boundaryMs);
  String oldDay = currentDay;
  String oldStart = stateStartIso;
  String oldReason = stateReason;
  String oldStatus = currentAttend == AS_PRESENT ? "PRESENT" : "ABSENT";
  unsigned long oldDuration = (boundaryMs - stateStartMs) / 1000UL;
  String summary = "{\"present_sec\":" + String(todayPresentSec) + ",\"absent_sec\":" + String(todayAbsentSec) + "}";

  // Change the accounting state before any blocking network operations.
  currentDay = d;
  todayPresentSec = todayAbsentSec = 0;
  presentRemainderMs = absentRemainderMs = 0;
  stateStartMs = lastAccountMs = boundaryMs;
  stateStartIso = boundaryIso;
  pushSession(oldDay, oldStatus, oldStart, boundaryIso, oldDuration, oldReason);
  firebasePATCH(String("/attendance/") + STUDENT_NAME + "/" + oldDay + "/summary", summary);
}

void pushSession(const String& day, const String& statusText, const String& startIso, const String& endIso, unsigned long durSec, const String& reason) {
  String base = String("/attendance/") + STUDENT_NAME + "/" + day + "/sessions";
  String body = "{";
  body += "\"status\":\"" + statusText + "\",";
  body += "\"start\":\"" + startIso + "\",";
  body += "\"end\":\"" + endIso + "\",";
  body += "\"duration_sec\":" + String(durSec) + ",";
  body += "\"reason\":\"" + reason + "\"";
  body += "}";
  firebasePOST(base, body);
}

String absentReason() {
  // choose the most important reason
  if (!gpsValid) return "NO_GPS_FIX";
  if (gpsStatus != "INSIDE") return "OUTSIDE_GEOFENCE";
  if (lastStatus == ST_DISCONNECTED) return "ELECTRODES_NOT_CONNECTED";
  if (lastStatus == ST_UNAUTHORIZED) return "UNAUTHORIZED_ECG";
  return "ECG_NOT_READY";
}

void updateAttendance() {
  startNewDayIfNeeded();
  if (!attendanceStarted) return;
  unsigned long nowMs = millis();
  accountElapsedUntil(nowMs);
  AttendState newState = isPresentNow() ? AS_PRESENT : AS_ABSENT;
  String newReason = newState == AS_PRESENT ? "OK" : absentReason();
  if (newState == currentAttend && newReason == stateReason) return;

  String endIso = isoNow();
  if (endIso.length() == 0) return;
  String oldStatus = currentAttend == AS_PRESENT ? "PRESENT" : "ABSENT";
  String oldStart = stateStartIso;
  String oldReason = stateReason;
  unsigned long duration = (nowMs - stateStartMs) / 1000UL;
  currentAttend = newState;
  stateReason = newReason;
  stateStartMs = nowMs;
  stateStartIso = endIso;

  // Totals were already accounted above. Closing a session never adds time twice.
  pushSession(currentDay, oldStatus, oldStart, endIso, duration, oldReason);
  String summary = "{\"present_sec\":" + String(todayPresentSec) + ",\"absent_sec\":" + String(todayAbsentSec) + "}";
  firebasePATCH(String("/attendance/") + STUDENT_NAME + "/" + currentDay + "/summary", summary);
}

// Publish the current state and summary at the configured heartbeat interval.
void firebaseHeartbeat() {
  if (!attendanceStarted || WiFi.status() != WL_CONNECTED) return;
  // write current status periodically so Firebase always shows live state
  if (millis() - lastFirebaseHeartbeatMs < FIREBASE_HEARTBEAT_MS) return;
  lastFirebaseHeartbeatMs = millis();

  // current
  String base = String("/attendance/") + STUDENT_NAME + "/current";
  String body = "{";
  body += "\"day\":\"" + currentDay + "\",";
  body += "\"status\":\"" + String(currentAttend == AS_PRESENT ? "PRESENT" : "ABSENT") + "\",";
  body += "\"gps_valid\":" + String(gpsValid ? "true" : "false") + ",";
  body += "\"gps_status\":\"" + gpsStatus + "\",";
  body += "\"lat\":" + String(lastLat, 6) + ",";
  body += "\"lng\":" + String(lastLng, 6) + ",";
  body += "\"ecg_status\":\"" + String(ecgStatusStr(lastStatus)) + "\",";
  body += "\"score\":" + String(lastScore, 3) + ",";
  body += "\"updated\":\"" + isoNow() + "\"";
  body += "}";
  firebasePUT(base, body);

  // summary today (PUT)
  String sumBase = String("/attendance/") + STUDENT_NAME + "/" + currentDay + "/summary";
  String sum = "{";
  sum += "\"present_sec\":" + String(todayPresentSec) + ",";
  sum += "\"absent_sec\":" + String(todayAbsentSec);
  sum += "}";
  firebasePUT(sumBase, sum);
}

// ================== ATTENDANCE TICK ==================
void attendanceEveryInterval() {
  // Update counters from elapsed time; heartbeat owns the periodic writes.
  updateAttendance();
  firebaseHeartbeat();
}

// ================== WEB UI PAGE (MAP + STATUS) ==================
// Changed client-side refresh to 15 seconds to match server-side interval.
const char PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>ESP32 GPS + ECG + Attendance</title>
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
  <style>
    body{margin:0;font-family:Arial}
    #top{padding:12px}
    #map{height:60vh;width:100%}
    .grid{display:grid;grid-template-columns:1fr;gap:10px;max-width:980px}
    .card{border:1px solid #ddd;border-radius:12px;padding:12px}
    .big{font-size:20px;font-weight:700}
    .pill{display:inline-block;padding:6px 10px;border-radius:999px;font-size:14px}
    .ok{background:#e9f7ef;color:#0a7a0a}
    .bad{background:#fdecea;color:#b00020}
    .warn{background:#fff4e5;color:#aa6a00}
    .nofix{background:#eee;color:#333}
    .row{margin:6px 0}
    code{background:#f6f6f6;padding:2px 6px;border-radius:6px}
  </style>
</head>
<body>
  <div id="top">
    <div class="grid">
      <div class="card">
        <div class="big">GPS Geofence</div>
        <div class="row">GPS: <span id="gpsPill" class="pill nofix">Loading...</span></div>
        <div class="row">Lat/Lng: <code id="latlng">-</code></div>
      </div>

      <div class="card">
        <div class="big">ECG Authentication</div>
        <div class="row">ECG: <span id="ecgPill" class="pill nofix">Loading...</span></div>
        <div class="row">Score: <code id="score">-</code> (thr <code id="thr">-</code>)</div>
        <div class="row">beats <code id="beats">-</code></div>
      </div>

      <div class="card">
        <div class="big">Attendance (Live)</div>
        <div class="row">Now: <span id="attPill" class="pill nofix">Loading...</span></div>
        <div class="row">Today Present: <code id="psec">-</code> sec</div>
        <div class="row">Today Absent: <code id="asec">-</code> sec</div>
      </div>
    </div>
  </div>

  <div id="map"></div>

<script>
const homeLat = %HOME_LAT%;
const homeLng = %HOME_LNG%;
const radiusM = %RADIUS%;

var map = L.map('map').setView([homeLat, homeLng], 18);
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:20, attribution:'&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'}).addTo(map);

L.marker([homeLat, homeLng]).addTo(map).bindPopup('Geofence Center');
L.circle([homeLat, homeLng], {radius: radiusM, color:'#0055ff', fillColor:'#0055ff', fillOpacity:0.15}).addTo(map);

var deviceMarker = L.marker([homeLat, homeLng]).addTo(map).bindPopup('Device');

function setPill(el, text, cls){
  el.textContent = text;
  el.className = "pill " + cls;
}

async function refresh(){
  try{
    const r = await fetch('/data');
    const d = await r.json();

    // GPS
    const gpsPill = document.getElementById('gpsPill');
    if(!d.gps.valid){
      setPill(gpsPill, "NO_FIX", "nofix");
      document.getElementById('latlng').textContent = "-";
    }else{
      document.getElementById('latlng').textContent = d.gps.lat.toFixed(6)+", "+d.gps.lng.toFixed(6);
      deviceMarker.setLatLng([d.gps.lat, d.gps.lng]);
      if(d.gps.status === "INSIDE") setPill(gpsPill, "INSIDE", "ok");
      else setPill(gpsPill, "OUTSIDE", "bad");
    }

    // ECG
    const ecgPill = document.getElementById('ecgPill');
    if(d.ecg.status === "VIRAJ") setPill(ecgPill, "VIRAJ", "ok");
    else if(d.ecg.status === "UNAUTHORIZED") setPill(ecgPill, "UNAUTHORIZED", "bad");
    else if(d.ecg.status === "ELECTRODES_NOT_CONNECTED") setPill(ecgPill, "ELECTRODES NOT CONNECTED", "warn");
    else setPill(ecgPill, "UNKNOWN", "nofix");

    document.getElementById('thr').textContent = d.ecg.threshold.toFixed(3);
    document.getElementById('score').textContent = d.ecg.score.toFixed(3);
    document.getElementById('beats').textContent = d.ecg.beats;

    // Attendance
    const attPill = document.getElementById('attPill');
    if(d.att.status === "PRESENT") setPill(attPill, "PRESENT", "ok");
    else setPill(attPill, "ABSENT", "bad");

    document.getElementById('psec').textContent = d.att.present_sec;
    document.getElementById('asec').textContent = d.att.absent_sec;

  }catch(e){
    setPill(document.getElementById('gpsPill'), "NO CONNECTION", "nofix");
    setPill(document.getElementById('ecgPill'), "NO CONNECTION", "nofix");
    setPill(document.getElementById('attPill'), "UNKNOWN", "nofix");
    console.log(e);
  }
}
// REFRESH every 15 seconds to match server-side interval
setInterval(refresh, 15000);
refresh();
</script>
</body>
</html>
)HTML";

String buildPage() {
  String p = PAGE;
  p.replace("%HOME_LAT%", String(homeLat, 6));
  p.replace("%HOME_LNG%", String(homeLng, 6));
  p.replace("%RADIUS%", String(RADIUS, 1));
  return p;
}

bool requireDashboardLogin() {
  if (String(DASHBOARD_USERNAME).length() == 0 ||
      String(DASHBOARD_PASSWORD).length() < 12 ||
      String(DASHBOARD_PASSWORD) == "INSERT_A_PRIVATE_PASSWORD_12_PLUS_CHARS") {
    server.send(503, "text/plain", "Configure a private dashboard username and password first.");
    return false;
  }
  if (!server.authenticate(DASHBOARD_USERNAME, DASHBOARD_PASSWORD)) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

void handleRoot() {
  if (!requireDashboardLogin()) return;
  server.send(200, "text/html", buildPage());
}

void handleData() {
  if (!requireDashboardLogin()) return;
  String json = "{";

  json += "\"gps\":{";
  json += "\"valid\":" + String(gpsValid ? "true" : "false") + ",";
  json += "\"lat\":" + String(lastLat, 6) + ",";
  json += "\"lng\":" + String(lastLng, 6) + ",";
  json += "\"status\":\"" + gpsStatus + "\"";
  json += "},";

  json += "\"ecg\":{";
  json += "\"status\":\"" + String(ecgStatusStr(lastStatus)) + "\",";
  json += "\"score\":" + String(lastScore, 6) + ",";
  json += "\"threshold\":" + String(AUTH_THRESHOLD, 6) + ",";
  json += "\"beats\":" + String(lastBeats);
  json += "},";

  json += "\"att\":{";
  json += "\"status\":\"" + String(currentAttend == AS_PRESENT ? "PRESENT" : "ABSENT") + "\",";
  json += "\"present_sec\":" + String(todayPresentSec) + ",";
  json += "\"absent_sec\":" + String(todayAbsentSec);
  json += "}";

  json += "}";
  server.send(200, "application/json", json);
}

// A verification expires if no fresh result is produced.
const unsigned long ECG_MAX_RESULT_AGE_MS = WINDOW_MS + 2000UL;
bool electrodesWereConnected = false;

void invalidateECG(StatusState state) {
  lastStatus = state;
  lastWindowMs = 0;
  lastScore = 999.0f;
  baseline = 2000.0f;
  resetWindow();
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);

  // GPS
  GPSserial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  // ECG
  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(ECG_PIN, ADC_11db);
  resetWindow();
  lastStatus = ST_DISCONNECTED;

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
  Serial.print("Open Web: http://");
  Serial.println(WiFi.localIP());

  // NTP time (India +5:30)
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");

  // Web routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();

  // init attendance day
  currentDay = "";
  stateStartMs = millis();
  currentAttend = AS_ABSENT;
  attendanceStarted = false; // First valid clock reading initializes/restores totals.

  // initial Firebase current node
  firebaseHeartbeat();


}

// ================== LOOP ==================
void loop() {
  if (lastWindowMs != 0 && millis() - lastWindowMs > ECG_MAX_RESULT_AGE_MS) {
    invalidateECG(ST_NONE);
  }
  server.handleClient();

  // Account elapsed time; database heartbeats are throttled to 15 seconds.
  attendanceEveryInterval();

  // ------------------ GPS PARSE ------------------
  while (GPSserial.available()) {
    gps.encode(GPSserial.read());
  }

  if (gps.location.isUpdated() && gps.location.isValid()) {
    lastLat = gps.location.lat();
    lastLng = gps.location.lng();
    lastFixMs = millis();
    haveGpsFix = true;
  }

  gpsValid = haveGpsFix && (millis() - lastFixMs) < GPS_HOLD_MS;

  if (gpsValid) {
    double d = distanceMeters(lastLat, lastLng, homeLat, homeLng);
    gpsStatus = (d <= RADIUS) ? "INSIDE" : "OUTSIDE";
  } else {
    gpsStatus = "NO_FIX";
  }

  // ------------------ ECG ------------------
  if (millis() - lastSample < SAMPLE_INTERVAL_MS) {
    // still run attendance + firebase heartbeat even if ECG sampling not ready
    updateAttendance();
    firebaseHeartbeat();
    return;
  }
  lastSample = millis();

  if (!electrodesConnected()) {
    electrodesWereConnected = false;
    invalidateECG(ST_DISCONNECTED);
    updateAttendance();
    firebaseHeartbeat();
    return;
  }
  if (!electrodesWereConnected) {
    electrodesWereConnected = true;
    invalidateECG(ST_NONE);
  }

  int ecg = analogRead(ECG_PIN);
  if (!validSample(ecg)) {
    invalidateECG(ST_NONE);
    updateAttendance();
    firebaseHeartbeat();
    return;
  }

  baseline = (1.0f - BASELINE_ALPHA) * baseline + BASELINE_ALPHA * (float)ecg;
  float dynThresh = baseline + THRESH_MARGIN;

  energySum += (float)ecg * (float)ecg;
  sampleCount++;

  unsigned long now = millis();

  if (armed) {
    if ((float)prev1 > dynThresh && prev1 > prev2 && prev1 >= ecg) {
      if (lastPeakTime != 0 && (now - lastPeakTime) > REFRACTORY_MS) {
        float rr = (float)(now - lastPeakTime);
        rrSum += rr;
        rrSqSum += rr * rr;
        rrCount++;

        beatCount++;
        peakSum += (float)prev1;
        peakSqSum += (float)prev1 * (float)prev1;

        lastPeakTime = now;
        armed = false;
      } else if (lastPeakTime == 0) {
        beatCount++;
        peakSum += (float)prev1;
        peakSqSum += (float)prev1 * (float)prev1;
        lastPeakTime = now;
        armed = false;
      }
    }
  } else {
    if ((float)ecg < dynThresh) armed = true;
  }

  prev2 = prev1;
  prev1 = ecg;

  if (now - windowStart >= WINDOW_MS) {
    if (sampleCount < 50) {
      invalidateECG(ST_NONE);
      updateAttendance();
      firebaseHeartbeat();
      return;
    }

    float meanRR = -1, stdRR = -1;
    if (rrCount >= 2) {
      float rrN = (float)rrCount;
      meanRR = rrSum / rrN;
      float varRR = (rrSqSum / rrN) - (meanRR * meanRR);
      if (varRR < 0) varRR = 0;
      stdRR = sqrtf(varRR);
    }

    float meanPeak = -1, stdPeak = -1;
    if (beatCount >= 1) {
      meanPeak = peakSum / (float)beatCount;
      float varPeak = (peakSqSum / (float)beatCount) - (meanPeak * meanPeak);
      if (varPeak < 0) varPeak = 0;
      stdPeak = sqrtf(varPeak);
    }

    float energy = energySum / (float)sampleCount;

    bool authorized = false;
    float score = 999.0f;

    if (meanRR > 0 && stdRR >= 0 && meanPeak > 0 && stdPeak >= 0) {
      score = 0;
      score += z2((float)beatCount, MU_beats, SD_beats);
      score += z2(meanRR,   MU_mean_rr_ms, SD_mean_rr_ms);
      score += z2(stdRR,    MU_std_rr_ms,  SD_std_rr_ms);
      score += z2(meanPeak, MU_mean_peak,  SD_mean_peak);
      score += z2(stdPeak,  MU_std_peak,   SD_std_peak);
      score += z2(energy,   MU_energy,     SD_energy);
      score = sqrtf(score);
      authorized = (score <= AUTH_THRESHOLD);
    }

    lastWindowMs = now;
    lastScore = score;
    lastBeats = beatCount;
    lastMeanRR = meanRR;
    lastStdRR = stdRR;
    lastMeanPeak = meanPeak;
    lastStdPeak = stdPeak;
    lastEnergy = energy;

    lastStatus = authorized ? ST_AUTHORIZED : ST_UNAUTHORIZED;

    resetWindow();
  }

  // ------------------ Attendance + Firebase ------------------
  updateAttendance();
  firebaseHeartbeat();
}