/**
 * @file      SIM-DC-Shield.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2026  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2026-02-28
 *
 */

#include "Arduino.h"
#include "utilities.h"
#include <Wire.h>
#include "SSD1306Wire.h"

const uint8_t oled_addr = 0x3c; // OLED I2C address

SSD1306Wire display(oled_addr, BOARD_SDA_PIN, BOARD_SCL_PIN);

uint32_t interval = 0;
bool found_oled = false;

uint8_t scanI2CDevices()
{
    Serial.println("Scanning I2C devices...");

    byte error, address;
    int nDevices = 0;

    for (address = 1; address < 127; address++ ) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();

        if (error == 0) {
            Serial.print("I2C device found at address 0x");
            if (address < 16)
                Serial.print("0");
            Serial.print(address, HEX);
            Serial.println("  !");
            nDevices++;
        } else if (error == 4) {
            Serial.print("Unknown error at address 0x");
            if (address < 16)
                Serial.print("0");
            Serial.println(address, HEX);
        }
    }
    if (nDevices == 0)
        Serial.println("No I2C devices found\n");
    return nDevices;
}

void setup()
{
    Serial.begin(115200); // Set console baud rate

    Wire.begin(BOARD_SDA_PIN, BOARD_SCL_PIN); // Initialize I2C

    // Modem use Serial1
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    adcAttachPin(SIM_DC_SHIELD_A0);
    adcAttachPin(SIM_DC_SHIELD_A1);

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    pinMode(SIM_DC_SHIELD_D0, OUTPUT);
    pinMode(SIM_DC_SHIELD_D1, OUTPUT);

    Serial.println("Setup complete");

    Wire.beginTransmission(oled_addr);
    if (Wire.endTransmission() == 0) {
        found_oled = true;
        display.init();
        display.flipScreenVertically();
        display.setFont(ArialMT_Plain_10);
        display.drawString(0, 0, "Hello, OLED!");
        display.display();
    }
}

void loop()
{
    if (millis() > interval) {

        uint8_t device_count = scanI2CDevices();

        uint16_t a0_raw = analogRead(SIM_DC_SHIELD_A0);
        uint16_t a1_raw = analogRead(SIM_DC_SHIELD_A1);

        Serial.printf("A0: %d, A1: %d\r\n", a0_raw, a1_raw);

        digitalWrite(SIM_DC_SHIELD_D0, 1 - digitalRead(SIM_DC_SHIELD_D0));
        digitalWrite(SIM_DC_SHIELD_D1, 1 - digitalRead(SIM_DC_SHIELD_D1));

        if (found_oled) {
            display.clear();
            display.drawString(0, 0,  "A0: " + String(a0_raw));
            display.drawString(0, 10, "A1: " + String(a1_raw));
            display.drawString(0, 20, "D0: " + String(digitalRead(SIM_DC_SHIELD_D0)));
            display.drawString(0, 30, "D1: " + String(digitalRead(SIM_DC_SHIELD_D1)));
            display.drawString(0, 40, "I2C Dev: " + String(device_count));
            display.display();
        }

        interval = millis() + 1000;
    }
}
