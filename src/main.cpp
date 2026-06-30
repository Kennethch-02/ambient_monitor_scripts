#include <Arduino.h>
#include <Wire.h>
#include <DHT.h>
#include <BH1750.h>
#include <WiFi.h>
#include <FirebaseClient.h>
#include <WiFiClientSecure.h>
#include "time.h"

// Configuración WiFi
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Configuración Firebase
#define FIREBASE_PROJECT_ID ""
#define API_KEY ""
#define DATABASE_URL ""
#define DEVICE_EMAIL ""
#define DEVICE_PASSWORD ""

// Configuración NTP
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC (-6 * 3600)  // UTC-6 (Costa Rica)
#define DAYLIGHT_OFFSET_SEC 0       // Costa Rica no usa horario de verano

// Definición de pines
#define DHT_PIN 4
#define LED_R 25
#define LED_G 26
#define LED_B 27
#define PIR_PIN 13
#define DHTTYPE DHT22

// Configuración de tiempos
#define MOTION_SAMPLE_INTERVAL 1000  // 1 segundo    
#define MAIN_INTERVAL 60000         // 1 minuto
#define LED_BLINK_INTERVAL 500     
#define PIR_CALIBRATION_TIME 30000  // 30 segundos de calibración

// Estados del sistema
enum SystemState {
    STATE_OK,
    STATE_WIFI_CONNECTING,
    STATE_SENSOR_ERROR,
    STATE_FIREBASE_ERROR,
    STATE_MOTION_DETECTED,
    STATE_INITIALIZING,
    STATE_CALIBRATING
};

// Estructura para datos ambientales
struct AmbientData {
    float temperature;
    float humidity;
    float light;
    String timestamp;
};

// Inicialización de sensores
DHT dht(DHT_PIN, DHTTYPE);
BH1750 lightMeter;

// Objetos para Firebase
DefaultNetwork network;
WiFiClientSecure ssl_client;
AsyncClientClass aClient(ssl_client, getNetwork(network));
FirebaseApp app;
RealtimeDatabase Database;

// Variables globales
unsigned long lastMotionCheck = 0;
unsigned long lastMainUpdate = 0;
unsigned long lastLedUpdate = 0;
SystemState currentState = STATE_INITIALIZING;
bool ledBlinkState = false;
struct tm timeinfo;
unsigned long pirCalibrationStartTime = 0;
bool pirCalibrated = false;
bool lastMotionState = false;

void initTime() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    
    Serial.print("Esperando hora NTP");
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 10) {
        Serial.print(".");
        delay(500);
        retry++;
    }
    Serial.println();
    
    if(getLocalTime(&timeinfo)) {
        Serial.println("Hora configurada correctamente");
        char timeStringBuff[50];
        strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
        Serial.println(timeStringBuff);
    } else {
        Serial.println("Error obteniendo hora");
        currentState = STATE_FIREBASE_ERROR;
    }
}

String getTimeStamp() {
    if(!getLocalTime(&timeinfo)) {
        Serial.println("Error obteniendo hora actual");
        return String(millis());
    }
    char timestamp[21];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", &timeinfo);
    return String(timestamp);
}

String getFormattedTime() {
    if(!getLocalTime(&timeinfo)) {
        return "Error de hora";
    }
    char timeStr[25];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStr);
}

void updateLEDs() {
    unsigned long currentMillis = millis();
    bool shouldBlink = (currentMillis - lastLedUpdate) >= LED_BLINK_INTERVAL;
    
    if (shouldBlink) {
        lastLedUpdate = currentMillis;
        ledBlinkState = !ledBlinkState;
    }

    digitalWrite(LED_R, LOW);
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_B, LOW);

    switch (currentState) {
        case STATE_OK:
            digitalWrite(LED_G, HIGH);
            break;
        case STATE_WIFI_CONNECTING:
            digitalWrite(LED_B, ledBlinkState);
            break;
        case STATE_SENSOR_ERROR:
            digitalWrite(LED_R, ledBlinkState);
            break;
        case STATE_FIREBASE_ERROR:
            if (ledBlinkState) {
                digitalWrite(LED_R, HIGH);
            } else {
                digitalWrite(LED_B, HIGH);
            }
            break;
        case STATE_MOTION_DETECTED:
            digitalWrite(LED_G, HIGH);
            digitalWrite(LED_B, HIGH);
            break;
        case STATE_CALIBRATING:
            if (ledBlinkState) {
                digitalWrite(LED_B, HIGH);
            }
            break;
        case STATE_INITIALIZING:
            if (ledBlinkState) {
                digitalWrite(LED_R, HIGH);
                digitalWrite(LED_G, HIGH);
                digitalWrite(LED_B, HIGH);
            }
            break;
    }
}

void connectToWiFi() {
    currentState = STATE_WIFI_CONNECTING;
    Serial.print("\nConectando a WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000) {
        Serial.print(".");
        updateLEDs();
        delay(300);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConectado a WiFi!");
        Serial.print("Dirección IP: ");
        Serial.println(WiFi.localIP());
        currentState = STATE_OK;
    } else {
        Serial.println("\nError conectando a WiFi!");
        currentState = STATE_FIREBASE_ERROR;
    }
}

