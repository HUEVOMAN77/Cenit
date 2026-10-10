# Cenit 0.6.30/0.6.31 — CONTRATO de la muestra unificada y del estado del sintonizador (Fases 0/1 del prompt maestro)

Este archivo es la INTERFAZ entre el lado nativo y el lado Java. Ambos equipos la
implementan LITERAL. Nadie cambia un índice, un tipo o un nombre sin actualizar
este archivo primero.

## Por qué double[] y no una struct JNI con field IDs

- Un cruce JNI por tick en vez de doce (`getGPUUsage`, `getEEThreadUsage`, ...):
  las doce lecturas sueltas pueden saltarse el borde de la ventana de 0,5 s y
  mezclar dos intervalos. Un solo array es un SNAPSHOT de un mismo instante de
  `PerformanceMetrics::Update`. Eso ES el requisito "mismo intervalo temporal"
  del prompt, y es la razón de existir de esta estructura.
- `double` preserva exactos los contadores de 64 bits (hasta 2^53: sobra para
  llamadas MTVU) y no obliga a dos arrays.
- Sin `FindClass`/`GetFieldID` por llamada: cero riesgo de excepción JNI y el
  arnés de Java puede fabricar muestras sintéticas sin nativo.

## Firma

```java
// NativeApp.java
public static native double[] getCenitSample();
public static double[] safeGetCenitSample() { // patron del resto de safeGet*:
    // if (hasNoNativeBinary) return null; try { return getCenitSample(); }
    // catch (Throwable t) { return null; }   -> null = "no hay muestra", NUNCA ceros
}
```

```cpp
// native-lib.cpp
jdoubleArray Java_com_izzy2lost_psx2_NativeApp_getCenitSample(JNIEnv* env, jclass);
```

Longitud fija: **24**. Si algún día se agregan campos, se agregan AL FINAL y la
constante `CenitLoadSample.SIZE` sube; nunca se reordena un índice existente.

## Índices (nombre Java en CenitLoadSample, unidad, fuente exacta del core)

| # | constante Java           | unidad                        | fuente                                                                 |
|---|-------------------------|-------------------------------|------------------------------------------------------------------------|
| 0 | IDX_FPS                 | cuadros/s                     | `PerformanceMetrics::GetFPS()`                                         |
| 1 | IDX_SPEED_PCT           | % (100 = a tiempo)            | `PerformanceMetrics::GetSpeed()`                                       |
| 2 | IDX_FRAMETIME_AVG_MS    | ms                            | `GetAverageFrameTime()`                                                |
| 3 | IDX_FRAMETIME_MIN_MS    | ms                            | `GetMinimumFrameTime()`                                                |
| 4 | IDX_FRAMETIME_MAX_MS    | ms                            | `GetMaximumFrameTime()`                                                |
| 5 | IDX_FRAMETIME_P50_MS    | ms                            | percentil sobre `s_frame_time_history` (ver abajo)                     |
| 6 | IDX_FRAMETIME_P95_MS    | ms                            | idem                                                                     |
| 7 | IDX_GPU_PCT             | % del RELOJ REAL (0..100)     | `GetGPUUsage()` — NO es una fracción                                   |
| 8 | IDX_GPU_AVG_MS          | ms/cuadro, puede NaN          | `GetGPUAverageTime()`                                                  |
| 9 | IDX_EE_PCT              | % del reloj real              | `GetCPUThreadUsage()`                                                  |
| 10| IDX_EE_AVG_MS           | ms/cuadro                     | `GetCPUThreadAverageTime()` (NO `GetCPUThreadTime`)                   |
| 11| IDX_GS_PCT              | % del reloj real              | `GetGSThreadUsage()`                                                   |
| 12| IDX_VU_THREAD_PCT       | % del reloj real              | `GetVUThreadUsage()` (0 si MTVU apagado)                               |
| 13| IDX_MTVU_WAIT_MS        | ms acumulados en la ventana   | `GetMtvuSyncStats().wait_ms`                                           |
| 14| IDX_MTVU_WAIT_CALLS     | llamadas                      | `.wait_calls`                                                          |
| 15| IDX_MTVU_EXEC_MS        | ms                            | `.exec_ms`                                                             |
| 16| IDX_VERTICAL_HZ         | Hz VERTICALES del modo de vídeo (NO son fps del juego) | `GetVerticalFrequency()`, saneada a 0 si no está en (5,240) |
| 17| IDX_PRESENTS            | presents en la ventana        | `GetPresentsInWindow()` — ventana CERRADA, no el contador vivo (ver abajo) |
| 18| IDX_WINDOW_S            | segundos de la ventana medida | `GetLastWindowSeconds()`                                               |
| 19| IDX_UPSCALE             | multiplicador efectivo        | `EmuConfig.GS.UpscaleMultiplier`                                       |
| 20| IDX_EE_CYCLE_RATE       | entero como double            | el que ya usa `getEffectiveEECycleRate`                                |
| 21| IDX_EE_CYCLE_SKIP       | entero como double            | el que ya usa `getEffectiveEECycleSkip`                                |
| 22| IDX_MTVU_ON             | 1.0 / 0.0                     | el que ya usa `getEffectiveMTVU`                                       |
| 23| IDX_FLAGS               | bits (ver abajo)              | calculado nativo                                                       |

