/**
 * LIS3DHTR - Lectura a 5 Hz
 *
 * Placa  : T-A7670X-S3-Standard (ESP32-S3)
 * Sensor : LIS3DHTR (Seeed / STMicro)
 * Conexión I2C: SDA → GPIO 3 | SCL → GPIO 2 | VCC → 3.3 V | GND → GND
 *
 * El chip LIS3DH no dispone de ODR de 5 Hz nativo.
 * Se configura a 10 Hz y se lee con periodo de 200 ms → tasa de salida 5 Hz.
 */

#include <Arduino.h>
#include <Wire.h>
#include "LIS3DHTR.h"

#define I2C_SDA   3
#define I2C_SCL   2
#define SAMPLE_INTERVAL_MS  200   // 200 ms = 5 Hz

LIS3DHTR<TwoWire> lis;

void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println("=== LIS3DHTR 5 Hz reader ===");
    Serial.printf("I2C SDA=GPIO%d | SCL=GPIO%d\n", I2C_SDA, I2C_SCL);

    Wire.begin(I2C_SDA, I2C_SCL);

    lis.begin(Wire, LIS3DHTR_ADDRESS_UPDATED);  // 0x19; usa 0x18 si SDO está a GND
    if (!lis) {
        Serial.println("[ERROR] LIS3DHTR no encontrado. Verifica conexión I2C.");
        while (true) { delay(1000); }
    }

    lis.setOutputDataRate(LIS3DHTR_DATARATE_10HZ);  // ODR más cercano a 5 Hz
    lis.setFullScaleRange(LIS3DHTR_RANGE_2G);

    Serial.println("Sensor inicializado. ODR=10 Hz, rango=±2 g, salida a 5 Hz.");
    Serial.println("---");
}

void loop()
{
    static uint32_t lastRead = 0;

    if (millis() - lastRead >= SAMPLE_INTERVAL_MS) {
        lastRead = millis();

        float x = lis.getAccelerationX();
        float y = lis.getAccelerationY();
        float z = lis.getAccelerationZ();

        Serial.printf("X: %7.4f g | Y: %7.4f g | Z: %7.4f g\n", x, y, z);
    }
}
