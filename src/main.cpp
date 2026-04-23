/**
 * Stage 1 MVP — Logger LIS3DHTR a 5 Hz con subida a S3 cada 10 min.
 *
 * Placa  : T-A7670X-S3-Standard (ESP32-S3)
 * Sensor : LIS3DHTR (Seeed / STMicro)  I2C: SDA=GPIO 3, SCL=GPIO 2
 * SD     : SCK=GPIO 12, MISO=GPIO 13, MOSI=GPIO 11, CS=GPIO 10
 * Módem  : A7670G-LLSE en Serial1 (RX=5, TX=4, PWRKEY=46)
 *
 * Flujo:
 *   1. Init I2C+sensor, SD, módem (registro + APN bam.entelpcs.cl + DNS).
 *   2. Sincroniza epoch UTC con la red. Crea /session_<epoch>.csv en SD.
 *   3. Loop cooperativo: muestra cada 200 ms; cada 10 min PUT del CSV
 *      completo a s3://<bucket>/lilygo-a7670/session_<epoch>.csv (sobrescribe).
 *   4. Sin boot sync, sin reintentos: si falla un PUT, se reintenta al siguiente
 *      ciclo de 10 min con el archivo más grande.
 */

#include <Arduino.h>
#include <Wire.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include "LIS3DHTR.h"

#include "modem_net.h"
#include "aws_uploader.h"

// ── Pines ────────────────────────────────────────────────────────────────────
#define I2C_SDA   3
#define I2C_SCL   2
#define SD_SCK    12
#define SD_MISO   13
#define SD_MOSI   11
#define SD_CS     10

// ── Temporización ────────────────────────────────────────────────────────────
#define SAMPLE_INTERVAL_MS   200UL          // 5 Hz
// 5 min → ~88 KB por chunk (5 Hz × 300 s × ~59 B/línea). Queda bajo el
// límite de ~96 KB del comando +HTTPDATA del A7670 con margen de ~8 KB.
// Si se cambia el sample rate o el esquema de línea, recalcular.
#define UPLOAD_INTERVAL_MS   (5UL * 60UL * 1000UL)  // 5 min (producción)
#define SAMPLE_RATE_HZ       5

// ── Config ──────────────────────────────────────────────────────────────────
#define DEVICE_FILE    "/device.json"
#define SESSION_FILE   "/session.json"
#define FIRMWARE_VER   "0.3.0-stage1"
#define UNKNOWN_STR    "UNKNOWN"
#define APN            "bam.entelpcs.cl"

#ifndef AWS_S3_PREFIX
#define AWS_S3_PREFIX "lilygo-a7670/"
#endif

// El header del CSV se reescribe en cada rotación de chunk.
#define CSV_HEADER "host_time_iso,device_time_us,ax_g,ay_g,az_g,device_event_id"

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

static uint32_t s_epochAtBoot   = 0;
static uint32_t s_millisAtBoot  = 0;
static char     s_csvPath[40]   = {0};
static char     s_s3Prefix[64]  = {0};   // "lilygo-a7670/session_<epoch>/"
static uint16_t s_chunkSeq      = 1;     // próximo chunk a subir
static uint32_t s_samplesSinceUpload = 0;
static uint32_t s_lastUploadMs  = 0;

// ── Configuración persistente ────────────────────────────────────────────────
static void loadDeviceConfig(DeviceCfg& cfg)
{
    File f = SD.open(DEVICE_FILE, FILE_READ);
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        if (doc["vehicle_id"].is<const char*>()) cfg.vehicle_id = (const char*)doc["vehicle_id"];
        if (doc["device_id"].is<const char*>())  cfg.device_id  = (const char*)doc["device_id"];
    }
    f.close();
}

static void loadSessionConfig(SessionCfg& cfg)
{
    File f = SD.open(SESSION_FILE, FILE_READ);
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        if (doc["driver_id"].is<const char*>())   cfg.driver_id   = (const char*)doc["driver_id"];
        if (doc["time_of_day"].is<const char*>()) cfg.time_of_day = (const char*)doc["time_of_day"];
        if (doc["notes"].is<const char*>())       cfg.notes       = (const char*)doc["notes"];
    }
    f.close();
}

