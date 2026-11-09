// Public source: configure locally using config.example.h; see docs/CONFIGURATION.md.
#include "config.local.h"

#include <math.h>

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
float rrSum = 0, rrSqSum = 0;
float peakSum = 0, peakSqSum = 0;
float energySum = 0;
unsigned long sampleCount = 0;

float baseline = 2000.0f;
int prev1 = 0, prev2 = 0;
bool armed = true;

// ================== STATUS PRINT CONTROL ==================
enum StatusState { ST_NONE, ST_DISCONNECTED, ST_AUTHORIZED, ST_UNAUTHORIZED };
StatusState lastStatus = ST_NONE;

void printStatusOnce(StatusState s) {
  if (s == lastStatus) return;  // ✅ print only on change
  lastStatus = s;

  if (s == ST_DISCONNECTED) {
    Serial.println("⚠️ Electrodes not connected");
  } else if (s == ST_AUTHORIZED) {
    Serial.println("✅ VIRAJ IS WEARING THE WATCH");
  } else if (s == ST_UNAUTHORIZED) {
    Serial.println("🚨 UNAUTHORIZED PERSON WEARING THE WATCH");
  }
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

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);

  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(ECG_PIN, ADC_11db);

  resetWindow();

  Serial.println("ECG Verification Started");
  Serial.print("AUTH_THRESHOLD = "); Serial.println(AUTH_THRESHOLD, 6);
}

// ================== LOOP ==================
void loop() {
  // sample timing
  if (millis() - lastSample < SAMPLE_INTERVAL_MS) return;
  lastSample = millis();

  // If electrodes disconnected -> print once and do nothing else
  if (!electrodesConnected()) {
    printStatusOnce(ST_DISCONNECTED);
    return;
  }

  int ecg = analogRead(ECG_PIN);
  if (!validSample(ecg)) return;

  // update baseline and dynamic threshold
  baseline = (1.0f - BASELINE_ALPHA) * baseline + BASELINE_ALPHA * (float)ecg;
  float dynThresh = baseline + THRESH_MARGIN;

  // accumulate energy
  energySum += (float)ecg * (float)ecg;
  sampleCount++;

  unsigned long now = millis();

  // -------- peak detection: local max above dynThresh --------
  if (armed) {
    if ((float)prev1 > dynThresh && prev1 > prev2 && prev1 >= ecg) {
      if (lastPeakTime != 0 && (now - lastPeakTime) > REFRACTORY_MS) {
        float rr = (float)(now - lastPeakTime);
        rrSum += rr;
        rrSqSum += rr * rr;

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

  // -------- end of window: compute features + verify --------
  if (now - windowStart >= WINDOW_MS) {

    if (sampleCount < 50) {
      // not enough data, don't spam status; just reset
      resetWindow();
      return;
    }

    float meanRR = -1, stdRR = -1;
    if (beatCount >= 3) {
      float rrN = (float)(beatCount - 1);
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
      score += z2(meanRR,        MU_mean_rr_ms, SD_mean_rr_ms);
      score += z2(stdRR,         MU_std_rr_ms,  SD_std_rr_ms);
      score += z2(meanPeak,      MU_mean_peak,  SD_mean_peak);
      score += z2(stdPeak,       MU_std_peak,   SD_std_peak);
      score += z2(energy,        MU_energy,     SD_energy);
      score = sqrtf(score);

      authorized = (score <= AUTH_THRESHOLD);
    }

    // ✅ Print only if status changed
    if (authorized) printStatusOnce(ST_AUTHORIZED);
    else            printStatusOnce(ST_UNAUTHORIZED);

    // Optional: if you want to see score ONLY when status changes, uncomment:
    // Serial.print("score="); Serial.println(score, 3);

    resetWindow();
  }
}
