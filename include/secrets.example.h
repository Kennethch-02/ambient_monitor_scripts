// secrets.example.h — TEMPLATE (safe to commit, no real values).
//
// SETUP:
//   1. Copy this file to  include/secrets.h
//   2. Fill in your real WiFi + Firebase credentials there.
//   3. include/secrets.h is gitignored and must NEVER be committed (public repo).
//
// PowerShell (Windows):
//   Copy-Item include/secrets.example.h include/secrets.h

#ifndef SECRETS_H
#define SECRETS_H

// ---- WiFi ----
#define WIFI_SSID           "your-wifi-ssid"
#define WIFI_PASSWORD       "your-wifi-password"

// ---- Firebase (project: ambientmonitor-e059c) ----
#define FIREBASE_PROJECT_ID "your-project-id"
#define API_KEY             "your-web-api-key"
#define DATABASE_URL        "https://your-project-default-rtdb.firebaseio.com"
#define DEVICE_EMAIL        "device@example.com"
#define DEVICE_PASSWORD     "device-account-password"

#endif // SECRETS_H