## Los DOS intervalos distintos (esto no es detalle, es el corazón de la Fase 1.1)

El prompt exige que GPU y emulación pertenezcan al mismo intervalo. Se verificó
contra el core y hay que decirlo con precisión:

- **Ventana = 0,5 s** (`UPDATE_INTERVAL`, PerformanceMetrics.cpp:21). Los campos
  0..15 y 17..18 salen TODOS del mismo `Update()` (líneas 224-236 y 306-325):
  GPU%, EE%, GS%, VU%, speed, frametime avg/min/max, MTVU. Son del mismo
  intervalo por construcción. Ese es el valor real del snapshot: leerlos sueltos
  desde Java puede saltarse el borde de ventana y mezclar dos intervalos.
- **Anillo de frametimes = 150 cuadros** (`s_frame_time_history`, se escribe en
  PerformanceMetrics.cpp:210 en CADA present, y se vacía con `fill(0)` solo en
  `Clear()` L145, NO en `Reset()`). Los campos 5 y 6 (P50/P95) NO son de la
  ventana de 0,5 s: son de ~2,5 s a 60 fps y de ~5 s a 30 fps. Se exponen como
  muestra de otro intervalo y se etiquetan asi en el nombre (`IDX_FRAMETIME_P50_MS`
  con comentario "anillo de 150 presents, NO la ventana"), y el lado Java los usa
  para TENDENCIA (el P95 del anillo contra el promedio de la ventana: si el P95
  sube mientras la ventana está tranquila, es degrado termico o de paginacion),
  NO para decidir GPU_BOUND en el mismo tick que la carga instantanea.
- Consecuencia medida y documentada: `s_average_gpu_time` (campo 8) divide el
  MISMO acumulador entre `s_unskipped_frames_since_last_update`, asi que con
  cero presents llega NaN. Por eso existe el campo 17: `GPU_METRIC_VALID` se
  calcula con presents>0, no con el porcentaje.
- Trap corregido en la implementación: `s_presents_since_last_update` se pone a
  cero al cerrar la ventana, así que leerlo "vivo" desde JNI devuelve la cuenta de
  la ventana que RECIÉN EMPIEZA (casi siempre 0). Por eso `GetPresentsInWindow()`
  devuelve el valor capturado en `Update()` ANTES del cero, y `Clear()` lo resetea
  junto con el resto de escalares de ventana — un VM nuevo no debe publicar nunca
  la ventana del juego anterior.

## Bits de IDX_FLAGS — validez, NO magnitud (Regla 3 del prompt)

```
bit 0 (1) GPU_METRIC_VALID : hay GPU_reportada. 0 significa "el driver no dio
       tiempo de GPU en esta ventana" (presents==0 o acumulador intacto).
       OJO: GPU al 3% con bit=1 es "GPU realmente holgada"; GPU al 0% con
       bit=0 es "no sabemos". Distinguirlos es TODO el punto de la Regla 3.
bit 1 (2) EMU_METRIC_VALID : GetCPUThreadUsage() tiene denominador de ventana
       valido (ticks_delta > 0 y GetThreadTicksPerSecond() > 0).
bit 2 (4) WINDOW_COMPLETE  : la ventana duró >= 0.25*UPDATE_INTERVAL. Un
       Update() cortísimo (primer tick, reset de stats) no es evidencia.
bit 3 (8) SAMPLE_FRESH     : el snapshot es de un Update() posterior al último
       ResetStatistics/Clear (arranque de VM, cambio de juego).
```

