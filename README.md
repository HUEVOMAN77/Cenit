# CENIT — Emulador de PlayStation 2 para Android

[![Licencia: GPL v3](https://img.shields.io/badge/Licencia-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Android](https://img.shields.io/badge/Android-8.0%2B-green.svg)](https://developer.android.com/)
[![ARM64](https://img.shields.io/badge/Arquitectura-arm64--v8a-orange.svg)](https://developer.arm.com/)
[![Última versión](https://img.shields.io/badge/versión-0.6.6-informational)](https://github.com/HUEVOMAN77/Cenit/releases/tag/base-0.6.6)

**Cenit** es un emulador de PlayStation 2 para teléfonos Android (arm64) construido como fork independiente de [PSX2](https://github.com/izzy2lost/PSX2) —el port Android de PCSX2 2.7— mantenido por una sola persona, pieza a pieza, con un objetivo concreto: **que los juegos de PS2 corran fluidos en gama baja y media, donde ningún otro port se preocupa por mirar.**

Este proyecto no busca ser una copia con logo distinto. Cada versión añade capas de optimización y de experiencia de uso que no existen en PSX2 ni en PCSX2, medidas sobre hardware real de teléfono.

---

## Ficha rápida

| | |
|---|---|
| **Versión actual** | 0.6.6 (versionCode 42) |
| **Descargas** | [Releases](https://github.com/HUEVOMAN77/Cenit/releases) — `Cenit-<versión>.apk` |
| **Requisitos** | Android 8.0+ (minSdk 26), procesador arm64, tu propia BIOS de PS2 |
| **Qué NO incluye** | BIOS, juegos, ni ningún contenido con copyright |
| **Firma** | Clave de debug (uso personal; no es una APK de tienda) |
| **Licencia** | GPL-3.0, como el PCSX2 del que desciende |

---

## Por qué existe Cenit

PCSX2 es un emulator excepcional diseñado para PCs. Su port de Android (PSX2) hereda de golpe supuestos que en un teléfono no valen: hilos que el sistema operativo del teléfono mueve a núcleos economizados a mitad de frame, readbacks síncronos que traban el pipeline, precargas de texturas que se comen la RAM compartida, y ajustes globales que el propio núcleo borra sin avisar.

En un gama alta esas torpezas se perdonan porque sobra potencia. En un gama media o baja son exactamente la diferencia entre 25 fps con tirones y 50 estables. Cenit ataca esa brecha: **detecta el hardware, aprende de cada juego y adapta el motor solo**, sin que el usuario tenga que entender qué es un VsyncQueueSize.

Y lo hace en público: todo el código está aquí, se compila en GitHub Actions ante cualquiera, y cada release explica qué cambió y por qué.

---

## Qué lleva dentro (historial por versión)

### Identidad e interfaz (0.2.0 → 0.6.2)

- **Pantalla de inicio propia** (0.2.0, rediseñada en 0.5.0): cabecera con marca, biblioteca en rejilla de 4 columnas con carátulas automáticas de cajas de PS2, buscador en vivo, barra inferior Inicio · Biblioteca · Carpetas · Ajustes. Los mandos táctiles solo aparecen con un juego en marcha.
- **Marca Cenit completa** (0.3.0): cortinilla de intro animada, nombre oficial en el launcher, ícono propio.
- **Onboarding de 3 pasos** (0.4.0): BIOS, carpetas y configuración inicial con tarjetas de progreso; nunca deja al usuario atrapado (arreglado en 0.1.1).
- **Ajustes a pantalla completa** en clave neón (0.4.0/0.6.2), agrupados por secciones.
- **Mandos en pantalla rediseñados** (0.6.1): estilo fantasma transparente, hombros L3/L2/L1 y R2/R1/R3 arriba, cruceta en rombo, botones con símbolos PS2.

### Motor de rendimiento (0.6.0 → 0.6.6)

- **0.6.0 — Perfil de rendimiento por hardware.** Detección de SoC (incluidos apodos de Qualcomm para ROMs que no reportan `ro.soc.model`) y aplicación de un baseline por gama: speedhacks seguros (IntcStat, WaitLoop, vuFlagHack, vu1Instant, fastCDVD), recompilación completa con fastmem, sin spin en readbacks de GPU, salto de frames duplicados y logs silenciados.
- **0.6.3 — Resolución dinámica (regidor propio).** Mide la velocidad real de emulación cada segundo; si el juego se atrasa, baja un paso de resolución por la vía oficial (INI + ApplySettings) y lo devuelve cuando afloja. Respeta la escala del usuario como techo, se congela con mando de velocidad o pausa, y se rinde si los ajustes por juego mandan sobre él.
- **0.6.4 — Hack de hardware POR JUEGO de verdad.** El medio píxel se guardaba en el INI global que MaskUserHacks borraba en cada ApplySettings: una función fantasma. Ahora vive en `gamesettings/<SERIAL>_CRC.ini` y, antes de activar el modo manual de un juego, **siembra sus fixes automáticos del GameDB** para que ninguno se pierda. Se aplica en caliente (ReloadGameSettings en el hilo de emulación). Mismo release añade: modo de descarga GPU *Unsynchronized* en gama baja, compresión de estados zstd-Low en gama baja, y **respaldo automático Vulkan↔OpenGL** si el renderizador Auto no logra abrir el dispositivo (con aviso en pantalla). El regidor sube a v2: deja de recortar resolución cuando el cuello de botella es la CPU emulada (usa el uso real de GPU como testigo) y obedece los límites térmicos del sistema.
- **0.6.5 — Cuatro funciones inventadas aquí** (no existen en PSX2 ni en PCSX2):
  1. **Memoria por juego**: Cenit recuerda qué escala sostuvo cada juego en *tu* teléfono; la próxima sesión arranca en ella en vez de pelear desde el máximo, y un juego visto como CPU-bound no vuelve a recibir recortes inútiles. (Se borra manteniendo pulsado el interruptor de memoria.)
  2. **Pre-corte térmico**: detecta la deriva silenciosa de velocidad propia del throttling que no avisa, y baja un paso *antes* del tirón. Máximo dos por partida.
  3. **Turbo en pantallas de carga**: reconoce la carga (GPU muerta + velocidad clavada), engancha el limitador Turbo del núcleo esos segundos y lo suelta solo, con corte de seguridad.
  4. **Cuotas de EE con evidencia**: si el juego se quedó clavado en 1x, la pantalla de Ajustes te lo dice con los segundos que midió y te sugiere probar; si no, te recomienda dejarlo en Normal. Se guarda solo en el juego delante y aplica al instante.
- **0.6.6 — Tres bloques de rendimiento fino**, todos regulables en Ajustes → Rendimiento:
  1. **Fijar emulación al núcleo rápido**: el reparto de hilos EE/VU/GS ya existía en el núcleo pero condicionada a que cpuinfo reportara varios clústeres; ahora se garantiza y se puede apagar para diagnosticar.
  2. **Cola de cuadros**: ritmo óptimo (cero cuadros por delante, menos input lag) en gama media/alta; 2 cuadros de amortiguación en gama baja, donde quitarla costaría fps.
  3. **Precarga de texturas por gama**: *Parcial* en gama baja (cientos de MB de RAM compartida libres, menos micro-cortes al entrar a zonas nuevas); *Completa* arriba; el GameDB sigue mandando cuando un juego lo exige.

### Arquitectura de estas capas

- **Java (capa propia)**: `DynamicResolutionGovernor` (v3: memoria + pre-corte + turbo + evidencia, un tick por segundo en el hilo principal), `AdaptiveProfile` (aprendizaje persistido por URI en las prefs de la app), la pantalla de Ajustes y todo el onboarding.
- **JNI (`native-lib.cpp`)**: setters que aplican en caliente respetando el ciclo del núcleo (escribir INI → `ApplySettings` → `MTGS::ApplySettings`), capa de escritura por-juego con siembra de GameDB, y métricas expuestas (`getEmulationSpeed`, `getGPUUsage`, `getGPUAverageTime`).
- **Núcleo PCSX2 intacto en su lógica**: ninguna función de Cenit parchea el bucle de dibujo del GS ni la recompilería; todo entra por las vías oficiales de configuración del propio emulador, que es lo que hace estas mejoras seguras de actualizar y de deshacer.

---

## Cómo usar

1. Descarga `Cenit-0.6.6.apk` desde [Releases](https://github.com/HUEVOMAN77/Cenit/releases) e instálala (permitir "instalar apps desconocidas" si es tu primera vez).
2. Al abrir, el asistente te guía: coloca tu **BIOS legal** (extraída de tu propia PS2) en la carpeta que indica, y elige una carpeta de juegos (ISO `.bin/.iso/.chd/.gz`).
3. Toca un juego de la biblioteca. Con el juego abierto, el panel de Ajustes → Rendimiento deja los mandos de Cenit: resolución dinámica, memoria por juego, turbo de cargas, cuotas de EE, fijado de hilos, cola de cuadros y precarga.
4. Recomendado para ver qué está haciendo el motor: activa el HUD de velocidad la primera semana.

## Compilar desde el código

```
CI de GitHub Actions (recomendada):
  Actions → "Build" → build_type: release   (o push a master; genera el artifact)

Local (Linux/Windows):
  Requisitos: Android NDK 28.2.13676358, CMake 3.22+, JDK 17, SDK de Android
  git clone https://github.com/HUEVOMAN77/Cenit && cd Cenit
  ./gradlew assembleRelease
  Salida: app/build/outputs/apk/release/
```

El proyecto completo se compila en `.github/workflows/build.yml` con cada push: si algo roto sube, la pestaña Actions lo muestra en minutos.

## Por qué las APK van firmadas con clave de debug

Cenit es un proyecto personal, sin cuenta de desarrollador ni intención de tienda. La firma de debug es lo honesto para un build de comunidad: la APK se instala directa, y no suplanta a ninguna firma oficial. Si alguna vez eso cambia, se anunciará aquí y en cada release.

## Hoja de ruta

Lo siguiente, en orden de impacto medido:

- **Hacks por-juego finos con UI**: TextureInsideRt (lectura del framebuffer dentro de la GPU — el de mayor rendimiento posible en juegos con agua/reflejos), NativeScaling y SkipDraw. Las tres infraestructuras (capa por-juego + siembra GameDB + reload en caliente) ya existen; falta exponerlas con criterio y con la evidencia del regidor como guía.
- **Base de datos de rendimiento propia**: usar lo que los perfiles de memoria juntan (escala sostenida, cpu-bound, cuotas) para sugerir un preset por juego y, con el tiempo, compartir una tabla de la comunidad.
- **Limpieza del binario**: el build de Android arrastra el recompiler x86 completo que nunca corre (≈2 MB muertos) — sacarlos del CMake de Android.
- **Mejoras térmicas activas**: colaboración con el framework de potencia de Android (ADPF en API 33+) para que el SoC suba frecuencia *antes* del pico en vez de recortarla después.
- **Más allá del governor**: lectura del modo de juego real para no recortar resolución durante FMVs y cortes de escena (ya hay heurística; el paso fino requiere medir en más títulos).

## Créditos y ascendencia

- **[PCSX2](https://github.com/PCSX2/pcsx2)** — el emulador. Todo el mérito del motor es de sus autores.
- **[PCSX2_ARM64](https://github.com/pontos2024/PCSX2_ARM64)** — la recompilería ARM64 nativa sobre la que se apoya el port.
- **[PSX2 (izzy2lost)](https://github.com/izzy2lost/PSX2)** — el port Android del que este repositorio es fork directo.

Cenit es una capa propia (identidad, interfaz, governor de rendimiento, capa por-juego, perfiles de hardware) sobre esa ascendencia, publicada bajo la misma licencia GPL-3.0 y mantenida por una sola persona, poco a poco. Los problemas del motor son del motor; las ideas de este archivo, de este fork.

## Aviso legal

Cenit es un proyecto educativo y de uso personal, sin afiliación con Sony Interactive Entertainment, con el equipo de PCSX2 ni con Google. No distribuye BIOS ni juegos: para usarlo necesitas tu propio hardware de PS2 del que extraer la BIOS y tus propias copias de los juegos.
