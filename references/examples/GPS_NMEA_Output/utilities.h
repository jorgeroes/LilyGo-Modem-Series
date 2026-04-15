/**
 * @file      utilities.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2025-04-30
 *
 */

#pragma once

// Note:

// When using ArduinoIDE, you must select a corresponding board type.
// If you don’t know which board type you have, please click on the link to view it.
// 使用ArduinoIDE ,必须选择一个对应的板型 ,如果你不知道你的板型是哪种，请点击链接进行查看

// The model name with S3 after it is the ESP32-S3 version, otherwise it is the ESP32 version
// 型号名称后面带S3的为ESP32-S3版本，否则是ESP32版本

// Products Link:https://www.lilygo.cc/products/t-sim-a7670e
// #define LILYGO_T_A7670

// There are two versions of T-Call A7670X. Please be careful to distinguish them. Please check the silkscreen on the front of the board to distinguish them.
// T-Call A7670X 有两个版本,请注意区分，如何区分请查看板子正面丝印.
// #define LILYGO_T_CALL_A7670_V1_0

// There are two versions of T-Call A7670X. Please be careful to distinguish them. Please check the silkscreen on the front of the board to distinguish them.
// T-Call A7670X 有两个版本,请注意区分，如何区分请查看板子正面丝印.
// #define LILYGO_T_CALL_A7670_V1_1

// Products Link: https://lilygo.cc/products/t-sim-7670g-s3

// Products Link: https://lilygo.cc/products/t-a7608e-h?variant=42860532433077

// Products Link: https://lilygo.cc/products/t-a7608e-h?variant=43932699033781

// Products Link: https://lilygo.cc/products/t-sim7000g

// Products Link: https://lilygo.cc/products/t-sim7070g

// Products Link: https://lilygo.cc/products/a-t-pcie?variant=42335922094261
// #define LILYGO_T_PCIE_A767X

// Products Link: https://lilygo.cc/products/a-t-pcie?variant=42335921897653

// Products Link: https://lilygo.cc/products/a-t-pcie?variant=42335921995957

// Products Link: ......

// Products Link: https://lilygo.cc/products/t-eth-elite-1?variant=44498205049013
// #define LILYGO_T_ETH_ELITE_A7670X


// https://lilygo.cc/products/t-sim7600

// Products Link: ......

// https://lilygo.cc/products/t-internet-com
// #define LILYGO_T_INTERNET_COM_A7670X

// SIMCOM standard interface series
// #define LILYGO_A7670X_S3_STAN


#if defined(LILYGO_T_A7670)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_DTR_PIN                       (25)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (27)
    // The modem boot pin needs to follow the startup sequence.
    #define BOARD_PWRKEY_PIN                    (4)
    // The modem power switch must be set to HIGH for the modem to supply power.
    #define BOARD_POWERON_PIN                   (12)
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define BOARD_MISO_PIN                      (2)
    #define BOARD_MOSI_PIN                      (15)
    #define BOARD_SCK_PIN                       (14)
    #define BOARD_SD_CS_PIN                     (13)
    #define BOARD_BAT_ADC_PIN                   (35)
    #define MODEM_RESET_LEVEL                   HIGH
    #define SerialAT                            Serial1

    #define MODEM_GPS_ENABLE_GPIO               (-1)
    #define MODEM_GPS_ENABLE_LEVEL              (-1)

    #ifndef TINY_GSM_MODEM_A7670
        #define TINY_GSM_MODEM_A7670
    #endif

    // It is only available in V1.4 version. In other versions, IO36 is not connected.
    #define BOARD_SOLAR_ADC_PIN                 (36)


    //! The following pins are for SimShield and need to be used with SimShield
    //! 以下引脚针对SimShield,需要搭配SimShield 
    #define SIMSHIELD_MOSI                      (23)
    #define SIMSHIELD_MISO                      (19)
    #define SIMSHIELD_SCK                       (18)
    #define SIMSHIELD_SD_CS                     (32)
    #define SIMSHIELD_RADIO_BUSY                (39)
    #define SIMSHIELD_RADIO_CS                  (5)
    #define SIMSHIELD_RADIO_IRQ                 (34)
    #define SIMSHIELD_RADIO_RST                 (15)
    #define SIMSHIELD_RS_RX                     (13)
    #define SIMSHIELD_RS_TX                     (14)
    #define SIMSHIELD_SDA                       (21)
    #define SIMSHIELD_SCL                       (22)
    #define SerialRS485                         Serial2

    // The following pins are for SIM-DC-Shield and need to be used with SIM-DC-Shield
    // !以下引脚针对SIM-DC-Shield,需要搭配SIM-DC-Shield
    #define SIM_DC_SHIELD_A0                    (34)
    #define SIM_DC_SHIELD_A1                    (39)
    #define SIM_DC_SHIELD_D0                    (19)
    #define SIM_DC_SHIELD_D1                    (18)
    #define SIM_DC_SHIELD_SENSOR_IRQ            (32)
    #define BOARD_SDA_PIN                       (21)
    #define BOARD_SCL_PIN                       (22)

    #define PRODUCT_MODEL_NAME                  "LilyGo-A7670 ESP32 Version"

