# Perfilado de conductor a partir de señal filtrada a 5 Hz

Síntesis técnica y reflexiva sobre cómo, con lo que hoy ya produce el pipeline de EDA de VIB, se puede construir un **perfil de conducción defendible**: qué medir, a qué escalas acumularlo, y bajo qué criterio estadístico decidir que un conductor pertenece a un perfil determinado.

El texto no pretende agotar el espacio de soluciones. Presenta la ruta principal de análisis, señala las divergencias razonables y las hace converger en una misma pregunta operativa.

---

## 1. Tesis de partida

Un perfil de conductor **no es una clasificación instantánea**. Ningún evento aislado —ni una frenada fuerte, ni un giro brusco, ni un bache— caracteriza por sí solo a un conductor. Lo que caracteriza es la **recurrencia** de cierto tipo de comportamiento a lo largo del tiempo, observada a distintas resoluciones.

De ahí que el perfil deba entenderse como un **acumulado de evaluaciones**: la misma métrica se calcula sobre ventanas de un minuto, sobre ventanas de cinco, sobre trayectos enteros, sobre días, sobre arriendos completos. Cada escala responde a una pregunta distinta; todas convergen en una sola —¿dónde se sitúa este conductor en la distribución de referencia?

Lo que sigue articula cómo llegar desde la señal cruda del acelerómetro hasta esa respuesta, usando sólo lo que el sistema ya produce (o puede producir con extensiones menores).

---

## 2. Base de señal: por qué 5 Hz

La señal **se captura** a 5 Hz efectivos en el firmware ([src/main.cpp](src/main.cpp)): el LIS3DHTR se configura a ODR de 10 Hz y se muestrea por software cada 200 ms. El filtro interno del propio sensor a 10 Hz actúa como anti-alias implícito antes de la decimación. No hay, por tanto, un filtro Butterworth en el pipeline Python: la limitación de banda ya está impuesta por el hardware aguas arriba.

La elección es física, no arbitraria. La dinámica que produce el conductor con sus decisiones —frenar, acelerar, girar, corregir trayectoria— ocupa esencialmente la banda por debajo de 2 Hz. Nyquist a 5 Hz de muestreo es **2.5 Hz**: techo suficiente para la banda útil del conductor, insuficiente para resolver transientes más cortos que ~400 ms. Por encima de Nyquist viven la vibración del motor, la rugosidad del pavimento y el ruido mecánico del chasis —señales que el sensor ya atenúa y la decimación termina de descartar.

Muestrear a 5 Hz **separa al conductor del vehículo**. Esa separación es la condición para que todas las métricas que vengan después sean atribuibles, con razonable confianza, a la persona al volante y no al auto ni a la vía.

Si se quisiera aislar aún más la banda del conductor del residuo que pudiera cruzar el decimador, se puede aplicar un low-pass IIR adicional en ~1.5 Hz dentro del pipeline Python. No es prerequisito; es refinamiento opcional.

La validación empírica del ancho de banda útil está en los notebooks [notebooks/eda/threshold_calibration.ipynb](notebooks/eda/threshold_calibration.ipynb) y [notebooks/eda/event_distribution.ipynb](notebooks/eda/event_distribution.ipynb), que muestran cómo las distribuciones de |ax| y |ay| en tramos de movimiento son aptas para calibrar detectores por percentiles.

