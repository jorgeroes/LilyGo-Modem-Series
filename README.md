# T-A7670X-S3-Standard — LIS3DHTR sensor reader

Firmware para la placa **T-A7670X-S3-Standard** (ESP32-S3) que lee el acelerómetro **LIS3DHTR** vía I2C a 5 Hz y envía los datos por Serial.

## Hardware

| Placa | T-A7670X-S3-Standard |
|---|---|
| MCU | ESP32-S3-WROOM-1-N16R2 |
| Sensor | LIS3DHTR (Seeed / STMicro) |
| Interfaz | I2C |

## Conexión I2C

| Señal | GPIO |
|---|---|
| SDA | GPIO 3 |
| SCL | GPIO 2 |
| VCC | 3.3 V |
| GND | GND |

> Dirección I2C: `0x19` (SDO a VCC, por defecto) o `0x18` (SDO a GND).

## Configuración del sensor

| Parámetro | Valor |
|---|---|
| ODR hardware | 10 Hz (mínimo nativo más cercano a 5 Hz) |
| Tasa de muestreo efectiva | 5 Hz (intervalo 200 ms en software) |
| Rango full-scale | ±2 g |

## Build & Flash

**Requisitos:** PlatformIO (VS Code extension o CLI).

```bash
# Compilar
pio run

# Compilar y flashear
pio run --target upload

# Monitor serial (115200 baud)
pio device monitor
```

El entorno activo está configurado en [platformio.ini](platformio.ini) como `default_envs = T-A7670X-S3-Standard`.

## Salida serial esperada

```
=== LIS3DHTR 5 Hz reader ===
I2C SDA=GPIO3 | SCL=GPIO2
Sensor inicializado. ODR=10 Hz, rango=±2 g, salida a 5 Hz.
---
X:  0.0156 g | Y: -0.0078 g | Z:  1.0039 g
X:  0.0156 g | Y: -0.0078 g | Z:  1.0078 g
```

## Referencias

La carpeta [references/](references/) contiene datasheets, esquemáticos, dimensiones y la librería del sensor.