#elif defined(LILYGO_T_CALL_A7670_V1_0)
    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_DTR_PIN                       (14)
    #define MODEM_TX_PIN                        (26)
    #define MODEM_RX_PIN                        (25)
    // The modem boot pin needs to follow the startup sequence.
    #define BOARD_PWRKEY_PIN                    (4)
    #define BOARD_LED_PIN                       (12)
    #define LED_ON                              HIGH
    #define MODEM_RING_PIN                      (13)
    #define MODEM_RESET_PIN                     (27)
    #define MODEM_RESET_LEVEL                   LOW
    #define SerialAT                            Serial1


    #define MODEM_GPS_ENABLE_GPIO               (-1)
    #define MODEM_GPS_ENABLE_LEVEL              (-1)


    #ifndef TINY_GSM_MODEM_A7670
        #define TINY_GSM_MODEM_A7670
    #endif

    #define PRODUCT_MODEL_NAME                  "LilyGo-T-Call A7670 V1.0"

#elif defined(LILYGO_T_CALL_A7670_V1_1)

    #define MODEM_BAUDRATE                      (115200)
    #define MODEM_DTR_PIN                       (32)
    #define MODEM_TX_PIN                        (27)
    #define MODEM_RX_PIN                        (26)
    // The modem boot pin needs to follow the startup sequence.
    #define BOARD_PWRKEY_PIN                    (4)
    #define BOARD_LED_PIN                       (13)
    #define LED_ON                              HIGH
    // There is no modem power control, the LED Pin is used as a power indicator here.
    #define MODEM_RING_PIN                      (33)
    #define MODEM_RESET_PIN                     (5)
    #define MODEM_RESET_LEVEL                   LOW
    #define SerialAT                            Serial1

    #define MODEM_GPS_ENABLE_GPIO               (-1)
    #define MODEM_GPS_ENABLE_LEVEL              (-1)

    #ifndef TINY_GSM_MODEM_A7670
        #define TINY_GSM_MODEM_A7670
    #endif

    #define PRODUCT_MODEL_NAME                  "LilyGo-T-Call A7670 V1.1"

#elif defined(LILYGO_T_PCIE_A767X)

    #define LILYGO_T_PCIE

    #ifndef TINY_GSM_MODEM_A7670
        #define TINY_GSM_MODEM_A7670
    #endif

    // Modem GPIO 4 control gps enable
    #define MODEM_GPS_ENABLE_GPIO               (4)
    #define MODEM_GPS_ENABLE_LEVEL              (0)
    #define PRODUCT_MODEL_NAME                  "LilyGo-T-PCIE-A7670X"

#elif defined(LILYGO_T_ETH_ELITE_A7670X)

    #define LILYGO_T_ETH_ELITE

    #ifndef TINY_GSM_MODEM_A7670
        #define TINY_GSM_MODEM_A7670
    #endif


    // Modem GPIO 4 control gps enable
    #define MODEM_GPS_ENABLE_GPIO               (4)
    #define MODEM_GPS_ENABLE_LEVEL              (0)
    #define PRODUCT_MODEL_NAME                  "LilyGo-T-ETH-Elite-A7670X"

#elif defined(LILYGO_T_INTERNET_COM_A7670X)

    #define LILYGO_T_INTERNET_COM

    #ifndef TINY_GSM_MODEM_A7670
        #define TINY_GSM_MODEM_A7670
    #endif


    // Modem GPIO 4 control gps enable
    #define MODEM_GPS_ENABLE_GPIO               (4)
    #define MODEM_GPS_ENABLE_LEVEL              (0)
    #define PRODUCT_MODEL_NAME                  "LilyGo-T-Internet-COM--A7670X"

#else
    #error "Use ArduinoIDE, please open the macro definition corresponding to the board above <utilities.h>"
#endif


#ifdef LILYGO_T_INTERNET_COM

    #define MODEM_RX_PIN            (35)
    #define MODEM_TX_PIN            (33)
    #define MODEM_RESET_PIN         (32)
    // The modem boot pin needs to follow the startup sequence.
    #define BOARD_PWRKEY_PIN        (32)

    // SD Socket pins
    #define BOARD_MISO_PIN          (2)
    #define BOARD_MOSI_PIN          (15)
    #define BOARD_SCK_PIN           (14)
    #define BOARD_SD_CS_PIN         (13)

    #define SerialAT                Serial1

    #define MODEM_RESET_LEVEL       HIGH