## Percentiles P50/P95 de frametime

 Fuente: `s_frame_time_history` (`PerformanceMetrics.h:67-68`,
 `NUM_FRAME_TIME_SAMPLES = 150`, escribe `s_frame_time_history_pos`).
 Implementación nativa: copiar las muestras **no nulas** a un buffer temporal,
 ordenar por selección e insertar el índice del percentil por posición
 (nearest-rank: `ceil(p*n)-1`, con n>=1). Con n < 20 muestras el percentil NO se
 calcula: escribir NaN en IDX_FRAMETIME_P50_MS/P95 y dejar el resto intacto.
 Motivo para el comentario del código: un P95 sobre 4 cuadros es ruido, y el
 clasificador trata NaN como "sin percentil" (confianza baja), no como cuadro
 barato.
 **Verificado** (0.6.30): `s_frame_time_history` SI se alimenta en la ruta de
 Android — la escritura esta en `PerformanceMetrics::Update()` linea ~210, en el
 mismo sitio donde se cuenta el present, y Update() se llama desde el GS thread
 con el VM corriendo. Lo que NO hace el core es limpiar el anillo en `Reset()`:
 solo `Clear()` (arranque/cambio de juego) lo vacia, y por eso el percentil se
 calcula sobre las muestras NO NULAS.

## Clasificación publicada por Java al HUD (Fase 1.8)

```java
public static native void setCenitLoadVerdict(int stateOrdinal, int confidencePct,
    int actionCode, double gpuPct, double eePct, int thermalStatus);
```
```cpp
// native-lib.cpp: publica en CenitTelemetry::g_load_verdict (header-only, atomics
// inline) y ya. No toca nada de emulacion.
```

Y su contraparte de limpieza, que tambien cruza JNI:

```java
public static native void resetCenitLoadVerdict(); // sin argumentos
```
```cpp
// native-lib.cpp -> CenitTelemetry::ResetLoadVerdict(): estado a UNKNOWN,
// confidence a -1 (con la guarda `confidencePct >= 0` la linea del HUD
// desaparece, que es lo correcto: "todavia no hay veredicto").
```

El regidor lo llama en `start()` Y en `stop()`. No lo llama el nucleo: el nucleo no
sabe cuando cambia "la partida" para el clasificador, y el regidor si. Sin este
llamante, `ResetLoadVerdict()` seria un adorno que compila y nunca corre, y al
volver al menu y entrar en otro disco quedaria pintado el LOAD del juego anterior
durante los primeros ticks — el numero viejo que hace confiar en una decision que
no se tomo en esta partida. `Harness7` exige la cadena completa (header -> JNI ->
`safeResetCenitLoadVerdict` -> dos llamadas en el regidor) porque cualquiera de los
eslabones se puede perder por separado.

Nota de diseño: el almacen NO lleva contador de publicacion. Hubo un `published_seq`
cuyo unico uso propuesto era "saber si es de esta partida", y para eso ya estan
`confidence_pct < 0` (no hay veredicto) y el reset de arriba. Un campo que nadie lee
es una decision que nadie tomo.

El sexto parametro — thermalStatus — existe porque el estado THERMAL_BOUND se
decide con el numero que solo Java conoce (`PowerManager.getCurrentThermalStatus`,
por reflexion). Sin el, el HUD pintaria un veredicto que nadie puede auditar. Sus
convenciones: -1 = sin senal (no es "0 = nominal"), -2 = el telefono no expone la
API. Se pinta como `-` en vez de -1 porque un "-1" suelto se lee como temperatura
negativa.

Por que el almacen vive en `CenitTelemetry.h` y no en `native-lib.cpp`: declararlos
`static` (o en un `namespace {}`) ahi y `extern` en ImGuiOverlays.cpp compila y
revienta en el LINK — enlace interno. Con un `inline` object en un header, el
enlace es correcto por construccion.

`ImGuiOverlays.cpp` imprime UNA línea nueva junto a la de VUSB, solo si
`confidencePct >= 0`:
```
"LOAD: %s CONF: %d%% GPU: %.0f%% EMU: %.0f%% ACTION: %s THERM:%d"
```
con `%s` de estado del diccionario {0=UNKNOWN,1=NO_BOTTLENECK,2=GPU_BOUND,
3=CPU_BOUND,4=VU_BOUND,5=THERMAL_BOUND,6=INVALID_SAMPLE} y ACTION
{0=HOLD,1=ALLOW_RESOLUTION_DROP,2=BLOCK_RESOLUTION_DROP,3=WAIT_FOR_EVIDENCE}.

