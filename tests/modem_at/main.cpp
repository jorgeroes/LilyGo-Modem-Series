/**
 * Test 1 — Módem A7670 responde a AT (sin SIM)
 *
 * Placa  : T-A7670X-S3-Standard (ESP32-S3)
 * Pines  : MODEM RX (ESP) = GPIO 4  | MODEM TX (ESP) = GPIO 5 | PWRKEY = GPIO 46
 *
 * Uso:
 *   1. En platformio.ini, poner `src_dir = tests/modem_at`.
 *   2. Compilar y flashear.
 *   3. Abrir monitor serial a 115200 por el puerto ESP-USB.
 *   4. Enviar `AT`, `ATI`, `AT+CPIN?`, `AT+CSQ`.
 *
 * Criterio de paso: `AT` → `OK` estable por >30 s de puente interactivo.
 */

#include <Arduino.h>

#define MODEM_RX_PIN          5     // ESP recibe aquí (conectado al TX del módem)
#define MODEM_TX_PIN          4     // ESP transmite aquí (conectado al RX del módem)
#define BOARD_PWRKEY_PIN      46
#define MODEM_BAUDRATE        115200
#define POWERON_PULSE_MS      100
#define BOOT_WAIT_MS          3000

#define SerialAT              Serial1

static bool checkRespond()
{
    for (int j = 0; j < 10; j++) {
        SerialAT.print("AT\r\n");
        String input = SerialAT.readString();
        if (input.indexOf("OK") >= 0) {
            return true;
        }
        delay(200);
    }
    return false;
}

static uint32_t autoBaud()
{
    static const uint32_t rates[] = {115200, 9600, 57600, 38400, 19200,
                                     74400, 74880, 230400, 460800};
    for (uint8_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        uint32_t rate = rates[i];
        Serial.printf("Probando baud %u...\n", rate);
        SerialAT.updateBaudRate(rate);
        delay(10);
        for (int j = 0; j < 10; j++) {
            SerialAT.print("AT\r\n");
            String input = SerialAT.readString();
            if (input.indexOf("OK") >= 0) {
                Serial.printf("Módem responde a %u baud\n", rate);
                return rate;
            }
        }
    }
    SerialAT.updateBaudRate(MODEM_BAUDRATE);
    return 0;
}

static void powerOnModem()
{
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(100);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);
    delay(POWERON_PULSE_MS);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
}

void setup()
{
    Serial.begin(115200);
    for (int i = 3; i > 0; i--) {
        Serial.printf("Esperando monitor... %d\n", i);
        delay(1000);
    }

    Serial.println("=== Test 1 — módem A7670 responde a AT ===");
    Serial.printf("UART: RX=%d  TX=%d  PWRKEY=%d  baud=%u\n",
                  MODEM_RX_PIN, MODEM_TX_PIN, BOARD_PWRKEY_PIN, MODEM_BAUDRATE);

    SerialAT.begin(MODEM_BAUDRATE, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    Serial.println("Pulsando PWRKEY (power-on)...");
    powerOnModem();

    Serial.printf("Esperando boot del módem (%d ms)...\n", BOOT_WAIT_MS);
    delay(BOOT_WAIT_MS);

    if (!checkRespond()) {
        Serial.println("No responde a 115200. Probando otras tasas...");
        if (autoBaud() == 0) {
            Serial.println("***********************************************************");
            Serial.println("[ERROR] El módem no responde. Verificar:");
            Serial.println("  - Antena LTE conectada al conector IPEX");
            Serial.println("  - Cableado TX/RX (GPIO 4/5) y PWRKEY (GPIO 46)");
            Serial.println("  - Alimentación estable (USB-C con ≥2 A de capacidad)");
            Serial.println("***********************************************************");
            while (true) { delay(1000); }
        }
    }

    Serial.println();
    Serial.println("***********************************************************");
    Serial.println(" Módem OK. Puente Serial ↔ SerialAT activo.");
    Serial.println(" Prueba: AT | ATI | AT+CPIN? | AT+CSQ");
    Serial.println(" (el monitor debe enviar con terminador CR+LF)");
    Serial.println("***********************************************************");
    Serial.println();
}

void loop()
{
    if (SerialAT.available()) {
        Serial.write(SerialAT.read());
    }
    if (Serial.available()) {
        SerialAT.write(Serial.read());
    }
    delay(1);
}
