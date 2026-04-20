/**
 * LIS3DHTR - Grabación a 5 Hz en microSD (CSV + metadata sidecar JSON)
 *
 * Placa  : T-A7670X-S3-Standard (ESP32-S3)
 * Sensor : LIS3DHTR (Seeed / STMicro)
 * Conexión I2C : SDA → GPIO 3  | SCL → GPIO 2
 * Conexión SD  : SCK → GPIO 12 | MISO → GPIO 13 | MOSI → GPIO 11 | CS → GPIO 10
 *
 * Comportamiento:
 *   1. Lee /device.json (vehicle_id, device_id) y /session.json (driver_id,
 *      time_of_day, notes) desde la SD. Si faltan, marca UNKNOWN.
 *   2. Lee el contador en /counter.txt y lo incrementa.
 *   3. Escribe /data_XXXX.meta.json con la metadata congelada al arranque.
 *   4. Abre /data_XXXX.csv y graba 60 s a 5 Hz (ODR hw 10 Hz, thinning 200 ms).
 *   5. Cierra el archivo y entra en idle.
 */

#include <Arduino.h>
#include <Wire.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "LIS3DHTR.h"

// ── Pines I2C ────────────────────────────────────────────────────────────────
#define I2C_SDA   3
#define I2C_SCL   2

// ── Pines SD (T-A7670X-S3-Standard) ─────────────────────────────────────────
#define SD_SCK    12
#define SD_MISO   13
#define SD_MOSI   11
#define SD_CS     10

// ── Temporización ─────────────────────────────────────────────────────────────
#define SAMPLE_INTERVAL_MS   200          // 200 ms = 5 Hz
#define RECORD_DURATION_MS   60000UL      // 1 minuto
#define SAMPLE_RATE_HZ       5
#define RECORD_DURATION_S    60

// ── Archivos de configuración ────────────────────────────────────────────────
#define COUNTER_FILE   "/counter.txt"
#define DEVICE_FILE    "/device.json"
#define SESSION_FILE   "/session.json"
#define FIRMWARE_VER   "0.2.0"
#define UNKNOWN_STR    "UNKNOWN"

struct DeviceCfg {
    String vehicle_id = UNKNOWN_STR;
    String device_id  = UNKNOWN_STR;
};

struct SessionCfg {
    String driver_id    = UNKNOWN_STR;
    String time_of_day  = "unknown";
    String notes        = "";
};

LIS3DHTR<TwoWire> lis;

// ── Helpers de carga de configuración ────────────────────────────────────────
static void loadDeviceConfig(DeviceCfg& cfg)
{
    File f = SD.open(DEVICE_FILE, FILE_READ);
    if (!f) {
        Serial.printf("[WARN] %s no encontrado. vehicle_id=%s\n",
                      DEVICE_FILE, UNKNOWN_STR);
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[WARN] %s JSON inválido (%s). vehicle_id=%s\n",
                      DEVICE_FILE, err.c_str(), UNKNOWN_STR);
        return;
    }

    if (doc["vehicle_id"].is<const char*>()) cfg.vehicle_id = (const char*)doc["vehicle_id"];
    if (doc["device_id"].is<const char*>())  cfg.device_id  = (const char*)doc["device_id"];
}

static void loadSessionConfig(SessionCfg& cfg)
{
    File f = SD.open(SESSION_FILE, FILE_READ);
    if (!f) {
        Serial.printf("[WARN] %s no encontrado. driver_id=%s\n",
                      SESSION_FILE, UNKNOWN_STR);
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[WARN] %s JSON inválido (%s). driver_id=%s\n",
                      SESSION_FILE, err.c_str(), UNKNOWN_STR);
        return;
    }

    if (doc["driver_id"].is<const char*>())   cfg.driver_id   = (const char*)doc["driver_id"];
    if (doc["time_of_day"].is<const char*>()) cfg.time_of_day = (const char*)doc["time_of_day"];
    if (doc["notes"].is<const char*>())       cfg.notes       = (const char*)doc["notes"];
}

// Devuelve el próximo sessionID y actualiza /counter.txt
static uint32_t nextSessionID()
{
    uint32_t id = 1;

    File f = SD.open(COUNTER_FILE, FILE_READ);
    if (f) {
        String line = f.readStringUntil('\n');
        f.close();
        uint32_t prev = (uint32_t)line.toInt();
        if (prev > 0) id = prev + 1;
    }

    File fw = SD.open(COUNTER_FILE, FILE_WRITE);
    if (fw) {
        fw.printf("%u\n", id);
        fw.close();
    } else {
        Serial.println("[WARN] No se pudo actualizar counter.txt");
    }

    return id;
}