## Los NUMEROS de estado tienen un solo dueño (0.6.30)

El entero que representa cada estado vive en TRES lados: el HUD C++ (`kStates[]`),
el clasificador Java (`LoadState.ordinalForHud()`) y las preferencias del perfil
por-juego (`AdaptiveProfile.loadMix`). Para que no puedan discrepar, el diccionario
esta en `CenitTelemetry.java` (`STATE_*`) y:

- Java lo usa como indice del tally y el HUD lo traduce con su propia tabla;
- el numero se escribe A MANO en los dos lados (nunca `ordinal()`), porque reordenar
  la enum cambiaria en silencio el significado de numeros viejos — en el HUD y, peor,
  en las prefs de un telefono donde ya hay aprendizaje guardado;
- `Harness7` (arnes de contrato) comprueba que las tres tablas dicen lo mismo y que
  la del C++ sigue igual que el contrato. Es el unico sitio donde un error de copiado
  se ve sin encender el telefono.

El tally de 0.6.29 tenia 4 casillas en orden `gpu,emu,vu,sin`. El de 0.6.30 tiene 7
en el orden del contrato (`unk,nog,gpu,cpu,vu,term,inv`), y `AdaptiveProfile`
reconoce ambos: un perfil viejo se traduce, no se malinterpreta.


## Estados y semantica (Fase 1.2) — los define Java, no el nativo

```
UNKNOWN_LOAD, NO_BOTTLENECK, GPU_BOUND, CPU_BOUND, VU_BOUND, THERMAL_BOUND, INVALID_SAMPLE
```

## Sintonizador CPU/VU: estado publicado por Java al HUD (0.6.31, Fase 1)

Mismo patrón que `setCenitLoadVerdict`: **Java mide y publica, el C++ solo lee y
pinta.** El nativo no escribe nunca `g_tuner_status`; si lo hiciera, habría dos dueños
del mismo cuadro y el HUD podría mostrar un estado que nadie decidió.

```java
// NativeApp.java
public static native void setCenitTunerStatus(int state, int mode, int gainPermille,
    int fpsBeforeX100, int fpsAfterX100, int p50DeciMs, int p95DeciMs,
    int thermalStatus, long profileHash);
public static native void resetCenitTunerStatus(); // sin argumentos
```

```cpp
// native-lib.cpp -> CenitTelemetry::PublishTunerStatus(...) / ResetTunerStatus()
```

El wrapper `safeSetCenitTunerStatus(...)` recibe `float p50Ms, float p95Ms` y `String
profileId` y **convierte aquí, no en C++**, por tres motivos escritos en el código:
la firma nativa queda puramente de enteros (nada de `std::string` entre TUs con hilos
no sincronizados), no viaja un `double` por campo, y un `NaN` no puede llegar nunca a
un `%f` del HUD. Las conversiones son:

| campo | escala | centinela |
|---|---|---|
| `gainPermille` | milésimas de **ratio**: +5 % → `50`, +10 % → `100` | `0` = sin ganancia medida |
| `fpsBeforeX100` / `fpsAfterX100` | fps × 100 | `-1` = "no medido" → el HUD pinta `-` |
| `p50DeciMs` / `p95DeciMs` | ms × 10 | `-1` (también lo pone el wrapper si el float era NaN o < 0) |
| `thermalStatus` | escalón de `PowerManager` | `-1` = sin señal, `-2` = la API no existe |
| `profileHash` | FNV-1a 64 bits de la identidad | `0` = sin identidad → el HUD pinta `sin-id` |

El `0` importa y por eso se filtra: `mBaseAgg` arranca en 0 y publicar "0.0 fps de
base" daría al HUD un número inventado justo en los estados donde aún no se midió
nada (`OFF`, `IDLE`, `LEARNING`). Es la Regla 3 aplicada al cuadro.

### Diccionario de estados (`CpuVUTuner.ST_*` ↔ `kTunerStates[]`)

Doce casillas, **escritas a mano en los dos lados** (nunca `ordinal()`), en este orden
exacto:

```
0=OFF  1=MANUAL  2=IDLE  3=LEARNING  4=MEASURING_BASELINE  5=MEASURING_CANDIDATE
6=VERIFYING_ADOPTION  7=ADOPTED  8=REJECTED  9=SUSPENDED  10=POST_VERIFY  11=RECOVERED
```

### Diccionario de modos (`CpuVUTuner.MODE_*` ↔ `kTunerModes[]` ↔ `arrays.xml`)

```
0=MODE_OFF              "OFF"            spinner: "Desactivado"
1=MODE_MANUAL           "MANUAL"         spinner: "Manual (solo mira, no toca)"
2=MODE_AUTO_SAFE        "AUTO_SAFE"      spinner: "Automático seguro (predeterminado)"
3=MODE_AUTO_MTVU_ONLY   "AUTO_MTVU"      spinner: "Automático sólo MTVU"
4=MODE_AUTO_NO_RATE_SKIP "AUTO_SAFE_sinRS" spinner: "Automático sin Rate/Skip"
```

**La posición del spinner ES el valor**: `arrays.xml` está ordenado por ordinal y el
`onItemSelected` escribe su índice en `cpu_tuner_mode` (int). Agregar un modo es
agregarlo al final de los tres sitios, en el mismo commit.

`ResetTunerStatus()` lo llama el regidor en `start()` y en `stop()` (después de
`tuner.onGameStop()`), igual que con el veredicto de carga: sin ese llamante, al
volver al menú y entrar en otro disco quedaría pintado el `TUNER:` del juego anterior.

### Quién verifica todo esto, sin teléfono

`tools/arneses/baseline.sh` corre los cinco arneses (h5–h9) y dos verificadores estáticos:

- `jnisig.py` — cada `native` de `NativeApp.java` tiene su `Java_...()` en el C++ con
  el **mismo número de argumentos**. Un desajuste no rompe CI (los símbolos JNI se
  resuelven por nombre en runtime): revienta en el teléfono como
  `UnsatisfiedLinkError`. Este es el único sitio donde se ve antes.
- `jnidict.py` — las tres tablas de arriba dicen lo mismo en Java, en el HUD C++ y en
  `arrays.xml`, y los guardas de rango del C++ coinciden con el tamaño real de sus
  tablas. En 0.6.31 cazó un desajuste real: el HUD decía `AUTO_noRS` mientras el log y
  Ajustes decían `AUTO_SAFE_sinRS` para el mismo modo 4.
- `Harness8` — compila y ejecuta `CpuVUTuner.java` y `TunerProfileStore.java` **del
  repo**. Ninguno de los arneses anteriores los compilaba (`Harness6` sustituye el
  tuner por un stub), así que hasta 0.6.31 "el tuner compila" no estaba cubierto por
  ninguna línea base.

## Máquina de superbloques VU1: contrato de estados y HUD (0.6.32, Fase 2)

El dueño de los números es `pcsx2/arm64/MvuSbLife.h` (header puro, sin
dependencias de PCSX2: por eso lo compila g++ en host). Nadie más define un
estado, un umbral o una etiqueta.

### Diccionario de estados (`mVUSbLife::State` ↔ `StateName` ↔ HUD)

| Ordinal | Estado | `StateName` | Etiqueta HUD | ¿sirve la variante? |
|---|---|---|---|---|
| 0 | `Unseen` | `UNSEEN` | `VU-SB: UNSEEN` | no |
| 1 | `Building` | `BUILDING` | `VU-SB: BUILDING` | no (compilación en vuelo) |
| 2 | `Validating` | `VALIDATE` | `VU-SB: VALIDATE` | sí, alternada (forma los pares) |
| 3 | `Shadowing` | `SHADOW` | `VU-SB: SHADOW` / `.../TRIAL` | no (autoritativo = cadena normal) |
| 4 | `Validated` | `VALIDATED` | `VU-SB: VALIDATED` / `.../TRIAL` | solo en ventana de prueba declarada |
| 5 | `Trusted` | `TRUSTED` | `VU-SB: TRUSTED` / `TRUSTED/SPOT` | sí, con vigilancia periódica |
| 6 | `Dead` | `DEAD` | `VU-SB: DEAD` | no (castigo del candidato, no del proceso) |
| 7 | `Invalidated` | `INVALIDATED` | `VU-SB: INVALIDATED` | no (evidence archivada, hueco reasignable) |

Sin candidatos y motor encendido: `VU-SB: ON/SIN CANDIDATO`. Con el breaker del
proceso disparado: `VU-SB: PARADO`. Con el motor apagado la línea no se pinta.