// host_time_iso = epoch_at_boot + (millis_now - millis_at_boot)/1000.
static void hostTimeIso(uint32_t nowMs, char out[25])
{
    uint32_t elapsedMs  = nowMs - s_millisAtBoot;
    uint32_t epochNow   = s_epochAtBoot + (elapsedMs / 1000UL);
    uint16_t fracMillis = (uint16_t)(elapsedMs % 1000UL);
    time_t t = (time_t)epochNow;
    struct tm tm_;
    gmtime_r(&t, &tm_);
    snprintf(out, 25, "%04d-%02d-%02dT%02d:%02d:%02d.%03uZ",
             tm_.tm_year + 1900, tm_.tm_mon + 1, tm_.tm_mday,
             tm_.tm_hour, tm_.tm_min, tm_.tm_sec, fracMillis);
}

static uint32_t epochNow()
{
    return s_epochAtBoot + (millis() - s_millisAtBoot) / 1000UL;
}

static void writeSessionMeta(const DeviceCfg& dev, const SessionCfg& ses)
{
    char metaPath[48];
    snprintf(metaPath, sizeof(metaPath), "/session_%u.meta.json", s_epochAtBoot);

    JsonDocument doc;
    doc["session_epoch"]     = s_epochAtBoot;
    doc["s3_prefix"]         = s_s3Prefix;
    doc["chunk_format"]      = "part_NNNN.csv";
    doc["vehicle_id"]        = dev.vehicle_id;
    doc["device_id"]         = dev.device_id;
    doc["firmware"]          = FIRMWARE_VER;
    doc["driver_id"]         = ses.driver_id;
    doc["time_of_day"]       = ses.time_of_day;
    doc["notes"]             = ses.notes;
    doc["sample_rate_hz"]    = SAMPLE_RATE_HZ;
    doc["upload_interval_s"] = UPLOAD_INTERVAL_MS / 1000UL;

    char isoBoot[25];
    hostTimeIso(s_millisAtBoot, isoBoot);
    doc["started_at_iso"] = isoBoot;

    File fw = SD.open(metaPath, FILE_WRITE);
    if (!fw) {
        Serial.printf("[meta][WARN] No se pudo crear %s\n", metaPath);
        return;
    }
    serializeJsonPretty(doc, fw);
    fw.close();
    Serial.printf("[meta] %s\n", metaPath);
}

// ── Init helpers ─────────────────────────────────────────────────────────────
static void initSensor()
{
    Wire.begin(I2C_SDA, I2C_SCL);
    lis.begin(Wire, LIS3DHTR_ADDRESS_UPDATED);
    if (!lis) {
        Serial.println("[sensor][ERR] LIS3DHTR no encontrado.");
        while (true) delay(1000);
    }
    lis.setOutputDataRate(LIS3DHTR_DATARATE_10HZ);
    lis.setFullScaleRange(LIS3DHTR_RANGE_2G);
    Serial.println("[sensor] OK (ODR=10 Hz, ±2 g)");
}

static void initSd()
{
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS)) {
        Serial.println("[sd][ERR] SD no detectada.");
        while (true) delay(1000);
    }
    Serial.printf("[sd] OK, %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
}

static void initNetwork()
{
    if (!ModemNet::begin()) {
        Serial.println("[net][ERR] init módem falló. Idle.");
        while (true) delay(5000);
    }

    // Attach → DNS → sync como un bloque atómico. Si CUALQUIER paso falla,
    // hacemos hardRestart (CFUN=0/CFUN=1) y reintentamos el bloque entero.
    // Motivación: observado que BAM asigna IPs del pool CGNAT 100.x.x.x con
    // routing roto para outbound HTTP — el gate DNS pasa pero el HTTP real
    // falla (706 → 713). Un power-cycle manual no siempre rerolea el pool;
    // un CFUN=0/CFUN=1 sí fuerza nueva asignación.
    bool netUp = false;
    for (int cycle = 1; cycle <= 2; cycle++) {
        Serial.printf("[net] Ciclo attach #%d\n", cycle);

        if (!ModemNet::connectGprs(APN)) {
            Serial.println("[net][ERR] gprsConnect falló.");
        } else {
            String ip = ModemNet::modem.getLocalIP();
            if (ip.startsWith("100.")) {
                Serial.printf("[net][WARN] IP en pool CGNAT (%s); BAM suele dar ruta rota aquí.\n",
                              ip.c_str());
            }
            if (ModemNet::configurePublicDns() &&
                ModemNet::syncEpoch(s_epochAtBoot)) {
                netUp = true;
                break;
            }
        }

        if (cycle == 1) {
            Serial.println("[net] Fallo de attach/DNS/sync, hard restart del módem...");
            if (!ModemNet::hardRestart()) {
                Serial.println("[net][ERR] Hard restart falló. Idle.");
                while (true) delay(5000);
            }
        }
    }
    if (!netUp) {
        Serial.println("[net][ERR] Red no disponible tras 2 ciclos. Idle.");
        while (true) delay(5000);
    }
    s_millisAtBoot = millis();
}