static void writeSessionMeta(uint32_t sessionID,
                             const DeviceCfg& dev,
                             const SessionCfg& ses,
                             uint32_t bootMillis)
{
    char metaPath[32];
    snprintf(metaPath, sizeof(metaPath), "/data_%04u.meta.json", sessionID);

    JsonDocument doc;
    doc["session_id"]        = sessionID;
    doc["vehicle_id"]        = dev.vehicle_id;
    doc["device_id"]         = dev.device_id;
    doc["firmware"]          = FIRMWARE_VER;
    doc["driver_id"]         = ses.driver_id;
    doc["time_of_day"]       = ses.time_of_day;
    doc["notes"]             = ses.notes;
    doc["sample_rate_hz"]    = SAMPLE_RATE_HZ;
    doc["record_duration_s"] = RECORD_DURATION_S;
    doc["boot_millis"]       = bootMillis;
    doc["started_at_iso"]    = nullptr;  // hook para RTC/GPS en iteración futura

    File fw = SD.open(metaPath, FILE_WRITE);
    if (!fw) {
        Serial.printf("[WARN] No se pudo crear %s. Se graba CSV sin sidecar.\n", metaPath);
        return;
    }

    if (serializeJsonPretty(doc, fw) == 0) {
        Serial.printf("[WARN] Fallo al serializar metadata a %s\n", metaPath);
    }
    fw.close();
    Serial.printf("Metadata: %s\n", metaPath);
}

void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println("=== LIS3DHTR → SD CSV logger ===");

    // ── Init I2C + acelerómetro ───────────────────────────────────────────────
    Wire.begin(I2C_SDA, I2C_SCL);

    lis.begin(Wire, LIS3DHTR_ADDRESS_UPDATED);  // 0x19; usa 0x18 si SDO a GND
    if (!lis) {
        Serial.println("[ERROR] LIS3DHTR no encontrado. Verifica conexión I2C.");
        while (true) { delay(1000); }
    }
    lis.setOutputDataRate(LIS3DHTR_DATARATE_10HZ);
    lis.setFullScaleRange(LIS3DHTR_RANGE_2G);
    Serial.println("Sensor OK. ODR=10 Hz, rango=±2 g.");

    // ── Init SPI + SD ─────────────────────────────────────────────────────────
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS)) {
        Serial.println("[ERROR] SD no detectada. Verifica tarjeta y conexiones.");
        while (true) { delay(1000); }
    }
    Serial.printf("SD OK. Capacidad: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));

    // ── Cargar metadata de dispositivo y sesión ──────────────────────────────
    DeviceCfg device;
    SessionCfg session;
    loadDeviceConfig(device);
    loadSessionConfig(session);
    Serial.printf("vehicle_id=%s  driver_id=%s  time_of_day=%s\n",
                  device.vehicle_id.c_str(), session.driver_id.c_str(),
                  session.time_of_day.c_str());

    // ── Obtener ID de sesión y escribir sidecar de metadata ──────────────────
    uint32_t sessionID = nextSessionID();
    uint32_t bootMillis = millis();
    writeSessionMeta(sessionID, device, session, bootMillis);

    // ── Abrir archivo CSV ────────────────────────────────────────────────────
    char filename[24];
    snprintf(filename, sizeof(filename), "/data_%04u.csv", sessionID);

    File dataFile = SD.open(filename, FILE_WRITE);
    if (!dataFile) {
        Serial.printf("[ERROR] No se pudo crear %s\n", filename);
        while (true) { delay(1000); }
    }

    dataFile.println("timestamp_ms,X_g,Y_g,Z_g");
    Serial.printf("Grabando sesión #%u → %s (60 s a 5 Hz)...\n", sessionID, filename);
    Serial.println("timestamp_ms,X_g,Y_g,Z_g");

    // ── Bucle de grabación (bloqueante, 60 s) ─────────────────────────────────
    uint32_t startTime = millis();
    uint32_t lastSample = startTime;

    while (millis() - startTime < RECORD_DURATION_MS) {
        uint32_t now = millis();
        if (now - lastSample >= SAMPLE_INTERVAL_MS) {
            lastSample = now;

            float x = lis.getAccelerationX();
            float y = lis.getAccelerationY();
            float z = lis.getAccelerationZ();

            dataFile.printf("%lu,%.4f,%.4f,%.4f\n", (unsigned long)now, x, y, z);
            Serial.printf("%lu,%.4f,%.4f,%.4f\n",   (unsigned long)now, x, y, z);
        }
    }

    dataFile.close();
    Serial.printf("Grabación completa. Archivo guardado: %s\n", filename);
    Serial.println("--- idle ---");
}

void loop()
{
    delay(1000);
}