#endif /*LILYGO_T_INTERNET_COM*/

#ifdef LILYGO_T_PCIE

    #define MODEM_DTR_PIN           (32)
    #define MODEM_RX_PIN            (26)
    #define MODEM_TX_PIN            (27)
    // The modem power switch must be set to HIGH for the modem to supply power.
    #define BOARD_POWERON_PIN       (25)
    // The modem boot pin needs to follow the startup sequence.
    #define BOARD_PWRKEY_PIN        (4)
    #define MODEM_RING_PIN          (33)
    #define BOARD_LED_PIN           (12)
    #define PMU_IRQ                 (35)

    #define LED_ON                  (LOW)

    #define SerialAT                Serial1

    #ifndef MODEM_GPS_ENABLE_GPIO
        #define MODEM_GPS_ENABLE_GPIO               (-1)
    #endif
    #ifndef MODEM_GPS_ENABLE_LEVEL
        #define MODEM_GPS_ENABLE_LEVEL              (-1)
    #endif
    
#endif /*LILYGO_T_PCIE*/

#ifdef LILYGO_T_ETH_ELITE

    #define ETH_MISO_PIN             (47)
    #define ETH_MOSI_PIN             (21)
    #define ETH_SCLK_PIN             (48)
    #define ETH_CS_PIN               (45)
    #define ETH_INT_PIN              (14)
    #define ETH_RST_PIN              (-1)
    #define ETH_ADDR                 (1)

    #define SPI_MISO_PIN             (9)
    #define SPI_MOSI_PIN             (11)
    #define SPI_SCLK_PIN             (10)

    #define SD_MISO_PIN              (SPI_MISO_PIN)
    #define SD_MOSI_PIN              (SPI_MOSI_PIN)
    #define SD_SCLK_PIN              (SPI_SCLK_PIN)
    #define SD_CS_PIN                (12)

    #define BOARD_SDA_PIN             (17)
    #define BOARD_SCL_PIN             (18)

    #define ADC_BUTTONS_PIN           (7)
    #define MODEM_RX_PIN              (4)
    #define MODEM_TX_PIN              (6)
    #define MODEM_DTR_PIN             (5)
    #define MODEM_RING_PIN            (1)
    #define BOARD_PWRKEY_PIN          (3)

    #define GPS_RX_PIN                (39)
    #define GPS_TX_PIN                (42)

    #define BOARD_LED_PIN             (38)
    #define LED_ON                    (HIGH)

    #define SerialAT                Serial1

    #ifndef MODEM_GPS_ENABLE_GPIO
        #define MODEM_GPS_ENABLE_GPIO               (-1)
    #endif
    #ifndef MODEM_GPS_ENABLE_LEVEL
        #define MODEM_GPS_ENABLE_LEVEL              (-1)
    #endif
    
#endif /*LILYGO_T_ETH_ELITE*/




#if defined(TINY_GSM_MODEM_SIM7670G) || defined(TINY_GSM_MODEM_A7670) || defined(TINY_GSM_MODEM_A7608)
    #define MODEM_REG_SMS_ONLY
#endif

// Invalid version

// Power on/off sequence
#if defined(TINY_GSM_MODEM_A7670)
    #define MODEM_POWERON_PULSE_WIDTH_MS      (100)
    #define MODEM_POWEROFF_PULSE_WIDTH_MS     (3000)
#elif defined(TINY_GSM_MODEM_SIM7670G)
    #define MODEM_POWERON_PULSE_WIDTH_MS      (100)
    #define MODEM_POWEROFF_PULSE_WIDTH_MS     (3000)
#elif defined(TINY_GSM_MODEM_SIM7600)
    #define MODEM_POWERON_PULSE_WIDTH_MS      (500)
    #define MODEM_POWEROFF_PULSE_WIDTH_MS     (3000)
    // T-SIM7600 startup time needs to wait
    #define MODEM_START_WAIT_MS               (15000)
#elif defined(TINY_GSM_MODEM_SIM7080)
    #define MODEM_POWERON_PULSE_WIDTH_MS      (1000)
    #define MODEM_POWEROFF_PULSE_WIDTH_MS     (1300)
#elif defined(TINY_GSM_MODEM_A7608)
    #define MODEM_POWERON_PULSE_WIDTH_MS      (1000)
    #define MODEM_POWEROFF_PULSE_WIDTH_MS     (3000)
#elif defined(TINY_GSM_MODEM_SIM7000SSL) || defined(TINY_GSM_MODEM_SIM7000)
    #define MODEM_POWERON_PULSE_WIDTH_MS      (1000)
    #define MODEM_POWEROFF_PULSE_WIDTH_MS     (1300)
#endif

#ifndef MODEM_START_WAIT_MS
    #define MODEM_START_WAIT_MS             3000
#endif