Reglas del HUD, codificadas en la propia etiqueta y no delegadas al llamador:

- `VALIDATE` y `SHADOW` **no pueden** decir «acelerado», «turbo» ni `TRUSTED`.
  Un candidato en esas fases no ha entregado todavía ningún número de ganancia.
- La ventana de prueba y la vigilancia se dicen **junto** al estado
  (`SHADOW/TRIAL`, `VALIDATED/TRIAL`, `TRUSTED/SPOT`), nunca en lugar de él.
- Los recuentos del HUD son los del candidato más adelantado (`pares=N/1000`,
  `ventana=K`), no los acumulados del proceso. El acumulado completo vive en
  `logs/vu_superblock.txt`.

### Unidades del puente (Java → C++)

| Campo | Canal | Unidad | Centinela |
|---|---|---|---|
| `profileHash` | `NativeApp.setCenitTunerStatus` (jlong) → `mVUSuperblock::SetProfileHash` | FNV-1a 64 bits de juego+dispositivo | `0` = sin identidad ⇒ identidad incompleta ⇒ **sin promoción** |
| `g_engineAbi` | registrado en `mVUinit` | `kMvuCompilerAbiVersion` (entero) | `0` = nadie registró ⇒ identidad incompleta |
| `g_exitMask` | registrado en `mVUinit` | bits `Regions::` (14) | máscara incompleta ⇒ la máquina **rechaza cada par** y lo cuenta |
| `g_rollbackSurface` | escrito en `mVUinit` | bool | `false` hoy ⇒ ningún candidato entra en `SHADOW` |
| `episodeNs` | `recMicroVU1::Execute` → `SbFinish` | nanoseconds, `Common::Timer` (monotónico) | `0` = no cronometrado ⇒ **no cuenta como par medido** |

### Umbrales de promoción (los define `MvuSbLife.h`, el motor los obedece)

- `kValidPairs = 1000` pares sin divergencia y `kValidWindows = 2` ventanas que
  aportaron pares ⇒ `VALIDATED`.
- `TRUSTED` añade: identidad **completa**, ganancia medida ≥ +3 % (criterio
  propio, no del prompt), `timedPairs ≥ 64`, y P95 de la variante no peor que el
  de la cadena normal (delta en **microsegundos** — en milisegundos el veto no
  se activaría nunca a escala de VU1).
- `kBudgetEpisodes = 60000` episodios por candidato: presupuesto de *trabajo*,
  no de pares.
- `kTrustedSpotPeriod = 4096` despachos: vigilancia de un `TRUSTED`.
- Breaker de proceso: 3 divergencias sin superficie de rollback; con superficie,
  8 repartidas en ≥ 3 candidatos distintos.

### Quién verifica esto, sin teléfono

- `Harness9` (`tools/arneses/h9-src/`): 227 comprobaciones sobre la máquina.
- `jnidict.py`: que la enum no tenga huecos ni cambie de orden, que ninguna
  etiqueta prometa velocidad, que `g_exitMask` declare exactamente
  `kRequiredRegions` y que **cada región declarada esté realmente hashkeada**
  campo por campo en `mVU1ExitDigest`, y que `HudSummary` tenga tantos
  especificadores como argumentos.

### Lo que este contrato NO afirma

No hay ni un número de ganancia de superbloque medido en hardware. La máquina
está verificada; **el rendimiento no**. El motor sigue apagado por defecto.

## Reglas de oro para ambos equipos

1. Ninguna magnitud se convierte de %. Todo lo que sale del core en % se queda
   en % (0..100). Comparar % contra fracciones fue el bug de 0.6.4.
2. NaN no es cero. Toda lectura se propaga como NaN si el core la da como NaN, y
   el lado Java la trata con `!(x > 0)` (que sí captura NaN), nunca `x <= 0`.
3. El camino viejo (getGPUUsage, getEEThreadUsage, getMtvuWaitMs, ...) NO se
   borra en esta versión: el governor puede migrar al snapshot pero los getters
   siguen expuestos porque el HUD y los reportes los usan. Se eliminan cuando no
   queden consumidores, y eso se comprueba con grep, no por intención.
4. No se toca: semantica de instrucciones VU, orden de memoria, sincronia
   EE/VU1, EECycleRate/EECycleSkip, estados MTVU, reglas de invalidacion del
   recompilador.