Sobre la señal de entrada, un ventaneo de 1 s con umbral de desviación estándar determina los tramos de reposo y de movimiento ([src/eda/config.py:12](src/eda/config.py#L12)). Todas las métricas de perfilado se calculan **sólo sobre tramos de movimiento**: el conductor detenido no se perfila.

> **Nota sobre BACHE.** La detección de bache mediante residual de alta frecuencia de `az` (señal cruda menos media móvil corta) **no es realizable a 5 Hz**: el fenómeno vive por encima de Nyquist. Esta familia de eventos queda fuera del alcance con el hardware actual y se traslada a §8 Limitaciones.

---

## 3. Tres familias de métricas, complementarias

Sobre la señal filtrada en movimiento, se distinguen tres familias. No son sustitutivas: cada una responde a una dimensión distinta del comportamiento, y un perfil robusto las integra.

**Frecuencia de eventos — cuántas veces.** Las tasas `jerk_rate_per_min` y `lateral_rate_per_min` ya se calculan en [src/eda/profile/profile.py:102-104](src/eda/profile/profile.py#L102-L104). Responden a "¿con qué asiduidad este conductor cruza el umbral de lo brusco?". Su virtud es que son **normalizables por tiempo**: una tasa por minuto compara trayectos de distinta duración sin sesgo. `bache_rate_per_min` no aplica con el muestreo actual a 5 Hz (ver §2).

**Energía sostenida — con qué intensidad continua.** El RMS de ax y ay (`ax_rms_moving`, `ay_rms_moving`) mide la energía media de la señal: una conducción nerviosa y continuamente corregida tendrá RMS alto sin generar un solo evento. Esta es la métrica que captura al conductor "inquieto pero no infractor". Complementar el RMS con **percentil 95** (`ax_p95_moving`, `ay_p95_moving`, pendientes pero triviales de añadir) y con **curtosis** (`ax_kurtosis_moving`) permite distinguir una señal plana-y-ruidosa de una señal de base suave con picos ocasionales —dos conductores distintos con RMS parecido.

**Severidad por evento — cómo de fuerte cuando ocurre.** Promediar `peak_g` sobre los eventos detectados (`mean_jerk_intensity`, `mean_lateral_intensity`, pendientes) distingue al conductor "moderado pero con frenadas duras" del "agresivo con frenadas suaves pero frecuentes". Tasas e intensidad son ortogonales: pueden ser altas ambas, una sola, o ninguna.

La reflexión: quedarse con una sola familia subrepresenta. Un conductor con `jerk_rate` bajo puede tener `ax_rms` alto y `mean_jerk_intensity` altísimo; otro con `jerk_rate` alto puede tener intensidad moderada. Reducir el perfil a un sólo eje —la tasa, el RMS— obligaría a identificar estos dos casos, que son distintos. El vector mínimo de perfilado tiene las tres familias.

---

## 4. El perfil como acumulado multi-escala

Si un perfil es recurrencia, la pregunta inmediata es: **¿recurrencia medida cada cuánto?**

### 4.1 Escalas cortas dentro del trayecto

No hay una escala temporal única que sea óptima. Cada una ilumina algo distinto.

- **1 minuto** tiene resolución alta. Captura micro-comportamiento: ráfagas de impaciencia en un atasco, un tramo puntual de conducción nerviosa. A 5 Hz son sólo **300 muestras** por ventana, lo que vuelve frágiles los percentiles altos y discreta la tasa de eventos. Útil más para **detectar anomalías** que para caracterizar.
- **5 minutos** es la escala natural de la conducción urbana. Un trayecto típico entre semáforos o entre giros se agota en esta ventana. A 5 Hz son **1 500 muestras** por ventana: suficientes para que las tasas y los percentiles sean estables, y suficiente granularidad para distinguir tramos distintos dentro de un mismo viaje.
- **10 minutos** casi coincide con un segmento completo homogéneo (**3 000 muestras** a 5 Hz). Útil para comparar tramos entre sí (p.ej. autopista vs. ciudad dentro del mismo trayecto), menos útil para capturar el detalle del comportamiento dentro de cada tramo.

La propuesta no es elegir una: es **calcular las métricas de §3 en ventanas deslizantes de las tres escalas** y mirar la distribución de cada una. Si las tres escalas coinciden en ubicar al conductor en el mismo percentil de la flota, el perfil es robusto a la resolución. Si divergen, la divergencia misma es información: un conductor cuya métrica a 1 minuto es muy dispersa pero que a 10 minutos parece regular es un conductor con **explosiones cortas**, no con agresividad sostenida.

Para cerrar con un valor único, la mediana y el p95 **de las ventanas** resumen la distribución: mediana describe la conducción típica del trayecto, p95 describe los peores cinco minutos de ese conductor en ese trayecto.

En la práctica, si hay que privilegiar una, **5 minutos** es la escala primaria: mejor compromiso señal/ruido, alineada con la duración natural de los segmentos. Las otras dos quedan como validación cruzada.

### 4.2 Escala de trayecto

Un trayecto —encendido a apagado, un CSV por sesión— es la unidad natural que hoy produce el pipeline ([src/eda/pipeline/run.py](src/eda/pipeline/run.py)). El resumen de ventanas del §4.1 se convierte en un **vector de features por trayecto**: mediana, p95 e IQR de cada métrica de §3, sobre las ventanas de 5 minutos. Es el objeto básico que alimenta las escalas superiores.

### 4.3 Escala diaria

Un día puede contener uno o varios trayectos. El perfil diario no es el promedio de los trayectos: es la **agregación ponderada por tiempo en movimiento**. Métricas relevantes:

- Tiempo total en movimiento del día (`duration_moving_s` sumado).
- Mediana y p95 de las tasas de evento **en las ventanas de todos los trayectos del día** (no sobre los valores-trayecto: se trata a todas las ventanas del día como una única población).
- Número de trayectos y huecos entre ellos —proxy de **frecuencia de uso**.
- Reparto horario: mañana/tarde/noche. La misma conducción a las 8:00 y a las 23:00 son eventos distintos a efectos de riesgo.

Esto requiere añadir `day_id` a la metadata de sesión (extensión menor de [src/eda/ingest/metadata.py](src/eda/ingest/metadata.py)) y un agregador que consuma los trayectos del día (pendiente, ver [docs/STATUS-PLAN.md](docs/STATUS-PLAN.md) Fase 2–3).

### 4.4 Escala multi-día: arriendo o ventana de semanas

Aquí la pregunta no es sólo dónde está el conductor, sino **qué tan estable es**. El coeficiente de variación de las métricas diarias es la herramienta: un conductor con CV bajo conduce de la misma forma casi todos los días, uno con CV alto tiene un comportamiento dependiente de contexto (día laboral vs. fin de semana, lluvia, humor). La estabilidad es, en sí misma, una dimensión del perfil.

La decisión de perfil en esta escala debería incorporar al menos dos estadísticos por métrica: su **nivel** (mediana a lo largo del arriendo) y su **variabilidad** (CV o IQR). Dos conductores con la misma mediana pero CV distinto no son iguales.

### 4.5 Convergencia

Las cuatro escalas generan vectores de features distintos —distintas unidades de comparación, distintas ventanas—, pero responden todas a la misma pregunta operativa: **¿dónde se ubica esta observación respecto a la distribución de referencia?** Lo que cambia es la observación: una ventana, un trayecto, un día, un arriendo. Lo que no cambia es el criterio.

Esto es lo que hace viable la estrategia: no se necesita un modelo distinto por escala. Se necesita **una sola familia de comparaciones percentílicas**, aplicada sucesivamente.

---

## 5. El vehículo como unidad paralela de estudio

Perfilar conductores sin perfilar vehículos introduce un sesgo sistemático. Un auto con suspensión desgastada produce más `az_std` y más BACHE; un auto con embrague duro produce más `ax` transitorios. Sin una línea base del vehículo, el conductor recibe en su perfil ruido que no es suyo.

Dos preguntas distintas sobre el auto:

- **Quién lo ha conducido.** Lista de perfiles de conductor asociados al `vehicle_id`, con mezcla temporal (cuánto del uso del auto lo explica cada perfil). Útil para interpretar el historial del vehículo.
- **Qué uso acumulado ha tenido.** Agregado de `duration_moving_s` como proxy de km y, cuando se integre GNSS en el firmware, distribución de velocidad y trazado geográfico. Predice desgaste y anticipa mantenimiento.

La utilidad crítica para el perfilado del conductor es **normalización cruzada**: las métricas del conductor se evalúan contra la línea base de ese vehículo, no contra una flota genérica. Si el auto X genera estructuralmente más `az_std` que la mediana de la flota, el conductor no debe ser penalizado por ello. El auto es un **coestimador**: sin él, confundimos conducción agresiva con vehículo maltratado.

Operativamente, el vehículo se trata como el conductor: se le construye su propia distribución de referencia (sobre todos los arriendos del auto, todos los días, todas las ventanas) y las métricas del conductor se expresan como **residuales contra esa base**.

---

## 6. Distribuciones y percentiles

Toda la estrategia descansa sobre una idea: los promedios ocultan, los percentiles revelan. Un conductor con un solo frenazo fuerte y uno con diez frenazos fuertes pueden tener la misma media de `ax`; su p95 no se parece.

### 6.1 Qué percentiles observar

- **p50 (mediana).** Comportamiento típico. Define la línea base del conductor. Estable, robusta a outliers. Es lo que ese conductor hace "normalmente". Necesaria pero no suficiente —dos conductores muy distintos pueden compartir mediana si uno es regular y otro tiene episodios extremos compensados.
- **p90.** Comportamiento recurrente intenso. Captura el 10 % superior —demasiado frecuente para ser accidente, demasiado alto para ser rutina. Es probablemente **el percentil más informativo para perfilar**: distingue al conductor agresivo de forma sostenida del que tiene repuntes esporádicos sin contaminarse del todo con los eventos raros.
- **p95.** Conducta límite pero recurrente. Útil para scoring de riesgo: es el umbral que separa "mal día puntual" de "patrón que se repite una de cada veinte ventanas". También la referencia natural para definir umbrales de clasificación (por ejemplo, un conductor está "sobre la flota" si su p95 excede el p95 de la flota).
- **p99.** Outliers. Útil para **calibrar detectores** —los umbrales actuales de JERK (0.28 g) y LATERAL (0.30 g) se fijaron como p99 de |ax_lp| y |ay_lp| en movimiento ([thresholds.yaml](thresholds.yaml))—, pero **no para perfilar**: un percentil 99 es un puñado de eventos, demasiado volátil entre sesiones para caracterizar.
- **IQR (p75 − p25).** Dispersión del comportamiento típico. Un conductor con IQR amplio tiene menos predecibilidad: su conducción varía mucho incluso en su rango normal. Interesa no tanto el nivel como la anchura.

En síntesis: p50 para la línea base, p90 para la recurrencia intensa, p95 para el umbral de riesgo, IQR para la predecibilidad. Cuatro números por métrica, aplicados a cada escala. Es mucho, pero todo converge a lo mismo: caracterizar la forma de la distribución del conductor, no un valor único.

### 6.2 Dos usos del percentil: intra y contra la flota

Los percentiles se aplican dos veces, y es importante no confundir los roles.

**Intra-conductor**, los percentiles **describen** al conductor. El p95 de `ax_p95_moving` sobre las ventanas de 5 minutos de un conductor dice "cómo es la frenada fuerte típica de este conductor". No compara con nadie: caracteriza su propia distribución.

**Inter-conductor**, el valor obtenido se expresa como percentil **contra la distribución de la flota**. "El p95 de este conductor está en el percentil 82 de los p95 de la flota" —ahora hay clasificación. Es la única forma de decir "agresivo" o "moderado" sin caer en umbrales arbitrarios.

Los dos usos son secuenciales: primero se construye la distribución intra-conductor (§4), luego se posiciona contra la de la flota (§6.2 inter). Saltarse el primero y comparar valores crudos contra la flota confunde ruido de trayecto con diferencias de conductor.

### 6.3 Recalibración

La distribución de la flota no es constante. Al crecer el número de conductores y de arriendos, los percentiles se desplazan: lo que hoy es p90 puede ser p75 dentro de seis meses. Los umbrales de perfilado deben **recalibrarse periódicamente** —como regla, cada vez que el volumen de datos se duplica, o mensualmente si el ritmo es estable. La infraestructura para esto está parcialmente en los notebooks de calibración y se consolidaría en un comando `vib-eda calibrate` (pendiente, ver [docs/STATUS-PLAN.md](docs/STATUS-PLAN.md)).

---

## 7. Ruta principal

Todo lo anterior se reduce a una tubería:

```
señal a 5 Hz (LIS3DHTR ODR 10 Hz + thinning 200 ms)  ✓ implementado
        │  + metadata de sesión (sidecar .meta.json) ✓ implementado
        ▼
señal en movimiento
        │  ventaneo deslizante 5 min (primario)       (pendiente)
        ▼
distribución de features por ventana
        │  percentiles intra-conductor (p50, p90, p95, IQR)
        ▼
vector de perfil por trayecto                          ✓ parcial
        │  agregación diaria / arriendo               (pendiente)
        ▼
perfil multi-escala del conductor
        │  normalización contra vehículo              (pendiente)
        │  posicionamiento contra flota                (pendiente)
        ▼
clasificación percentílica defendible
```

Aunque en cada eslabón hay alternativas razonables —otra ventana, otra métrica, otro percentil—, el esqueleto es único: señal a 5 Hz de origen, ventaneo, percentiles intra, agregación acumulativa, normalización, posicionamiento inter.

### Prioridades de trabajo

1. **Cerrar las métricas Tier 2** ya planificadas: `ax_p95_moving`, `ay_p95_moving`, `mean_jerk_intensity`, `mean_lateral_intensity`, `ax_kurtosis_moving`. Son extensiones pequeñas de [src/eda/profile/profile.py](src/eda/profile/profile.py) y multiplican el contenido informativo del vector de perfil.
2. **Implementar el ventaneo deslizante de 5 minutos** como escala primaria, con 1 y 10 min como opcionales para validación. Todo el resto del pipeline se apoya en esto.
3. **Agregación diaria y multi-día**: requiere `day_id` y `rental_id` en metadata, y un agregador que consuma ventanas/trayectos. Sin flota mínima (≥10 conductores, ≥10 arriendos) el posicionamiento percentílico es inestable; hasta ese volumen, trabajar intra-conductor es suficiente.
4. **Línea base del vehículo** y normalización cruzada. Puede hacerse sencillo: un JSON por `vehicle_id` con la distribución de sus métricas bajo uso promedio.
5. **Recalibración periódica** de los percentiles de flota.

---

## 8. Limitaciones

- **Sin GPS, la velocidad es desconocida.** Una frenada de 0.3 g a 30 km/h y a 120 km/h significan cosas distintas, y hoy no se distinguen. Todo el perfilado es relativo, no absoluto. El A7670 trae GNSS integrado; incorporarlo en firmware es la siguiente capa prevista tras la metadata de sesión.
- **El muestreo a 5 Hz deja fuera la detección de BACHE** por residual de alta frecuencia (el fenómeno vive por encima de Nyquist = 2.5 Hz) y limita la resolución de transientes cortos (<400 ms) — el jerk fino se promedia. Las tasas, el RMS y la intensidad por evento siguen siendo válidas; la familia "pavimento" no lo es con el hardware actual.
- **El ventaneo introduce una restricción mínima**: una ventana debe tener suficientes muestras para que los percentiles sean estables. A 5 Hz, 5 minutos son 1 500 muestras —suficiente—; 1 minuto son 300 —frágil para p95 y superiores—; por debajo de 30 s (150 muestras) la inferencia estadística se vuelve ruidosa.
- **Sin RTC, los timestamps son relativos al arranque.** El sidecar `.meta.json` deja el campo `started_at_iso` en `null` hasta que se integre la hora de red del A7670 (o un RTC dedicado). La agregación multi-día (§4.3–4.4) requiere correlación externa en el ingest hasta ese punto.
- **La metadata es indispensable para estratificar.** Sin `driver_id`, `vehicle_id` y `time_of_day` (capturados desde la iteración actual en el sidecar JSON por sesión), la comparación inter-conductor mezcla efectos de persona, vehículo y horario. La calidad del perfilado depende directamente de la disciplina de anotación en `/session.json`.
- **Perfil no es scoring de riesgo.** Este documento caracteriza; no asigna probabilidad de siniestro. El paso a scoring requiere etiquetas externas —accidentes, partes, reclamos— que hoy no existen en el sistema. El perfilado percentílico es la base descriptiva sobre la que después se monta ese modelo, no su sustituto.
