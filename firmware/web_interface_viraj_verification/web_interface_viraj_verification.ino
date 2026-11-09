// Public source: configure locally using config.example.h; see docs/CONFIGURATION.md.
#include <Arduino.h>
#include "config.local.h"

#include <WiFi.h>
#include <WebServer.h>
#include <math.h>

// ================== WIFI ==================

// ================== WEB SERVER ==================
WebServer server(80);

// ================== PINS (AD8232 -> ESP32) ==================
#define ECG_PIN   36   // VP (GPIO36)
#define LO_PLUS   34   // GPIO34
#define LO_MINUS  35   // GPIO35

// ================== SAMPLING / WINDOW ==================
#define SAMPLE_INTERVAL_MS 4        // 250 Hz
#define WINDOW_MS          10000    // 10 sec features
#define REFRACTORY_MS      300

// ================== PEAK DETECTOR ==================
#define BASELINE_ALPHA 0.01f
#define THRESH_MARGIN  250

// ================== AUTH TEMPLATE (FROM YOUR PYTHON OUTPUT) ==================



// ================== STATE ==================
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

// ================== STATUS + LAST RESULT (FOR WEB) ==================
enum StatusState { ST_NONE, ST_DISCONNECTED, ST_AUTHORIZED, ST_UNAUTHORIZED };
StatusState lastStatus = ST_NONE;

// last computed info (shown on web)
volatile float lastScore = 999.0f;
volatile int lastBeats = 0;
volatile float lastMeanRR = -1, lastStdRR = -1;
volatile float lastMeanPeak = -1, lastStdPeak = -1;
volatile float lastEnergy = 0;
volatile unsigned long lastWindowMs = 0;

const char* statusToStr(StatusState s) {
  if (s == ST_DISCONNECTED) return "ELECTRODES_NOT_CONNECTED";
  if (s == ST_AUTHORIZED)   return "VIRAJ";
  if (s == ST_UNAUTHORIZED) return "UNAUTHORIZED";
  return "UNKNOWN";
}

void setStatus(StatusState s) {
  // only change when needed
  lastStatus = s;
}

// ================== HELPERS ==================
static inline bool electrodesConnected() {
  return !(digitalRead(LO_PLUS) == 1 || digitalRead(LO_MINUS) == 1);
}

