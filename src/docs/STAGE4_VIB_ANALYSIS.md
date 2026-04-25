# Stage 4 — Pipeline de análisis local (`vib-analysis`)

Blueprint para crear el repositorio Python que descarga las sesiones generadas por la placa T-LILYGO A7670 desde S3 y produce el análisis de patrones de conducción a partir del acelerómetro LIS3DHTR.

Este documento vive **en el repo del firmware** (referencia técnica del contrato S3 + esquema CSV). Cuando se cree el repo `vib-analysis`, el README de ese repo apuntará de vuelta aquí para el detalle del schema.

---

## 1. Contexto

- El firmware sube cada 5 min un chunk CSV (~88 KB) a `s3://vib-data-707859598777/lilygo-a7670/session_<epoch>/part_NNNN.csv`. Cada `session_<epoch>` es una sesión de manejo continuo (un boot del dispositivo). Los chunks dentro de una sesión están numerados secuencialmente desde `part_0001.csv`.
- El esquema CSV es fijo (mismo de VIB):
  ```
  host_time_iso, device_time_us, ax_g, ay_g, az_g, device_event_id
  ```
  Unidades: aceleraciones en g (1 g = 9.81 m/s²), `host_time_iso` en UTC ISO-8601 con resolución de milisegundos.
- Sample rate: **5 Hz** fijo. Cada chunk tiene ~1500 muestras.

El repo `vib-analysis` debe encargarse de:
1. **Descubrir** sesiones disponibles en S3.
2. **Descargar y unir** los chunks de una sesión en un CSV continuo.
3. **Analizar** la distribución de aceleraciones para perfilar al conductor.

---

## 2. Setup del repo

### Estructura propuesta

```
vib-analysis/
├── README.md                      # apunta a este blueprint para el contrato de datos
├── pyproject.toml                 # dependencias y metadata
├── .gitignore                     # excluye .env, .venv, data/, *.csv descargados
├── .env.example                   # plantilla de variables (sin valores reales)
├── tools/
│   ├── list_sessions.py           # LIST de sesiones en S3
│   └── merge_session.py           # descarga y une chunks de una sesión
├── notebooks/
│   └── driver_profile.ipynb       # análisis de aceleraciones (sección 6)
├── data/                          # CSVs descargados, gitignored
└── src/
    └── vib_analysis/
        ├── __init__.py
        ├── loader.py              # cargar sesiones a DataFrame
        └── driver.py              # cálculos de percentiles y perfiles
```

### `pyproject.toml`

```toml
[project]
name = "vib-analysis"
version = "0.1.0"
requires-python = ">=3.10"
dependencies = [
    "boto3>=1.34",
    "pandas>=2.2",
    "numpy>=1.26",
    "matplotlib>=3.8",
    "python-dotenv>=1.0",
]

[project.optional-dependencies]
dev = ["jupyter", "ipykernel"]
```

Crear y entrar al venv:

```bash
python -m venv .venv
source .venv/bin/activate          # macOS/Linux
.\.venv\Scripts\activate           # Windows PowerShell
pip install -e ".[dev]"
```

### `.gitignore`

```
.env
.venv/
data/
*.csv
*.parquet
.ipynb_checkpoints/
__pycache__/
```

---

## 3. Credenciales AWS

El repo necesita permisos de **lectura** sobre el prefijo `lilygo-a7670/`. **No** necesita permisos de escritura — el firmware es quien escribe.

### Permisos IAM mínimos

Política IAM aplicada al usuario o role que use el script:

```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Sid": "ListPrefix",
      "Effect": "Allow",
      "Action": "s3:ListBucket",
      "Resource": "arn:aws:s3:::vib-data-707859598777",
      "Condition": {
        "StringLike": {
          "s3:prefix": ["lilygo-a7670/*"]
        }
      }
    },
    {
      "Sid": "ReadObjects",
      "Effect": "Allow",
      "Action": "s3:GetObject",
      "Resource": "arn:aws:s3:::vib-data-707859598777/lilygo-a7670/*"
    }
  ]
}
```

### Ideal: usuario IAM separado para análisis

Crear un usuario IAM `vib-analysis-reader` distinto del que usa el firmware (`vib-firmware-writer`). Ventajas: rotar credenciales independientes, revocar lectura sin afectar al firmware, audit log separado.

