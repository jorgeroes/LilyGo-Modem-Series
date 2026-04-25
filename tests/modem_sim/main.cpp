/**
 * Test 2 — SIM detectada + registro de red (sin sesión de datos)
 *
 * Placa  : T-A7670X-S3-Standard (ESP32-S3)
 * Pines  : MODEM RX (ESP) = GPIO 4  | MODEM TX (ESP) = GPIO 5 | PWRKEY = GPIO 46
 *
 * Requisitos previos:
 *   - SIM insertada antes de encender (ver FAQ del board).
 *   - Antena LTE conectada al IPEX.
 *
 * Uso:
 *   1. En platformio.ini, poner `src_dir = tests/modem_sim`.
 *   2. Compilar y flashear. Abrir monitor a 115200.
 *
 * Criterio de paso: SIM_READY + registro (home/roaming) + CSQ ≥ 10 estable 1 min.
 */

#define TINY_GSM_MODEM_A7670
#define TINY_GSM_RX_BUFFER    1024
#define DUMP_AT_COMMANDS              // muestra el tráfico AT en la consola

#include <Arduino.h>
#include <TinyGsmClient.h>

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(Serial1, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(Serial1);
#endif

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

static const char* simStatusStr(SimStatus s)
{
    switch (s) {
        case SIM_READY:   return "READY";
        case SIM_LOCKED:  return "LOCKED (PIN requerido)";
        case SIM_ANTITHEFT_LOCKED: return "ANTITHEFT_LOCKED";
        case SIM_ERROR:   return "ERROR";
        default:          return "UNKNOWN";
    }
}

static const char* regStatusStr(RegStatus r)
{
    switch (r) {
        case REG_UNREGISTERED:  return "UNREGISTERED";
        case REG_SEARCHING:     return "SEARCHING";
        case REG_DENIED:        return "DENIED";
        case REG_OK_HOME:       return "OK_HOME";
        case REG_OK_ROAMING:    return "OK_ROAMING";
        case REG_SMS_ONLY:      return "SMS_ONLY";
        case REG_NO_RESULT:     return "NO_RESULT";
        default:                return "UNKNOWN";
    }
}

void setup()
{
    Serial.begin(115200);
    for (int i = 3; i > 0; i--) {
        Serial.printf("Esperando monitor... %d\n", i);
        delay(1000);
    }

    Serial.println("=== Test 2 — SIM + registro de red ===");
    Serial.printf("UART: RX=%d  TX=%d  PWRKEY=%d\n",
                  MODEM_RX_PIN, MODEM_TX_PIN, BOARD_PWRKEY_PIN);

    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    Serial.println("Pulsando PWRKEY...");
    powerOnModem();
    Serial.printf("Esperando boot (%d ms)...\n", BOOT_WAIT_MS);
    delay(BOOT_WAIT_MS);

    // Esperar a que el módem responda. Cada 30 reintentos, re-pulsar PWRKEY.
    Serial.println("Esperando respuesta AT...");
    int retry = 0;
    while (!modem.testAT(1000)) {
        Serial.print(".");
        if (++retry > 30) {
            Serial.println("\n[WARN] Re-pulsando PWRKEY...");
            powerOnModem();
            delay(BOOT_WAIT_MS);
            retry = 0;
        }
    }
    Serial.println("\nMódem responde.");

    // Reset suave para partir de un estado conocido.
    Serial.println("Reset suave (CFUN=6)...");
    modem.sendAT("+CFUN=6");
    modem.waitResponse();
    delay(10000);
    while (!modem.testAT(1000)) { Serial.print("."); delay(500); }
    Serial.println();

    // Info de hardware/firmware del módem.
    Serial.printf("Modem info: %s\n", modem.getModemInfo().c_str());
    String ati;
    modem.sendAT("+SIMCOMATI");
    modem.waitResponse(10000UL, ati);
    Serial.println(ati);

    // Estado de la SIM.
    Serial.println("Verificando SIM...");
    SimStatus sim = SIM_ERROR;
    uint32_t simDeadline = millis() + 15000UL;
    while (millis() < simDeadline) {
        sim = modem.getSimStatus();
        Serial.printf("SIM: %s\n", simStatusStr(sim));
        if (sim == SIM_READY) break;
        if (sim == SIM_LOCKED) {
            Serial.println("[ERROR] SIM con PIN activo. Desactivarlo en un celular");
            Serial.println("        o añadir modem.simUnlock(\"NNNN\") aquí.");
            while (true) { delay(1000); }
        }
        delay(1000);
    }
    if (sim != SIM_READY) {
        Serial.println("[ERROR] SIM no llegó a READY. Verificar inserción y polaridad.");
        while (true) { delay(1000); }
    }

    // Registro en red. CSQ reportado cada ciclo.
    Serial.println("Esperando registro...");
    RegStatus reg = REG_NO_RESULT;
    uint32_t regDeadline = millis() + 60000UL;
    while (millis() < regDeadline) {
        reg = modem.getRegistrationStatus();
        int16_t sq = modem.getSignalQuality();
        Serial.printf("[%lus] reg=%s  CSQ=%d\n",
                      (unsigned long)(millis() / 1000UL), regStatusStr(reg), sq);

        if (reg == REG_OK_HOME || reg == REG_OK_ROAMING) break;
        if (reg == REG_DENIED) {
            Serial.println("[ERROR] Registro rechazado por la red (REG_DENIED).");
            Serial.println("        Causas típicas: APN requerido, SIM fuera de banda, plan inactivo.");
            while (true) { delay(5000); }
        }
        if (reg == REG_SMS_ONLY) {
            Serial.println("[WARN] SIM sólo SMS (no datos). Contactar al operador.");
            break;
        }
        delay(2000);
    }

    if (reg != REG_OK_HOME && reg != REG_OK_ROAMING && reg != REG_SMS_ONLY) {
        Serial.println("[ERROR] Timeout esperando registro.");
        while (true) { delay(5000); }
    }

    Serial.printf("Registrado: %s\n", regStatusStr(reg));
    Serial.printf("Operador: %s\n", modem.getOperator().c_str());

    String ueInfo;
    if (modem.getSystemInformation(ueInfo)) {
        Serial.print("System info: ");
        Serial.println(ueInfo);
    }

    Serial.println("--- Monitoreando CSQ cada 5 s ---");
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 5000UL) {
        last = millis();
        int16_t sq = modem.getSignalQuality();
        RegStatus reg = modem.getRegistrationStatus();
        Serial.printf("[%lus] reg=%s  CSQ=%d\n",
                      (unsigned long)(millis() / 1000UL), regStatusStr(reg), sq);
    }
    delay(50);
}