void initFirebase() {
    ssl_client.setInsecure();
    
    // Crear autenticación de usuario
    UserAuth auth(API_KEY, DEVICE_EMAIL, DEVICE_PASSWORD);
    
    // Inicializar Firebase con la autenticación
    initializeApp(aClient, app, getAuth(auth));
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
}

String createAmbientJsonString(const AmbientData& data) {
    String json = "{";
    json += "\"temperatura\":" + String(data.temperature) + ",";
    json += "\"humedad\":" + String(data.humidity) + ",";
    json += "\"luz\":" + String(data.light) + ",";
    json += "\"timestamp\":\"" + data.timestamp + "\"";
    json += "}";
    return json;
}

void sendAmbientData(const AmbientData& data) {
    String timeKey = getTimeStamp();
    String path = "/ambient_data/" + timeKey;
    String jsonStr = createAmbientJsonString(data);
    
    if (!Database.set(aClient, path.c_str(), object_t(jsonStr.c_str()))) {
        currentState = STATE_FIREBASE_ERROR;
    }
}

void sendMotionEvent(bool detected) {
    String timeKey = getTimeStamp();
    String path = "/motion_events/" + timeKey;
    String json = "{\"detectado\":" + String(detected ? "true" : "false") + "}";
    
    if (!Database.set(aClient, path.c_str(), object_t(json.c_str()))) {
        currentState = STATE_FIREBASE_ERROR;
    }
}

void initPIR() {
    pinMode(PIR_PIN, INPUT);
    pirCalibrationStartTime = millis();
    currentState = STATE_CALIBRATING;
    Serial.println("Iniciando calibración del sensor de movimiento (30 segundos)...");
    Serial.println("Mantenga el área libre de movimiento durante la calibración.");
}

bool checkPIRCalibration() {
    if (!pirCalibrated && millis() - pirCalibrationStartTime >= PIR_CALIBRATION_TIME) {
        pirCalibrated = true;
        Serial.println("¡Calibración del sensor completada!");
        return true;
    }
    return pirCalibrated;
}

void checkSensors(AmbientData &data) {
    data.temperature = dht.readTemperature();
    data.humidity = dht.readHumidity();
    data.light = lightMeter.readLightLevel();
    data.timestamp = getFormattedTime();
    
    if (isnan(data.humidity) || isnan(data.temperature) || data.light < 0) {
        currentState = STATE_SENSOR_ERROR;
        return;
    }
    
    if (currentState == STATE_SENSOR_ERROR) {
        currentState = STATE_OK;
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n--------------------");
    Serial.println("Iniciando sistema");
    Serial.println("--------------------");
    
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(LED_B, OUTPUT);
    
    Wire.begin(21, 22);
    dht.begin();
    initPIR();
    
    if (!lightMeter.begin()) {
        Serial.println("✗ Error BH1750");
        currentState = STATE_SENSOR_ERROR;
    }
    
    connectToWiFi();
    initTime();
    initFirebase();
}

void loop() {
    app.loop();
    Database.loop();
    
    unsigned long currentMillis = millis();
    
    // Verificar calibración del sensor
    if (!pirCalibrated) {
        if (checkPIRCalibration()) {
            currentState = STATE_OK;
        }
        updateLEDs();
        return;
    }
    
    // Verificar movimiento cada segundo
    if (currentMillis - lastMotionCheck >= MOTION_SAMPLE_INTERVAL) {
        lastMotionCheck = currentMillis;
        bool motion = !digitalRead(PIR_PIN);  // PIR activo bajo
        
        // Solo registrar cambios en el estado del movimiento
        if (motion != lastMotionState) {
            lastMotionState = motion;
            
            if (motion) {
                currentState = STATE_MOTION_DETECTED;
                Serial.println("¡Movimiento detectado! - " + getFormattedTime());
            } else {
                currentState = STATE_OK;
                Serial.println("Movimiento terminado - " + getFormattedTime());
            }
            
            // Enviar evento de movimiento inmediatamente
            sendMotionEvent(motion);
        }
    }
    
    // Actualización de datos ambientales cada minuto
    if (currentMillis - lastMainUpdate >= MAIN_INTERVAL) {
        lastMainUpdate = currentMillis;
        
        AmbientData data;
        checkSensors(data);
        
        // Imprimir lecturas
        Serial.println("\n║ LECTURAS DE SENSORES ║");
        Serial.println("╠═══════════════════════");
        Serial.printf("║ Hora: %s\n", data.timestamp.c_str());
        Serial.printf("║ Estado: %s\n", 
            currentState == STATE_OK ? "OK" :
            currentState == STATE_SENSOR_ERROR ? "Error en sensores" :
            currentState == STATE_FIREBASE_ERROR ? "Error en Firebase" :
            currentState == STATE_MOTION_DETECTED ? "Movimiento detectado" :
            currentState == STATE_CALIBRATING ? "Calibrando" :
            "Desconocido");
        
        if (currentState != STATE_SENSOR_ERROR) {
            Serial.printf("║ Temperatura: %5.1f °C\n", data.temperature);
            Serial.printf("║ Humedad:     %5.1f %%\n", data.humidity);
            Serial.printf("║ Luz:         %5.1f lx\n", data.light);
        }
        Serial.println("╚═══════════════════════");
        
        if (currentState != STATE_SENSOR_ERROR) {
            sendAmbientData(data);
        }
    }
    
    updateLEDs();
}