Alternativamente, en local **se pueden reutilizar las credenciales VIB existentes** si ya tienen `s3:GetObject` y `s3:ListBucket` (probable). Verificar con:

```bash
aws s3 ls s3://vib-data-707859598777/lilygo-a7670/ --profile vib
```

Si funciona → reutilizables. Si no → crear el usuario nuevo.

### Configuración de credenciales en local

**Opción A — `aws configure` (recomendada, estándar de la industria):**

```bash
aws configure --profile vib-analysis
# AWS Access Key ID: AKIA...
# AWS Secret Access Key: ...
# Default region: us-east-2
# Default output format: json
```

Las credenciales quedan en `~/.aws/credentials` (Linux/macOS) o `%USERPROFILE%\.aws\credentials` (Windows). **No** comiteadas, fuera del repo.

Los scripts de Python usan el profile vía:

```python
import boto3
session = boto3.Session(profile_name="vib-analysis")
s3 = session.client("s3")
```

**Opción B — variables de entorno con `.env`:**

Si prefieres credenciales por proyecto en lugar de globales:

```bash
# .env (gitignored)
AWS_ACCESS_KEY_ID=AKIA...
AWS_SECRET_ACCESS_KEY=...
AWS_DEFAULT_REGION=us-east-2
VIB_BUCKET=vib-data-707859598777
VIB_PREFIX=lilygo-a7670/
```

```python
from dotenv import load_dotenv
load_dotenv()
import boto3
s3 = boto3.client("s3")  # toma credenciales de las env vars
```

Y `.env.example` (sin valores reales, sí comiteado):

```
AWS_ACCESS_KEY_ID=
AWS_SECRET_ACCESS_KEY=
AWS_DEFAULT_REGION=us-east-2
VIB_BUCKET=vib-data-707859598777
VIB_PREFIX=lilygo-a7670/
```

**Lo que NO hacer:**
- Hardcodear credenciales en `.py` ni notebooks.
- Comitear `.env` ni `~/.aws/credentials`.
- Usar las credenciales del firmware (`secrets.ini`) en local — son el mismo IAM user pero conceptualmente queremos separar lectura de escritura.

---

## 4. Tools mínimos

### `tools/list_sessions.py`

Lista las sesiones disponibles en S3 con su epoch, fecha legible y número de chunks.

```python
"""Lista sesiones disponibles bajo lilygo-a7670/ en S3."""
import os
from datetime import datetime, timezone
import boto3
from collections import defaultdict
from dotenv import load_dotenv

load_dotenv()
BUCKET = os.environ.get("VIB_BUCKET", "vib-data-707859598777")
PREFIX = os.environ.get("VIB_PREFIX", "lilygo-a7670/")

s3 = boto3.client("s3")

paginator = s3.get_paginator("list_objects_v2")
sessions = defaultdict(list)
for page in paginator.paginate(Bucket=BUCKET, Prefix=PREFIX):
    for obj in page.get("Contents", []):
        key = obj["Key"]
        # esperado: lilygo-a7670/session_<epoch>/part_NNNN.csv
        rel = key.removeprefix(PREFIX)
        if "/" not in rel:
            continue
        session_id, _, part = rel.partition("/")
        if not session_id.startswith("session_") or not part:
            continue
        sessions[session_id].append((part, obj["Size"], obj["LastModified"]))

print(f"{'session':<32} {'epoch':<12} {'fecha UTC':<22} {'chunks':>6} {'KB':>8}")
print("-" * 88)
for sid in sorted(sessions.keys()):
    parts = sessions[sid]
    epoch = int(sid.removeprefix("session_"))
    fecha = datetime.fromtimestamp(epoch, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    total_kb = sum(sz for _, sz, _ in parts) // 1024
    print(f"{sid:<32} {epoch:<12} {fecha:<22} {len(parts):>6} {total_kb:>8}")
```

Uso:

```bash
python tools/list_sessions.py
```

### `tools/merge_session.py`

Descarga todos los `part_NNNN.csv` de una sesión y los une en un único CSV local.