// Borra el CSV local y lo recrea con solo el header. Llamado al boot y
// después de cada PUT exitoso para empezar el siguiente chunk vacío.
static bool resetCsv()
{
    SD.remove(s_csvPath);
    File f = SD.open(s_csvPath, FILE_WRITE);
    if (!f) return false;
    f.println(CSV_HEADER);
    f.close();
    return true;
}

static void openSessionCsv()
{
    snprintf(s_csvPath,  sizeof(s_csvPath),  "/session_%u.csv", s_epochAtBoot);
    snprintf(s_s3Prefix, sizeof(s_s3Prefix), "%ssession_%u/",   AWS_S3_PREFIX, s_epochAtBoot);

    if (!resetCsv()) {
        Serial.printf("[csv][ERR] No se pudo crear %s\n", s_csvPath);
        while (true) delay(5000);
    }
    Serial.printf("[csv] Sesión: %s -> s3://.../%s (chunks part_NNNN.csv)\n",
                  s_csvPath, s_s3Prefix);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("=== Stage 1 MVP: LIS3DHTR -> SD -> S3 ===");

    initSensor();
    initSd();
    initNetwork();

    DeviceCfg device;
    SessionCfg session;
    loadDeviceConfig(device);
    loadSessionConfig(session);
    Serial.printf("[cfg] vehicle=%s driver=%s\n",
                  device.vehicle_id.c_str(), session.driver_id.c_str());

    openSessionCsv();
    writeSessionMeta(device, session);

    s_lastUploadMs = millis();
    Serial.printf("[loop] Muestreando 5 Hz, PUT cada %lu s\n",
                  UPLOAD_INTERVAL_MS / 1000UL);
}

static void sampleOnce(uint32_t nowMs, File& csv)
{
    char iso[25];
    hostTimeIso(nowMs, iso);
    float x = lis.getAccelerationX();
    float y = lis.getAccelerationY();
    float z = lis.getAccelerationZ();
    csv.printf("%s,%lu,%.4f,%.4f,%.4f,%u\n",
               iso, (unsigned long)micros(), x, y, z, 0u);
    s_samplesSinceUpload++;
}

static void uploadIfDue()
{
    if (s_samplesSinceUpload == 0) {
        Serial.println("[up] sin muestras nuevas, salta ciclo");
        return;
    }

    char key[96];
    snprintf(key, sizeof(key), "%spart_%04u.csv", s_s3Prefix, s_chunkSeq);

    if (AwsUploader::putObjectFromSd(s_csvPath, key, epochNow())) {
        Serial.printf("[up] chunk %u OK (%lu muestras), rotando CSV\n",
                      s_chunkSeq, (unsigned long)s_samplesSinceUpload);
        s_chunkSeq++;
        s_samplesSinceUpload = 0;
        if (!resetCsv()) {
            Serial.println("[up][ERR] No se pudo resetear CSV tras PUT exitoso");
        }
    } else {
        Serial.println("[up] chunk falló, se acumula y reintenta próximo ciclo");
    }
}

void loop()
{
    static uint32_t lastSampleMs = 0;
    uint32_t now = millis();

    if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
        lastSampleMs = now;
        File f = SD.open(s_csvPath, FILE_APPEND);
        if (f) {
            sampleOnce(now, f);
            f.close();
        }
    }

    if (now - s_lastUploadMs >= UPLOAD_INTERVAL_MS) {
        s_lastUploadMs = now;
        uploadIfDue();
    }
}
