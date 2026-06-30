# Ambient Monitor — Firmware (ESP32)

Firmware for an IoT environmental monitor node. An **ESP32** reads **temperature, humidity,
ambient light and motion** from a room and streams the readings to **Firebase Realtime Database**,
where the [Ambient Monitor dashboard](https://github.com/Kennethch-02/ambient-monitor-dashboard)
visualizes them live.

**Live demo:** https://ambient.teonix.dev

This is the **producer** half of a two-repo product:

- **Firmware** (this repo) — `ambient_monitor_scripts` · ESP32 / PlatformIO / C++
- **Dashboard** — `ambient-monitor-dashboard` · React / Vite

---

## Hardware

**Board:** ESP32 (PlatformIO env `upesy_wroom` — works with generic WROOM-32 dev boards).

### Bill of materials
| Qty | Part | Notes |
|-----|------|-------|
| 1 | ESP32 dev board (`upesy_wroom` / WROOM-32) | 2.4 GHz Wi-Fi only |
| 1 | DHT22 (AM2302) | temperature + humidity |
| 1 | BH1750 (GY-302) | ambient light (I²C) |
| 1 | PIR sensor (e.g. HC-SR501) | motion — *substitutable* |
| 1 | RGB LED (+ 3× ~220 Ω resistors) | status indicator — *substitutable* |
| — | Breadboard + jumper wires, USB cable | |

### Pinout
| Component | ESP32 pin | Notes |
|-----------|-----------|-------|
| DHT22 data | **GPIO 4** | 3.3 V |
| BH1750 SDA | **GPIO 21** | I²C |
| BH1750 SCL | **GPIO 22** | I²C |
| PIR output | **GPIO 13** | active-low in firmware (`!digitalRead`) |
| RGB LED — Red | **GPIO 25** | |
| RGB LED — Green | **GPIO 26** | |
| RGB LED — Blue | **GPIO 27** | |
| Battery sense | **GPIO 35** | battery + → 2:1 divider (e.g. 100k/100k) → GPIO35; ADC1, input-only |

### Status-LED legend
| LED | Meaning |
|-----|---------|
| 🟢 Green (solid) | OK |
| 🔵 Blue (blink) | Connecting to Wi-Fi |
| 🔴 Red (blink) | Sensor error |
| 🔴/🔵 Red↔Blue (alternating) | Firebase error |
| 🟢+🔵 Green+Blue | Motion detected |
| 🔵 Blue (blink) | PIR calibrating (first 30 s) |
| ⚪ White (blink) | Initializing |

---

## Setup

### 1. Secrets
Credentials live in `include/secrets.h`, which is **gitignored** — never commit it (this repo is public).

```powershell
Copy-Item include/secrets.example.h include/secrets.h
```

Then fill in `include/secrets.h`:

| Define | Where to get it |
|--------|-----------------|
| `WIFI_SSID`, `WIFI_PASSWORD` | Your **2.4 GHz** network (the ESP32 cannot see 5 GHz) |
| `FIREBASE_PROJECT_ID` | Firebase console → Project settings |
| `API_KEY` | Firebase console → Project settings → Web API Key |
| `DATABASE_URL` | Realtime Database URL (`https://<project>-default-rtdb.firebaseio.com`) |
| `DEVICE_EMAIL`, `DEVICE_PASSWORD` | An Email/Password user you create in Firebase Auth (see below) |

### 2. Firebase
In the [Firebase console](https://console.firebase.google.com):

1. **Authentication → Sign-in method:** enable **Email/Password** and **Anonymous**
   (the dashboard reads anonymously).
2. **Authentication → Users → Add user:** create the device account → its email/password become
   `DEVICE_EMAIL` / `DEVICE_PASSWORD`.
3. **Realtime Database:** create it (note the URL for `DATABASE_URL`).
4. **Rules:** publish [`database.rules.json`](./database.rules.json) — public read, authenticated
   write (the device writes; the dashboard reads).

### 3. Build & flash (PlatformIO)
1. Install [PlatformIO](https://platformio.org/) (VS Code extension or CLI) and your board's
   USB driver (CP2102 or CH340).
2. Open this folder in VS Code → **Build** → **Upload** → **Monitor** (115200 baud).

On boot the node calibrates the PIR for 30 s (keep the area still), syncs time over NTP, connects
to Firebase, then runs. A solid green LED means everything is OK.

---

## Data written to Firebase

```
/ambient_data/{YYYYMMDDHHMMSS}   → { "temperatura": float, "humedad": float, "luz": float,
                                     "timestamp": "YYYY-MM-DD HH:MM:SS" }
/motion_events/{YYYYMMDDHHMMSS}  → { "detectado": bool }
```

- Ambient readings are sent every **60 s**; motion is sampled every **1 s** and written only on
  **state change**.
- Field names are in Spanish; the dashboard maps them to English on read.
- Timestamps use **UTC-6** (Costa Rica), no DST.

---

## Configuration reference (`src/main.cpp`)
| Constant | Default | Meaning |
|----------|---------|---------|
| `MAIN_INTERVAL` | 60000 ms | Ambient-data send interval |
| `MOTION_SAMPLE_INTERVAL` | 1000 ms | PIR sampling interval |
| `PIR_CALIBRATION_TIME` | 30000 ms | PIR warm-up at boot |
| `GMT_OFFSET_SEC` | -6×3600 | Timezone offset (Costa Rica) |

---

## Known limitations / roadmap
- No automatic Wi-Fi/Firebase reconnect in `loop()` — if Wi-Fi drops after boot, a reset is needed.
- `getTimeStamp()` falls back to `millis()` if NTP fails, producing a non-standard key — better to
  skip the write when time is invalid.
- TLS uses `setInsecure()` (no certificate validation).

> Dependencies are version-pinned in `platformio.ini` to a known-good build. `FirebaseClient`
> (mobizt) changes its API across releases — bump it deliberately.
