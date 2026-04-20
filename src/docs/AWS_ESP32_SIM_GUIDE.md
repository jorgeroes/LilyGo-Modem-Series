# Guía AWS para ESP32 + SIM (T-LILYGO A7670)

Guía corta de subida de datos a AWS desde la placa. El foco está en la **Fase 1** (S3 directo), que es la ruta a implementar ahora. Las Fases 2 y 3 quedan documentadas como evolución futura.

---

## 1. Decisiones heredadas del proyecto VIB

Este proyecto reutiliza la infraestructura AWS ya configurada para VIB. **No hay que crear bucket, usuario IAM ni política nueva.**

| Recurso | Valor | Notas |
|---|---|---|
| Bucket S3 | `vib-data-707859598777` | us-east-2 |
| Prefijo de este proyecto | `lilygo-a7670/` | separa los CSVs de los de VIB |
| Usuario IAM | el existente de VIB | política actual: `s3:PutObject` sobre el bucket; suficiente |
| Esquema CSV | `host_time_iso, device_time_us, ax_g, ay_g, az_g, device_event_id` | mismo que VIB, para que el pipeline de análisis no cambie |
| Frecuencia de muestreo | **5 Hz** | decisión deliberada, no subir a 100 Hz |

Las credenciales (`AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`) no viven en este documento. Se configuran fuera del repo (p. ej. variables PlatformIO o archivo `.env` gitignored) y se embeben en el firmware al compilar.

---

## 2. Flujo operativo (sesión autónoma, sin interacción)

La placa funciona sola: al encenderse empieza a grabar, sube periódicamente, y al apagarse simplemente deja de subir.

1. **Al boot**: generar un identificador de sesión único (p. ej. `session_<epoch>.csv`) y abrir el CSV en buffer/SD.
2. **Loop**: acumular muestras a 5 Hz en el CSV.
3. **Cada 10 min**: `PUT` HTTPS con SigV4 del CSV **completo acumulado** a:
   ```
   s3://vib-data-707859598777/lilygo-a7670/session_<epoch>.csv
   ```
   Cada PUT sobrescribe la misma key: el objeto en S3 va creciendo a lo largo de la sesión.
4. **Al apagarse**: no se hace nada. Se pierden las muestras desde el último PUT (< 10 min). **Trade-off aceptado** a cambio de no necesitar shutdown graceful.

**Nota operativa**: habilitar versionado en el bucket (si no está activo) evita que un PUT corrupto en mitad de la sesión destruya el histórico del objeto — siempre se puede recuperar la versión anterior.

---

## 3. Fase 1 — S3 directo con SigV4 *(implementar ahora)*

### Arquitectura

```
┌──────────────────┐     HTTPS PUT + SigV4      ┌──────────┐
│ ESP32 + A7670    │  ────────────────────────► │    S3    │
│ (creds en flash) │                            └──────────┘
└──────────────────┘
```

### Qué implementar en firmware

- Cliente HTTPS sobre el A7670 vía TinyGSM (`TinyGsmClientSecure`).
- Firma AWS Signature V4 de cada request `PUT`.
- Sincronización de hora del módem al arranque (SigV4 rechaza requests con skew >15 min): usar el reloj del módem tras registrar red.
- Reintento simple: si el PUT falla, reintentar al siguiente ciclo de 10 min con el CSV acumulado hasta ese momento.

### Librerías (PlatformIO)

```ini
lib_deps =
    jandelgado/aws-sigv4-esp32       ; firma SigV4
    vshymanskyy/TinyGSM@^0.11.7      ; módem A7670
```

### Constraint del módem: APN Entel BAM sin DNS

El APN `bam.entelpcs.cl` da IP pero **no resuelve DNS**. Para que el PUT a `vib-data-707859598777.s3.us-east-2.amazonaws.com` funcione:

- Resolver el hostname con `AT+CDNSGIP="..."` antes de abrir la conexión, o
- Hardcodear una IP de S3 us-east-2 (frágil — AWS rota IPs).

La primera opción es la recomendada.

### Código de referencia (VIB)

La lógica Python de VIB se puede portar a C++ como plantilla:

| Qué | Archivo |
|---|---|
| Subida a S3 y deduplicación | [`VIB/src/acquisition/sync/s3_uploader.py`](../../../VIB/src/acquisition/sync/s3_uploader.py) |
| Esquema CSV | [`VIB/src/core/config.py`](../../../VIB/src/core/config.py) |

---

## 4. Fase 2 — AWS IoT Core para telemetría *(futuro)*

Si llega el momento de tener dashboard en tiempo real o más de ~10 dispositivos, el salto es a MQTT/TLS contra AWS IoT Core con certificado X.509 único por placa. En lugar de subir el CSV crudo se publican eventos agregados (p. ej. RMS, picos), y los CSVs completos se guardan en SD para recogerlos físicamente. Ventaja: revocación granular por dispositivo en un click.

---

## 5. Fase 3 — IoT Core + Presigned URLs *(futuro lejano)*

Evolución natural de la Fase 2 cuando se comercialice a terceros o se necesite auditoría por dispositivo. El ESP32 publica un `session/end` por MQTT, una Lambda genera una URL S3 firmada válida 15 min y la devuelve por MQTT; la placa hace `PUT` HTTP a esa URL sin llevar credenciales AWS en flash. Si alguien dumpea la memoria, lo único que encuentra es el certificado del dispositivo, que se revoca individualmente.

---

## 6. Verificación end-to-end (Fase 1)

- [ ] `curl` con las credenciales VIB sube un CSV de prueba a `s3://vib-data-707859598777/lilygo-a7670/test.csv`.
- [ ] Tras un boot real, aparece `session_<epoch>.csv` en S3 dentro de los primeros 10 min.
- [ ] Un segundo PUT a los 20 min **sobrescribe** la misma key (sin crear duplicado).
- [ ] Apagar la placa durante una sesión: el objeto en S3 queda íntegro con el contenido del último PUT completo.
- [ ] La placa reconecta automáticamente tras perder cobertura celular.

---

## 7. Costes y volumen (Fase 1)

A 5 Hz con 6 columnas en CSV sin comprimir: **~1.3 MB/hora** (orden de magnitud; recalcular con el ancho de fila final).

- S3 PUTs: 6/hora → coste negligible.
- S3 Storage: < $0.05/mes por dispositivo.
- **Datos celulares** = coste dominante. Si se vuelve problema, evaluar `gzip` antes del PUT (ahorra ~70% en señales de acelerómetro).
