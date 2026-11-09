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

// LAN dashboard access only. HTTP Basic does not encrypt network traffic.
#define DASHBOARD_USERNAME "INSERT_YOUR_USERNAME"
#define DASHBOARD_PASSWORD "INSERT_A_PRIVATE_PASSWORD_12_PLUS_CHARS"

// Obtain an ID token for your own Firebase user; it is short lived.
// This prototype does not automatically refresh tokens. Never commit a live token.
#define FIREBASE_ID_TOKEN "INSERT_CURRENT_FIREBASE_ID_TOKEN"
// Use the trusted root CA for your own database endpoint, in PEM format.
// Do not use setInsecure() or guess/paste a certificate from an untrusted source.
static const char FIREBASE_ROOT_CA[] = R"PEM(INSERT_TRUSTED_ROOT_CA_PEM_HERE)PEM";