```python
"""Descarga y concatena los chunks de una sesión en un único CSV."""
import sys
import os
from pathlib import Path
import boto3
from dotenv import load_dotenv

load_dotenv()
BUCKET = os.environ.get("VIB_BUCKET", "vib-data-707859598777")
PREFIX = os.environ.get("VIB_PREFIX", "lilygo-a7670/")

if len(sys.argv) != 2:
    print("Uso: python tools/merge_session.py <session_id>")
    print("  ej: python tools/merge_session.py session_1776956305")
    sys.exit(1)

session_id = sys.argv[1]
out_dir = Path("data")
out_dir.mkdir(exist_ok=True)
out_path = out_dir / f"{session_id}_merged.csv"

s3 = boto3.client("s3")

paginator = s3.get_paginator("list_objects_v2")
chunk_keys = []
for page in paginator.paginate(Bucket=BUCKET, Prefix=f"{PREFIX}{session_id}/"):
    for obj in page.get("Contents", []):
        if obj["Key"].endswith(".csv"):
            chunk_keys.append(obj["Key"])
chunk_keys.sort()

if not chunk_keys:
    print(f"No se encontraron chunks para {session_id}")
    sys.exit(1)

print(f"Bajando {len(chunk_keys)} chunks de {session_id}...")
with open(out_path, "wb") as out:
    for i, key in enumerate(chunk_keys):
        body = s3.get_object(Bucket=BUCKET, Key=key)["Body"].read()
        # Skip CSV header en chunks 2+ (cada chunk trae su propio header)
        if i > 0:
            body = body.split(b"\n", 1)[1]
        out.write(body)
        print(f"  [{i+1}/{len(chunk_keys)}] {key.rsplit('/', 1)[-1]}")

print(f"\nMerged → {out_path} ({out_path.stat().st_size // 1024} KB)")
```

Uso:

```bash
python tools/merge_session.py session_1776956305
# → data/session_1776956305_merged.csv
```

### Verificación rápida

Tras configurar credenciales, validar todo:

```bash
# Acceso lectura S3
aws s3 ls s3://vib-data-707859598777/lilygo-a7670/

# Listado vía script
python tools/list_sessions.py

# Descargar una sesión completa
python tools/merge_session.py session_<epoch>

# Inspeccionar
head -5 data/session_<epoch>_merged.csv
wc -l data/session_<epoch>_merged.csv
```

---

## 5. Identificación del eje longitudinal

Antes de cualquier análisis hay que saber **cuál de los tres ejes (`ax_g`, `ay_g`, `az_g`) corresponde a la dirección longitudinal del auto** (eje de avance/frenado). Esto depende de cómo se monta físicamente la placa en el vehículo.

Tres opciones para determinarlo:

### A. Calibración por inspección

En reposo (auto detenido en plano):
- El eje **vertical** (Z global) marca aproximadamente **±1 g** (gravedad).
- Los otros dos marcan **~0 g**.

Identificas Z por gravedad. De los dos ejes restantes, el longitudinal es el que cambia más durante una aceleración o frenada conocida (ver paso B).

### B. Calibración por movimiento conocido

Hacer un ride breve con maniobras claras y mirar la traza temporal:
1. Auto detenido 10 s (baseline).
2. Aceleración fuerte recta 5 s.
3. Detenerse y mantener 5 s.
4. Frenar fuerte recta 5 s.
5. Detenerse 10 s.

El eje longitudinal mostrará un pulso positivo durante la aceleración y negativo durante la frenada (o al revés, según orientación). Un eje "lateral" mostraría señal en curvas, no en aceleraciones rectilíneas.

### C. Calibración futura por código

Más adelante se puede automatizar: detectar el eje longitudinal vía PCA o por la varianza condicional a la velocidad. Por ahora, **decisión manual basada en la primera prueba real**.

**Convención asumida en el resto del documento:** llamamos `accel_long` al eje longitudinal identificado, sea `ax_g`, `ay_g` o `az_g`. En el código, definirlo como variable al inicio del notebook:

```python
LONGITUDINAL_AXIS = "ax_g"  # ajustar según calibración
```

---

## 6. Análisis: distribución de aceleraciones longitudinales

### Objetivo

Entender el perfil de conducción a partir de la distribución de aceleraciones en el eje longitudinal:

- ¿Cuán fuerte acelera y frena en general?
- ¿Acelera prudente y frena brusco, o al revés?
- ¿Es consistente o tiene picos extremos?

### Estructura del análisis

#### 6.1 Distribución global (todo el eje longitudinal)