static inline bool validSample(int ecg) {
  return (ecg > 0 && ecg < 4095);
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

static inline float z2(float x, float mu, float sd) {
  if (sd < 1e-6f) sd = 1e-6f;
  float z = (x - mu) / sd;
  return z * z;
}

// ================== WEB: HTML PAGE ==================
String htmlPage() {
  String h = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>ECG Watch Verification</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 18px; }
    .card { padding: 16px; border: 1px solid #ddd; border-radius: 12px; max-width: 520px; }
    .big { font-size: 22px; font-weight: 700; margin: 8px 0; }
    .row { margin: 6px 0; }
    .ok { color: #0a7a0a; }
    .bad { color: #b00020; }
    .warn { color: #aa6a00; }
    code { background:#f6f6f6; padding:2px 6px; border-radius: 6px; }
  </style>
</head>
<body>
  <div class="card">
    <div class="big">ECG Watch Verification</div>
    <div class="row">Device: <code>ESP32 + AD8232</code></div>
    <div class="row">Status: <span id="status" class="big">Loading...</span></div>
    <hr/>
    <div class="row">Last window time (ms): <span id="tms">-</span></div>
    <div class="row">Score: <span id="score">-</span> (threshold: <span id="thr">-</span>)</div>
    <div class="row">beats: <span id="beats">-</span></div>
    <div class="row">mean_rr_ms: <span id="mrr">-</span></div>
    <div class="row">std_rr_ms: <span id="srr">-</span></div>
    <div class="row">mean_peak: <span id="mp">-</span></div>
    <div class="row">std_peak: <span id="sp">-</span></div>
    <div class="row">energy: <span id="en">-</span></div>
  </div>

<script>
async function refresh(){
  try{
    const r = await fetch('/status');
    const j = await r.json();

    const st = document.getElementById('status');
    st.textContent = j.status;

    st.className = 'big';
    if(j.status === 'VIRAJ') st.classList.add('ok');
    else if(j.status === 'UNAUTHORIZED') st.classList.add('bad');
    else if(j.status === 'ELECTRODES_NOT_CONNECTED') st.classList.add('warn');

    document.getElementById('tms').textContent = j.t_ms;
    document.getElementById('score').textContent = j.score;
    document.getElementById('thr').textContent = j.threshold;
    document.getElementById('beats').textContent = j.beats;
    document.getElementById('mrr').textContent = j.mean_rr_ms;
    document.getElementById('srr').textContent = j.std_rr_ms;
    document.getElementById('mp').textContent = j.mean_peak;
    document.getElementById('sp').textContent = j.std_peak;
    document.getElementById('en').textContent = j.energy;
  } catch(e){
    const st = document.getElementById('status');
    st.textContent = 'NO CONNECTION';
    st.className = 'big warn';
    console.log(e);
  }
}
setInterval(refresh, 1000);
refresh();
</script>
</body>
</html>
)HTML";
  return h;
}

// ================== WEB: ROUTES ==================
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
  server.send(200, "text/html", htmlPage());
}

void handleStatus() {
  if (!requireDashboardLogin()) return;
  String json = "{";
  json += "\"status\":\"" + String(statusToStr(lastStatus)) + "\",";
  json += "\"t_ms\":" + String(lastWindowMs) + ",";
  json += "\"score\":" + String(lastScore, 3) + ",";
  json += "\"threshold\":" + String(AUTH_THRESHOLD, 3) + ",";
  json += "\"beats\":" + String(lastBeats) + ",";
  json += "\"mean_rr_ms\":" + String(lastMeanRR, 2) + ",";
  json += "\"std_rr_ms\":" + String(lastStdRR, 2) + ",";
  json += "\"mean_peak\":" + String(lastMeanPeak, 1) + ",";
  json += "\"std_peak\":" + String(lastStdPeak, 1) + ",";
  json += "\"energy\":" + String(lastEnergy, 1);
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

  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(ECG_PIN, ADC_11db);

  resetWindow();

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
  Serial.print("Open this in browser: http://");
  Serial.println(WiFi.localIP());

  // Web routes
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.begin();

  setStatus(ST_DISCONNECTED);
}

// ================== LOOP ==================
void loop() {
  if (lastWindowMs != 0 && millis() - lastWindowMs > ECG_MAX_RESULT_AGE_MS) {
    invalidateECG(ST_NONE);
  }
  server.handleClient();  // ✅ keeps web server alive

  // sample timing
  if (millis() - lastSample < SAMPLE_INTERVAL_MS) return;
  lastSample = millis();

  // electrodes disconnected => status only
  if (!electrodesConnected()) {
    electrodesWereConnected = false;
    invalidateECG(ST_DISCONNECTED);
    return;
  }
  if (!electrodesWereConnected) {
    electrodesWereConnected = true;
    invalidateECG(ST_NONE);
  }

  int ecg = analogRead(ECG_PIN);
  if (!validSample(ecg)) { invalidateECG(ST_NONE); return; }

  baseline = (1.0f - BASELINE_ALPHA) * baseline + BASELINE_ALPHA * (float)ecg;
  float dynThresh = baseline + THRESH_MARGIN;

  energySum += (float)ecg * (float)ecg;
  sampleCount++;

  unsigned long now = millis();

  // Peak detection: local max
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

  // End of window => compute & verify
  if (now - windowStart >= WINDOW_MS) {
    if (sampleCount < 50) { invalidateECG(ST_NONE); return; }

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

    // compute score
    bool authorized = false;
    float score = 999.0f;
    if (meanRR > 0 && stdRR >= 0 && meanPeak > 0 && stdPeak >= 0) {
      score = 0;
      score += z2((float)beatCount, MU_beats, SD_beats);
      score += z2(meanRR,        MU_mean_rr_ms, SD_mean_rr_ms);
      score += z2(stdRR,         MU_std_rr_ms,  SD_std_rr_ms);
      score += z2(meanPeak,      MU_mean_peak,  SD_mean_peak);
      score += z2(stdPeak,       MU_std_peak,   SD_std_peak);
      score += z2(energy,        MU_energy,     SD_energy);
      score = sqrtf(score);
      authorized = (score <= AUTH_THRESHOLD);
    }

    // update shared variables for web UI
    lastWindowMs = now;
    lastScore = score;
    lastBeats = beatCount;
    lastMeanRR = meanRR;
    lastStdRR = stdRR;
    lastMeanPeak = meanPeak;
    lastStdPeak = stdPeak;
    lastEnergy = energy;

    setStatus(authorized ? ST_AUTHORIZED : ST_UNAUTHORIZED);

    // optional serial log
    Serial.print("WEB STATUS: ");
    Serial.print(statusToStr(lastStatus));
    Serial.print(" score=");
    Serial.println(score, 3);

    resetWindow();
  }
}
