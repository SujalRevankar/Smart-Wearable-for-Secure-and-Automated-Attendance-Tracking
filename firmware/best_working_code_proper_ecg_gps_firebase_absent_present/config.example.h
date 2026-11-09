#pragma once
// Copy to config.local.h in THIS sketch folder. Never commit config.local.h.
// Placeholders are deliberately disabled to prevent use as an enrolled profile.
#define PROJECT_CONFIGURED 0
#if !PROJECT_CONFIGURED
#error "Fill in private configuration, then set PROJECT_CONFIGURED to 1. See docs/CONFIGURATION.md."
#endif

#define WIFI_SSID "INSERT_YOUR_WIFI_NAME"
#define WIFI_PASSWORD "INSERT_YOUR_WIFI_PASSWORD"

// Host only: omit https:// and a trailing slash.
#define FIREBASE_HOST "YOUR_FIREBASE_PROJECT-default-rtdb.firebaseio.com"
#define STUDENT_NAME "YOUR_STUDENT_ID"
const double homeLat = 0.0; // PLACEHOLDER: your geofence centre latitude
const double homeLng = 0.0; // PLACEHOLDER: your geofence centre longitude

// Private enrollment statistics: replace every placeholder using your own notebook output.
const float MU_beats = 0.0f; // PLACEHOLDER
const float MU_mean_rr_ms = 0.0f; // PLACEHOLDER
const float MU_std_rr_ms = 0.0f; // PLACEHOLDER
const float MU_mean_peak = 0.0f; // PLACEHOLDER
const float MU_std_peak = 0.0f; // PLACEHOLDER
const float MU_energy = 0.0f; // PLACEHOLDER
const float SD_beats = 1.0f; // PLACEHOLDER
const float SD_mean_rr_ms = 1.0f; // PLACEHOLDER
const float SD_std_rr_ms = 1.0f; // PLACEHOLDER
const float SD_mean_peak = 1.0f; // PLACEHOLDER
const float SD_std_peak = 1.0f; // PLACEHOLDER
const float SD_energy = 1.0f; // PLACEHOLDER
float AUTH_THRESHOLD = 0.0f; // PLACEHOLDER: choose and validate with your enrollment data.