Histograma simple, percentiles 50, 75, 90, 95, 99, 99.5 sobre el valor absoluto. Da idea de **intensidad general** sin diferenciar acelerar de frenar.

```python
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

LONGITUDINAL_AXIS = "ax_g"   # según calibración

df = pd.read_csv("data/session_1776956305_merged.csv", parse_dates=["host_time_iso"])
long_g = df[LONGITUDINAL_AXIS]

print("Distribución |aceleración longitudinal| (g):")
for p in [50, 75, 90, 95, 99, 99.5]:
    print(f"  P{p}: {np.percentile(np.abs(long_g), p):.3f} g")

fig, ax = plt.subplots(figsize=(10, 4))
ax.hist(long_g, bins=80, edgecolor="black", linewidth=0.3)
ax.set_xlabel("Aceleración longitudinal (g)")
ax.set_ylabel("Frecuencia")
ax.set_title("Distribución global del eje longitudinal")
ax.axvline(0, color="red", linestyle="--", linewidth=1, alpha=0.5)
plt.tight_layout()
```

#### 6.2 Descomposición acelerar vs frenar

Aquí está el núcleo de lo que pediste. Separar el eje longitudinal en dos series:

- **Aceleraciones positivas** = el conductor está acelerando.
- **Aceleraciones negativas** = el conductor está frenando.

Y graficar **ambas en valor absoluto** sobre el mismo eje X, para que sean directamente comparables.

```python
accel_pos = long_g[long_g > 0]              # acelera
brake_neg = long_g[long_g < 0].abs()        # frena (volcado a positivo)

print("\nAceleraciones (positivas, conductor acelerando):")
print(f"  N muestras: {len(accel_pos)}")
for p in [50, 75, 90, 95, 99]:
    print(f"  P{p}: {np.percentile(accel_pos, p):.3f} g")

print("\nFrenadas (negativas → abs, conductor frenando):")
print(f"  N muestras: {len(brake_neg)}")
for p in [50, 75, 90, 95, 99]:
    print(f"  P{p}: {np.percentile(brake_neg, p):.3f} g")

# Plot lado a lado, mismo eje X, escala log para ver la cola
fig, ax = plt.subplots(figsize=(10, 5))
bins = np.linspace(0, max(accel_pos.max(), brake_neg.max()), 60)
ax.hist(accel_pos, bins=bins, alpha=0.55, label=f"Acelerando (n={len(accel_pos)})", color="tab:green")
ax.hist(brake_neg, bins=bins, alpha=0.55, label=f"Frenando |x| (n={len(brake_neg)})", color="tab:red")
ax.set_xlabel("Magnitud de aceleración longitudinal (g)")
ax.set_ylabel("Frecuencia (log)")
ax.set_yscale("log")
ax.legend()
ax.set_title("Acelerar vs frenar — distribución comparada")
plt.tight_layout()
```

#### 6.3 Tabla resumen comparativa

```python
def summary(s, label):
    return {
        "lado": label,
        "n": len(s),
        "media_g": s.mean(),
        "P50": np.percentile(s, 50),
        "P75": np.percentile(s, 75),
        "P90": np.percentile(s, 90),
        "P95": np.percentile(s, 95),
        "P99": np.percentile(s, 99),
        "max_g": s.max(),
    }

resumen = pd.DataFrame([
    summary(accel_pos, "acelera"),
    summary(brake_neg, "frena"),
])
print(resumen.to_string(index=False, float_format=lambda x: f"{x:.3f}"))
```

#### 6.4 Clasificación cualitativa del conductor

Con los percentiles P95 (umbral de "fuerte") definimos cuatro arquetipos. Los umbrales son orientativos y se ajustan con experiencia:

```python
THRESHOLD_HARD_G = 0.25   # umbral aceleración/frenada "fuerte" en g

p95_acc = np.percentile(accel_pos, 95)
p95_brk = np.percentile(brake_neg, 95)

acc_fuerte = p95_acc > THRESHOLD_HARD_G
brk_fuerte = p95_brk > THRESHOLD_HARD_G

if acc_fuerte and brk_fuerte:
    perfil = "Brusco (acelera y frena fuerte)"
elif acc_fuerte and not brk_fuerte:
    perfil = "Agresivo al acelerar, prudente al frenar"
elif not acc_fuerte and brk_fuerte:
    perfil = "Prudente al acelerar, brusco al frenar"
else:
    perfil = "Suave (consistente y prudente)"

print(f"\nPerfil del conductor: {perfil}")
print(f"  P95 acelerar : {p95_acc:.3f} g  (umbral 'fuerte': {THRESHOLD_HARD_G})")
print(f"  P95 frenar   : {p95_brk:.3f} g")
```

