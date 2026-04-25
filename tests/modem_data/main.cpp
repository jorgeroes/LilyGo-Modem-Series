/**
 * Test 3 — Sesión de datos móviles (ping + HTTP GET)
 *
 * Placa  : T-A7670X-S3-Standard (ESP32-S3)
 * Pines  : MODEM RX (ESP) = GPIO 4  | MODEM TX (ESP) = GPIO 5 | PWRKEY = GPIO 46
 *
 * Requisitos previos:
 *   - Test 2 pasó (SIM_READY + REG_OK_*).
 *   - Editar NETWORK_APN abajo con el APN del operador (anotado en Parte 0).
 *
 * Uso:
 *   1. En platformio.ini, poner `src_dir = tests/modem_data`.
 *   2. Ajustar NETWORK_APN. Compilar y flashear.
 *
 * Criterio de paso: IP local ≠ 0.0.0.0 y al menos 1 ping o HTTP GET exitoso.
 */

#define TINY_GSM_MODEM_A7670
#define TINY_GSM_RX_BUFFER    1024
#define DUMP_AT_COMMANDS

// ── Configuración de red — EDITAR ────────────────────────────────────────────
#define NETWORK_APN           "bam.entelpcs.cl"
#define NETWORK_APN_USER      ""
#define NETWORK_APN_PASS      ""

// HTTP GET plano: devuelve la IP pública asignada.
#define HTTP_HOST             "ifconfig.io"
#define HTTP_PORT             80
#define HTTP_PATH             "/"

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(Serial1, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(Serial1);
#endif

TinyGsmClient gsmClient(modem);
HttpClient http(gsmClient, HTTP_HOST, HTTP_PORT);

#define MODEM_RX_PIN          5     // ESP RX (conectado al TX del módem)
#define MODEM_TX_PIN          4     // ESP TX (conectado al RX del módem)
#define BOARD_PWRKEY_PIN      46
#define MODEM_BAUDRATE        115200
#define POWERON_PULSE_MS      100
#define BOOT_WAIT_MS          3000

#define SerialAT              Serial1

static void powerOnModem()
{
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(POWERON_PULSE_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
}

static bool waitForRegistration(uint32_t timeoutMs)
{
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        RegStatus reg = modem.getRegistrationStatus();
        int16_t sq = modem.getSignalQuality();
        Serial.printf("[%lus] reg=%d  CSQ=%d\n",
                      (unsigned long)(millis() / 1000UL), (int)reg, sq);
        if (reg == REG_OK_HOME || reg == REG_OK_ROAMING) return true;
        if (reg == REG_DENIED) return false;
        delay(2000);
    }
    return false;
}

static void runPings()
{
    // Primero por IP (bypass DNS) para confirmar que la sesión de datos funciona.
    // Luego por dominio para validar que DNS resuelve.
    const char* targets[] = {"8.8.8.8", "www.google.com"};
    String resolvedIp;
    uint32_t pktSize = 0, tripTime = 0;
    uint8_t ttl = 0;
    for (uint8_t t = 0; t < 2; t++) {
        Serial.printf("--- Ping x3 a %s ---\n", targets[t]);
        int ok = 0;
        for (int i = 0; i < 3; i++) {
            int r = modem.ping(targets[t], resolvedIp, pktSize, tripTime, ttl);
            if (r == 1) {
                Serial.printf("  Reply from %s: bytes=%u time=%ums TTL=%u\n",
                              resolvedIp.c_str(), pktSize, tripTime, ttl);
                ok++;
            } else {
                Serial.printf("  Ping fallo: código=%d\n", r);
            }
            delay(1000);
        }
        Serial.printf("Resumen %s: %d/3 exitosos\n", targets[t], ok);
    }
}

static void runHttpGet()
{
    Serial.printf("--- HTTP GET http://%s%s ---\n", HTTP_HOST, HTTP_PATH);
    http.setTimeout(15000);
    int err = http.get(HTTP_PATH);
    if (err != 0) {
        Serial.printf("HTTP get() fallo: %d\n", err);
        http.stop();
        return;
    }

    int status = http.responseStatusCode();
    Serial.printf("Status: %d\n", status);
    if (status <= 0) {
        http.stop();
        return;
    }

    String body = http.responseBody();
    Serial.printf("Body (%u bytes):\n%s\n", body.length(), body.c_str());
    http.stop();
}

void setup()
{
    Serial.begin(115200);
    for (int i = 3; i > 0; i--) {
        Serial.printf("Esperando monitor... %d\n", i);
        delay(1000);
    }

    Serial.println("=== Test 3 — sesión de datos móviles ===");
    Serial.printf("APN: \"%s\"\n", NETWORK_APN);

    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    Serial.println("Pulsando PWRKEY...");
    powerOnModem();
    delay(BOOT_WAIT_MS);

    Serial.println("Esperando respuesta AT...");
    int retry = 0;
    while (!modem.testAT(1000)) {
        Serial.print(".");
        if (++retry > 30) {
            powerOnModem();
            delay(BOOT_WAIT_MS);
            retry = 0;
        }
    }
    Serial.println();

    Serial.println("Reset suave (CFUN=6)...");
    modem.sendAT("+CFUN=6");
    modem.waitResponse();
    delay(10000);
    while (!modem.testAT(1000)) { delay(500); }

    Serial.println("Esperando SIM_READY...");
    SimStatus sim = SIM_ERROR;
    uint32_t simDeadline = millis() + 15000UL;
    while (millis() < simDeadline) {
        sim = modem.getSimStatus();
        if (sim == SIM_READY) break;
        delay(1000);
    }
    if (sim != SIM_READY) {
        Serial.println("[ERROR] SIM no READY. Correr Test 2 antes.");
        while (true) { delay(5000); }
    }

    Serial.println("Esperando registro (60 s)...");
    if (!waitForRegistration(60000UL)) {
        Serial.println("[ERROR] No se pudo registrar. Correr Test 2 antes.");
        while (true) { delay(5000); }
    }
    Serial.printf("Operador: %s\n", modem.getOperator().c_str());

    // Activar PDP context con el APN.
    Serial.printf("Activando red con APN=\"%s\"...\n", NETWORK_APN);
    bool active = false;
    for (int i = 0; i < 3 && !active; i++) {
        active = modem.gprsConnect(NETWORK_APN, NETWORK_APN_USER, NETWORK_APN_PASS);
        if (!active) {
            Serial.println("gprsConnect fallo, reintento en 3 s...");
            delay(3000);
        }
    }
    if (!active) {
        Serial.println("[ERROR] No se pudo activar la sesión de datos.");
        Serial.println("        Verificar APN (¿coincide con el del celular?).");
        while (true) { delay(5000); }
    }

    String ip = modem.getLocalIP();
    Serial.printf("IP local: %s\n", ip.c_str());
    if (ip.length() == 0 || ip == "0.0.0.0") {
        Serial.println("[ERROR] IP local inválida.");
        while (true) { delay(5000); }
    }

    // BAM APNs a menudo no entregan DNS; forzar DNS público.
    Serial.println("Configurando DNS (8.8.8.8 / 1.1.1.1)...");
    modem.sendAT("+CDNSCFG=\"8.8.8.8\",\"1.1.1.1\"");
    modem.waitResponse();

    runPings();
    runHttpGet();

    Serial.println("--- Test 3 completado. Idle ---");
    modem.gprsDisconnect();
}

void loop()
{
    delay(5000);
}
