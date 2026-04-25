# T-A7670X-S3-Standard — Logger LIS3DHTR con subida a S3

Firmware Stage 1 MVP para la placa **T-A7670X-S3-Standard** (ESP32-S3 + módem celular A7670G). Lee el acelerómetro **LIS3DHTR** a 5 Hz, guarda CSV en microSD y sube chunks periódicos a **AWS S3** vía SigV4 sobre la red celular.

Flujo:

```
LIS3DHTR (I2C 5 Hz) → SD (/session_<epoch>.csv) → A7670G (BAM/Entel) → S3 (SigV4)
```

## Hardware

| Componente | Referencia |
|---|---|
| Placa | T-A7670X-S3-Standard (LilyGO) |
| MCU | ESP32-S3-WROOM-1-N16R2 |
| Módem | A7670G-LLSE (sin GPS built-in) |
| Sensor | LIS3DHTR (Seeed / STMicro, I2C) |
| Almacenamiento | microSD (SPI, formateada FAT32) |

### Pines

| Señal | GPIO |
|---|---|
| I2C SDA / SCL | 3 / 2 |
| SD SCK / MISO / MOSI / CS | 12 / 13 / 11 / 10 |
| Módem RX / TX / PWRKEY | 5 / 4 / 46 |

## Configuración del sensor

| Parámetro | Valor |
|---|---|
| ODR hardware | 10 Hz (mínimo nativo más cercano a 5 Hz) |
| Tasa de muestreo efectiva | 5 Hz (intervalo 200 ms por software) |
| Rango full-scale | ±2 g |

## Build & Flash

**Requisitos:**
- [PlatformIO](https://platformio.org/) (extensión VS Code o CLI).
- Un archivo `secrets.ini` en la raíz con las credenciales AWS (gitignored). Ver [secrets.ini.example](secrets.ini.example) si existe, o formato:
  ```ini
  [secrets]
  build_flags =
      -DAWS_ACCESS_KEY_ID=\"...\"
      -DAWS_SECRET_ACCESS_KEY=\"...\"
      -DAWS_REGION=\"us-east-2\"
      -DAWS_S3_BUCKET=\"<bucket>\"
      -DAWS_S3_PREFIX=\"lilygo-a7670/\"
  ```

Comandos (PlatformIO CLI, portables):

```bash
# Compilar
pio run -e T-A7670X-S3-Standard

# Compilar + flashear + abrir monitor (115200 baud)
pio run -e T-A7670X-S3-Standard -t upload -t monitor

# Solo monitor (ej. tras reset físico)
pio device monitor --baud 115200
```

El entorno por defecto está en [platformio.ini](platformio.ini) como `default_envs = T-A7670X-S3-Standard`.

> Para comandos específicos de tu entorno local (rutas absolutas a `platformio.exe`, puerto COM, etc.) mantén un `commands.md` en la raíz — está en `.gitignore` y no se sube al repo.

## Archivos persistentes en SD

La SD necesita estar formateada **FAT32** e idealmente contener:

- `/device.json` — metadatos fijos del equipo:
  ```json
  { "vehicle_id": "veh-01", "device_id": "lilygo-a7670-01" }
  ```
- `/session.json` — metadatos de la sesión actual:
  ```json
  { "driver_id": "drv-01", "time_of_day": "morning", "notes": "" }
  ```

Ambos son opcionales; si faltan, el firmware usa valores `UNKNOWN`.

El firmware crea por sesión:
- `/session_<epoch>.csv` — datos crudos del acelerómetro, rotado tras cada PUT exitoso.
- `/session_<epoch>.meta.json` — sidecar con configuración del run.

## Salida serial esperada

```
=== Stage 1 MVP: LIS3DHTR -> SD -> S3 ===
[sensor] OK (ODR=10 Hz, ±2 g)
[sd] OK, 15200 MB
[modem] Pulsando PWRKEY...
[modem] Operador: 73001
[modem] IP local: 10.x.x.x
[modem] Gate DNS: esperando resolución de httpbin.org (90 s max)...
[modem] DNS OK en intento 1 (87 ms)
[modem] Hora HTTPS: "Wed, 22 Apr 2026 21:58:24 GMT" -> epoch=1776895104
[csv] Sesión: /session_1776895104.csv -> s3://.../lilygo-a7670/session_1776895104/
[aws] PUT .../part_0001.csv (17458 B)
[aws] OK status=200
[up] chunk 1 OK (301 muestras), rotando CSV
```

## Roadmap

### Stage 1 (cerrado)

Adquisición + SD + chunked uploads a S3 (~88 KB cada 5 min). Validado end-to-end con 7 chunks consecutivos `status=200`.

### Stage 2a (cerrado)

Resilience básica:
- `f.flush()` por muestra para reducir ventana de pérdida ante power-cut.
- Retry de `SD.begin()` al boot ante estado transitorio post power-cycle.
- Auto-reconexión del módem (`hardRestart` + reattach) tras 2 fallos consecutivos de PUT.
- Manejo del pool CGNAT 100.x de BAM (gate DNS pasa pero outbound HTTP muere) con `hardRestart` para rerolear.

### Stage 2b (pendiente)

**Boot sync de sesiones huérfanas.** Si la placa muere mid-sesión, los chunks no subidos quedan en SD y se ignoran al próximo boot. Solución: al arrancar, escanear `/session_*.csv`, subir lo pendiente al prefijo S3 correspondiente, y solo entonces borrarlo de SD. Requiere persistir `next_chunk_seq` en cada `.meta.json` y, opcionalmente, splitting interno si el huérfano excede el límite de 96 KB del `+HTTPDATA` del módem.

**Cuándo abordarlo:** antes del primer despliegue en vehículo real con power-cuts frecuentes (ignition off). Para uso en banco de pruebas con alimentación estable, prescindible.

### Pendientes adicionales sin etapa asignada

- Lifecycle SD: política para borrar sesiones ya confirmadas en S3 (a 6 chunks/h × ~88 KB, una SD de 16 GB aguanta ~1000 días — no urgente).
- Splitting interno de chunks > 96 KB (necesario solo si cambia el sample rate, esquema CSV, o intervalo de upload).

## Referencias

- [src/docs/AWS_ESP32_SIM_GUIDE.md](src/docs/AWS_ESP32_SIM_GUIDE.md) — guía completa de integración AWS.
- [references/](references/) — datasheets, esquemáticos, dimensiones y librería del sensor.