#### 6.5 Traza temporal (contexto visual)

Útil para ver dónde caen los eventos extremos en el tiempo:

```python
fig, ax = plt.subplots(figsize=(14, 4))
ax.plot(df["host_time_iso"], long_g, linewidth=0.6, color="black")
ax.axhline(THRESHOLD_HARD_G, color="green", linestyle="--", alpha=0.5, label=f"+{THRESHOLD_HARD_G} g (acel fuerte)")
ax.axhline(-THRESHOLD_HARD_G, color="red", linestyle="--", alpha=0.5, label=f"-{THRESHOLD_HARD_G} g (frenada fuerte)")
ax.axhline(0, color="gray", linewidth=0.5)
ax.set_xlabel("Tiempo (UTC)")
ax.set_ylabel("Aceleración longitudinal (g)")
ax.set_title("Traza temporal del eje longitudinal")
ax.legend()
plt.tight_layout()
```

### Métricas derivables

A partir de lo anterior se pueden definir indicadores compuestos para ranquear o comparar conductores:

- **Smoothness Index** = inverso de la desviación estándar de la aceleración longitudinal.
- **Aggressive Driving Score** = fracción del tiempo con `|long_g| > THRESHOLD_HARD_G`.
- **Asymmetry Ratio** = `P95(brake) / P95(accel)`. Mayor que 1 indica frenadas más bruscas que aceleraciones.

Estas métricas resumen una sesión en pocos números, útiles para series temporales (mismo conductor a lo largo del tiempo) o comparación de conductores.

---

## 7. Roadmap de evolución del repo

Lo descrito arriba es el MVP del análisis. Evoluciones naturales:

1. **Cargar varias sesiones a la vez**: comparar entre conductores o un mismo conductor en distintos viajes.
2. **Integrar metadata por sesión**: `device.json` y `session.json` (driver_id, vehicle_id) están en SD pero no se suben a S3 actualmente. Decisión a tomar: subirlos como sidecar JSON al prefijo de la sesión, o llevar metadata en una tabla separada.
3. **Detección de eventos discretos**: en lugar de percentiles continuos, detectar "eventos de frenada brusca" como picos > umbral con duración mínima. Permite contar eventos por sesión, no solo distribuciones.
4. **Análisis multi-eje**: incorporar el eje lateral para detectar curvas bruscas (cornering). Pasar de "perfil longitudinal" a "perfil 2D".
5. **Integración con velocidad**: cuando el firmware integre GPS (vía A7670 o módulo externo), normalizar las aceleraciones por velocidad para detectar eventos relativos al estado del vehículo.

Estas evoluciones se abordan después de que el MVP de análisis esté funcionando con datos reales — no al revés.

---

## 8. Comandos completos de bootstrap (resumen)

Asumiendo que tienes Python 3.10+ y AWS CLI instalado:

```bash
# 1. Crear repo
mkdir vib-analysis && cd vib-analysis
git init

# 2. Copiar pyproject.toml, .gitignore, .env.example desde este blueprint
# (o crearlos a mano según secciones 2 y 3)

# 3. Crear venv y dependencias
python -m venv .venv
source .venv/bin/activate          # macOS/Linux
.\.venv\Scripts\activate           # Windows PowerShell
pip install -e ".[dev]"

# 4. Configurar credenciales AWS (opción A)
aws configure --profile vib-analysis

# 5. Verificar acceso
aws s3 ls s3://vib-data-707859598777/lilygo-a7670/ --profile vib-analysis

# 6. Crear tools/ y notebooks/, copiar el código de la sección 4

# 7. Listar sesiones
python tools/list_sessions.py

# 8. Descargar una sesión
python tools/merge_session.py session_<epoch>

# 9. Abrir el notebook
jupyter notebook notebooks/driver_profile.ipynb

# 10. Primer commit
git add .
git commit -m "feat: bootstrap vib-analysis with S3 download tools and driver profile notebook"
```
