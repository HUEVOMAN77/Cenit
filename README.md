# CENIT — Emulador de PlayStation 2 para Android

[![Licencia: GPL v3](https://img.shields.io/badge/Licencia-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Android](https://img.shields.io/badge/Android-8.0%20o%20superior-green.svg)](https://developer.android.com/)
[![ARM64](https://img.shields.io/badge/Procesador-arm64--v8a-orange.svg)](https://developer.arm.com/)
[![Última versión](https://img.shields.io/badge/versión%20actual-0.6.23-informational)](https://github.com/HUEVOMAN77/Cenit/releases/tag/base-0.6.23)
[![Driver](https://img.shields.io/badge/driver%20incluido-Turnip%2025.3.6-yellow)](https://github.com/HUEVOMAN77/Cenit/releases)

**Cenit es mi emulador de PlayStation 2 para Android.** Lo diseño, lo programo, lo compilo y lo mantengo yo solo: [HUEVOMAN77](https://github.com/HUEVOMAN77). Empezó como bifurcación de [PSX2](https://github.com/izzy2lost/PSX2) (el adaptador Android de PCSX2 2.7), pero eso fue el punto de partida, no el proyecto: hoy Cenit es propiedad mía y todo lo que lo define —el motor de rendimiento adaptativo, los ajustes por juego, mi driver Vulkan propio, el guardarraya, la interfaz, la instrumentación del recompilador VU— es trabajo mío, pieza por pieza y en público. Cada decisión de este repositorio se toma aquí.

La meta es muy concreta: **que los juegos de PS2 se muevan fluidos en celulares de gama baja y media, que es justamente donde ningún otro proyecto está mirando.**

No es una copia con otro logo. Cada versión suma mejoras de rendimiento y de uso que no existen ni en PSX2 ni en PCSX2, y —salvo que yo mismo diga lo contrario— medidas sobre hardware real antes de publicarse.

> **Versión actual: 0.6.23.** Mantengo una línea pública y continua (0.6.x), con su release, su verificación en teléfono real y mi registro de qué cambió y qué no. En [Historial versión por versión](#historial-versión-por-versión) está todo el camino recorrido, sin adornos.

---

## Lo esencial

| | |
|---|---|
| **Versión actual** | 0.6.23 |
| **Descarga** | [Lanzamientos](https://github.com/HUEVOMAN77/Cenit/releases) → `Cenit-0.6.23.apk` |
| **Driver incluido** | `Cenit-Turnip-25.3.6` (compilado por mi CI) |
| **Requiere** | Android 8 o superior, procesador de 64 bits y tu propia BIOS de PS2 |
| **No incluye** | BIOS, juegos ni ningún archivo con derechos de autor |
| **Licencia** | GPL-3.0, libre como el PCSX2 del que desciende |

Mi regla permanente: **cada release muestra siempre los dos archivos** —el APK del emulador y el driver más reciente—, sin importar cuál de los dos se haya publicado en ese momento. Así, desde la página que abras puedes instalar el emulador e importar el driver sin buscar más.

---

## Por qué existe Cenit

PCSX2 es un emulador excelente, pero está pensado para computadoras. Al llevarlo a un celular arrastra supuestos que acá no valen: hilos que el sistema del teléfono manda a los núcleos lentos a mitad de partida, esperas de la gráfica que traban todo, cargas de texturas que se comen la memoria compartida y ajustes que el propio motor borra sin avisar.

En un gama alta eso se perdona porque sobra potencia. En un gama media o baja es exactamente la diferencia entre 25 cuadros por segundo con tirones y 50 estables. Yo ataco ese problema: **Cenit reconoce tu hardware, aprende cómo se comporta cada juego en tu teléfono y adapta el motor solo**, sin que tengas que saber qué significa cada ajuste.

---

## Qué hace Cenit hoy

### 1. Motor de rendimiento adaptativo

Es el corazón del proyecto y lo que no existe en ningún otro port.

- **Perfil automático por hardware.** Al instalar, Cenit reconoce tu procesador —incluidos los apodos internos que muchas marcas reportan mal— y deja lista la línea base: aceleradores de CPU seguros, recompilación completa, sin esperas inútiles a la gráfica. Arranca ya configurado para tu gama. Si tu teléfono no está catalogado, se le asigna el perfil más seguro para no prometer rendimiento que no puede dar, y lo agrego en cuanto tengo mediciones reales.
- **Resolución dinámica.** Si un juego se atrasa, baja un escalón de resolución solo y lo devuelve cuando afloja. Respeta la escala que elegiste como techo, se congela si pausas o aceleras, y se rinde en paz si los ajustes del juego mandan más que él.
- **Memoria por juego.** Cenit recuerda qué resolución sostuvo cada juego *en tu teléfono*. La próxima partida arranca directo ahí, sin pelear desde el máximo. Si un juego resultó limitado por CPU, deja de recortarle píxeles que no le sirven. Se borra manteniendo pulsado el interruptor de memoria.
- **Recorte térmico anticipado.** Muchos celulares bajan su potencia por calor sin avisar. Cenit detecta la caída silenciosa y baja un paso *antes* de que sientas el tirón, con un tope por partida para no pasarse de listo.
- **Cuotas de CPU con evidencia.** Hay juegos que nunca llegan al 100 % aunque todo esté al máximo (Shadow of the Colossus es el clásico). Si Cenit nota que tu juego se quedó clavado, te lo dice con los segundos que midió y te sugiere probar las cuotas; si no, te recomienda dejarlas en Normal. Se guardan solo para el juego que tienes delante y se aplican al instante.
- **Hilos anclados al núcleo rápido.** Mantiene el motor del juego y la gráfica en los núcleos potentes, sin que el sistema los mueva a los economizados a mitad de frame. Apagable para diagnosticar.
- **Ritmo de cuadro según tu gama.** En media/alta usa el ritmo óptimo (el mando responde antes); en baja deja dos cuadros de amortiguación, que es como absorbe los picos sin perder fluidez.
- **Carga de texturas por gama.** En gama baja solo sube a la gráfica las texturas que se van a ver: cientos de megas de memoria compartida libres y menos microcortes al entrar a zonas nuevas.
- **Plan B gráfico.** Si el renderizador automático no logra iniciar, prueba Vulkan y OpenGL entre sí y te avisa en pantalla en vez de quedarse negro.

### 2. Control fino, juego por juego

- **Ajustes por juego que sí se guardan y sí llegan.** El desplazado de medio píxel, las cuotas de CPU y **el Ciclo EE** se escriben en la configuración individual de cada juego y se aplican al vuelo, sin reiniciar. Desde 0.6.14 el núcleo también lee las rutas de Android moderno (`saf://`), así que el valor que ves en pantalla es el que el emulador está usando; antes el motor los borraba o simplemente no los cargaba.
- **Ciclo EE por juego (0.6.12).** La velocidad de la CPU emulada (el −1/−2 que Shadow of the Colossus y God of War necesitan para ser jugables en gama media) era global: bajarla para un juego la bajaba para toda la librería. Hoy se guarda solo para ese juego, y el regidor de resolución dinámica lee el valor *efectivo* para no pelear contra ti.
- **Packs de texturas.** Importa y gestiona paquetes de texturas de alta resolución por juego, con carga asíncrona y precarga opcionales.
- **Arreglos automáticos de compatibilidad.** El motor aplica las correcciones conocidas de cada juego, y ninguna función mía las rompe al tocar ajustes manuales.
- **Los dos aceleradores delicados, con interruptor.** La lectura de disco acelerada (Fast CDVD) y el VU1 en hilo aparte (MTVU) son las dos ayudas que pueden romper un juego concreto. Hasta 0.6.6 Cenit los encendía por su cuenta; hoy están en Ajustes, explicados, y el de disco viene **apagado por defecto** porque en un teléfono el ahorro es mínimo y el riesgo no.
- **Trampas, tarjetas de memoria y estados de guardado.** Menú de trampas, administrador completo de memory cards (crear, importar, exportar) y guardado/cargado rápido en cualquier momento.
- **Logros (RetroAchievements).** Inicia sesión y juega por logros, con notificaciones dentro del juego.

### 3. Drivers gráficos: importación, protección y —desde 0.6.20— ajuste fino

Éste es el bloque más reciente de mi proyecto y el que más cambió la forma en que Cenit habla con la GPU.

**Importar drivers (Adreno).** En celulares Snapdragon puedes cargar un controlador Turnip (Mesa) en el formato estándar de la comunidad (el mismo de Yuzu, Strato y Vita3K): botón en Ajustes, eliges el `.zip`, se instala y se activa. Solo aplica a gráfica Adreno; en el resto, el botón no tiene efecto.

**Cenit Turnip, mi propio driver.** Un workflow de CI (`build-turnip.yml`) compila Turnip desde la fuente oficial de Mesa para Android arm64 y lo empaqueta como driver-pack adrenotools con identidad propia (`vulkan.cenit.so`), listo para importar sin tocar nada más. Estado actual: **Mesa 25.3.6 con ThinLTO**. Cada build deja además, como artefacto del CI, el binario con símbolos sin strip: si un juego revienta con este driver, traduzco la dirección del backtrace a la función culpable el mismo día.

**Guardarraya de drivers.** Un controlador de terceros puede ser perfecto para diez juegos y reventar el onceavo durante el arranque. Cenit anota cada intento de arrancar con driver personalizado y solo lo da por bueno cuando el juego lleva un rato dibujando de verdad. Si el juego muere antes, **en ese mismo instante** se atribuye el fallo y de ahí en adelante ese juego arranca con el driver del sistema —el resto de tu biblioteca conserva el tuyo— con un aviso en español. Tocar «Forzar otra vez el driver seleccionado» limpia el historial y reintenta desde cero.

**Ajuste fino del driver (0.6.20).** Aquí está la novedad importante. Cenit ahora **configura el driver antes de cargarlo**, en lugar de aceptarlo como viene:

- **Perfiles según tu gama.** Automático elige uno; también puedes fijarlo a mano. En gama baja se aplica la configuración conservadora (menos batching y menos resolve concurrente, que es donde la memoria compartida se satura); en media, el perfil intermedio. Los perfiles agresivos quedan manuales a propósito.
- **Caché de shaders en disco activada.** En Android, Mesa trae esta caché **desactivada por defecto**. Al encenderla (hasta 512 MB, se puede apagar), el trabajo de compilar shaders ya no se repite cada vez que abres un juego: la segunda sesión arranca sin ese desfile de congelamientos y tirones iniciales. Es la mejora más universal de esta versión porque aplica a todos los juegos y a todos los teléfonos con driver importado.
- **Reglas por juego.** Puedes darle banderas distintas a un solo juego, identificadas por su serial, sin afectar al resto de la biblioteca. Las banderas (`TU_DEBUG`) se aplican desde ya con el driver que tienes instalado.
- **Correcciones finas horneadas (driconf).** En Android el driconf **no se lee de un archivo en tiempo de ejecución**: se compila dentro del driver. Cenit ya exporta el archivo de reglas y mi CI lo hornea en la siguiente compilación. El diálogo te dice con claridad qué parte de una regla está viva ahora y qué parte necesita recompilar — no te vendo como activo lo que no lo es.
- **Medidor honesto.** El diálogo del driver muestra los FPS medios por juego y **por configuración realmente aplicada**: si una regla se suspendió, la fila pasa a llamarse «driver de fábrica» y no sigue acreditándose el mérito al perfil que ya no está activo. Los números son locales y se pueden borrar.
- **Guardarraya más justo.** Si un juego se cierra con mi ajuste fino activado, lo primero que se retira es **el ajuste fino de ese juego**, no el driver. El driver solo se condena si vuelve a fallar ya limpio. Así no se le echa la culpa a un controlador por un error mío.

> **Sobre promesas de rendimiento.** Ni este driver ni ningún perfil se anuncian como «más rápido» sin datos. La ganancia esperable y verificable hoy es la de la caché de shaders (menos compilación repetida, arranque más suave). Lo demás requiere tu medición en tu teléfono, y para eso construí el medidor.

### 4. Instrumentación: medir antes de tocar

Cenit no cambia el motor "a ver si suena". Desde 0.6.13 vengo construyendo la evidencia primero:

- **Sincronía EE↔VU1 (0.6.13).** El cuello de Shadow of the Colossus es CPU, y ninguna métrica respondía la pregunta decisiva: *¿el EE pierde el tiempo esperando al VU1, o el VU1 está realmente ocupado?* Hoy el HUD muestra `MTVU: espera X ms (Nx) | publica Y ms (Nx)`, con guardas para que un juego sin MTVU no pague ni las lecturas de reloj. El dato viaja en el log del reporte.
- **Sonda de trazas VU (0.6.15 → 0.6.17).** Un interruptor en Ajustes activa un registro 100 % pasivo del recompilador microVU: entradas al dispatcher, ejecuciones por bloque, secuencias bloque→bloque, la **forma** de cada bloque (microinstrucciones, ciclos cobrados y motivo por el que termina ahí), aristas estáticas de salto y un **ranking de candidatos a fusión ordenados por beneficio**, con las ramas condicionales marcadas por separado porque su beneficio sale inflado. El informe `vu_probe.txt` viaja adjunto en «Enviar registro» sin que lo busques.
- **Diagnóstico de afinidad (0.6.13).** El log de arranque imprime, para EE/VU1/GS, no solo el índice del núcleo sino su **clúster y frecuencia máxima**, para responder con datos de tu teléfono concreto si el hilo del juego cayó de verdad en el núcleo rápido.
- **Auditoría de compilación en el CI.** Mi job genera `compile_commands.json` y vuelca en el log los flags reales de los archivos calientes (microVU, iR5900) más el tamaño de los `.so`. Release/LTO se auditan con evidencia, no con suposiciones. Se confirmó así que el core ya compila a −O3: ahí no había margen que buscar.
- **Evidencia de cierres (0.6.10/0.6.11).** Cuando un driver tumba el juego, el proceso muere de un golpe y Java no se entera. Cenit guarda **la señal exacta del choque** (SIGSEGV/SIGABRT, la dirección y el `.so` culpable, por ejemplo `libvulkan_freedreno.so`) más una línea del núcleo que prueba qué driver estaba cargado de verdad. Y si el renderer está en OpenGL o Software —donde el driver personalizado ni se carga—, el diálogo lo advierte al instante en lugar de quedarse esperando.

### 5. La experiencia de usarlo

- **Pantalla de inicio propia (rediseñada en 0.6.9, pulida en 0.6.18).** Tarjetas con relieve donde la carátula ocupa todo y el título va sobrepuesto; nombre limpio de extensiones, códigos de región y etiquetas de scene (`Shadow.of.the.Colossus.(USA).ch1.iso` se lee **Shadow of the Colossus**); carrusel «Siguiendo donde lo dejaste» con lo jugado en la última semana; secciones con contador; borde que se enciende en cian al enfocar con el mando; carátula con estética PS2 por defecto cuando no hay imagen; fondo con degradado medianoche y barra inferior: Inicio · Biblioteca · Carpetas · Ajustes. Buscador en vivo.
- **Asistente de primera vez.** Tres pasos con progreso claro: BIOS, carpetas y listo. Nunca te deja atrapado ni te pide saber de emuladores.
- **Mandos en pantalla.** Estilo fantasma transparente, con hombros y gatillos arriba (L3/L2/L1 · R2/R1/R3), cruceta y botones con los símbolos de PS2. Solo aparecen con un juego en marcha.
- **Soporte de mandos externos.** Bluetooth y USB, con pantalla de prueba de botones.
- **Ajustes a pantalla completa** organizados por secciones, con todo lo de arriba al alcance y explicado en español.
- **Formatos de juego.** ISO, BIN/CUE, CHD y comprimidos.
- **Textos y privacidad (0.6.19).** Diálogos corregidos y en español, política de privacidad propia publicada, y búsqueda de carátulas y packs de texturas con la identidad correcta de la app.

---

## Cómo se comporta en gama baja y media

- **Gama baja (4 GB de RAM, Mali/Adreno de entrada):** arranca en 1×; la carga de texturas parcial, la resolución dinámica y la memoria por juego trabajan juntos. En la práctica, juegos 2D y 3D sencillos corren a tiempo y los pesados quedan jugables con baches menos frecuentes.
- **Gama media (Snapdragon 7-series, Helio G99 y similares):** el escenario donde Cenit más brilla. La segunda partida de un juego ya aprendido arranca en su escala sostenida, los tirones por calor se adelantan, las cargas se aceleran solas si activas el turbo y —desde 0.6.20— los shaders ya no se recompilan al volver a un juego.
- **Gama alta:** también gana (ritmo óptimo, hilos anclados), pero su mérito es no estorbar: el techo de resolución siempre es el tuyo.

Lo digo claro: ningún truco hace correr *God of War* a 60 cuadros en un teléfono de 100 dólares. Cenit reduce la distancia entre «no es jugable» y «se puede disfrutar», y te muestra la evidencia de cada decisión.

---

## Cómo empezar

1. Descarga `Cenit-0.6.23.apk` desde [Lanzamientos](https://github.com/HUEVOMAN77/Cenit/releases) e instálala (Android pedirá permitir apps de esta fuente la primera vez).
2. Abre la app y sigue el asistente: coloca la **BIOS extraída de tu propia PS2** y elige la carpeta de tus juegos.
3. Toca un juego de la biblioteca y listo. Con una partida abierta, el panel **Ajustes → Rendimiento** muestra todos los mandos de Cenit.
4. **Si tu teléfono es Snapdragon/Adreno**: descarga también `Cenit-Turnip-25.3.6-*.zip` de la misma release, impórtalo en **Ajustes → Controlador gráfico personalizado** (no lo descomprimas), toca «Forzar otra vez el driver seleccionado» y juega normal. La segunda sesión de cada juego ya no recompila shaders; el diálogo del driver va llenando el medidor solo.
5. Consejo: la primera semana activa el **HUD de rendimiento** en Ajustes y verás qué está haciendo el motor por ti.

## ¿Algo no funciona? Avísame

Cenit lo desarrollo yo solo, y eso tiene una ventaja directa: **no hay un formulario que se pierde en una bandeja compartida — tus reportes los lee el dueño del proyecto.**

Para contar un problema o pedir una mejora, usa la pestaña **[Issues](https://github.com/HUEVOMAN77/Cenit/issues)**. No necesitas saber programar ni escribir en inglés; alcanza con contarlo en español. Para que el reporte me sirva de verdad, trata de incluir:

- **Tu celular**: marca y modelo (por ejemplo, *Samsung A34*, *Redmi Note 12*).
- **El juego** que falla, y si te da igual con otro.
- **Qué esperabas y qué pasó**: «se queda en pantalla negra», «va al 40 % de velocidad», «el audio se corta», «se cierra solo al guardar».
- **Una captura del HUD de rendimiento** si puedes (Ajustes → Cuenta y extras → HUD de rendimiento).
- **Y lo más útil de todo: el registro.** Si un juego se cierra o no arranca, abre Ajustes → «Cuenta y extras» → **Enviar registro de errores**. Cenit arma un archivo con tu modelo de celular, la versión, el estado del registro en disco y las últimas líneas del registro del sistema — que sobrevive incluso cuando el juego muere antes de escribir cualquier archivo. Desde Android 11 no puedes sacar esos registros con un explorador de archivos: por eso existe el botón. Si usaste la sonda VU, `vu_probe.txt` viaja adjunto sin que la busques.

Los reportes de juegos específicos son la materia prima con la que construyo la tabla de rendimiento y el ajuste fino por hardware. Si tu gama baja corre algo que antes era injugable, también vale la pena contarlo.

---

## Historial versión por versión

Todo mi camino, incluido lo que estuvo mal y corregí. Cada fila tiene su release con sus archivos.

### Base del proyecto (0.6.2 → 0.6.7)

| Versión | Qué sumó |
|---|---|
| **0.6.2** | Ajustes a pantalla completa, mandos en pantalla estilo fantasma, carátulas automáticas, identidad animada. |
| **0.6.3** | Resolución dinámica automática y speedhacks de CPU reales; corregido el reinicio del regidor fuera del hilo principal. |
| **0.6.4** | Clasificación de gama curada, preparación de hacks por-juego, APIs térmicas con verificación por reflexión. |
| **0.6.5** | Cuatro mecanismos propios: **memoria por juego**, **pre-corte térmico**, **turbo en pantallas de carga** y **cuotas de EE con evidencia**. |
| **0.6.6** | Anclaje al núcleo rápido garantizado, cola de cuadros por gama y precarga de texturas parcial en gama baja. |
| **0.6.7** | Fast CDVD deja de forzarse y se vuelve apagable (apagado por defecto), MTVU con interruptor, y **registro de errores exportable** desde la app. |

### Los drivers: importación, fallos y evidencia (0.6.8 → 0.6.11)

| Versión | Qué sumó |
|---|---|
| **0.6.8** | Primer guardarraya de drivers personalizados y reporte con evidencia del sistema. |
| **0.6.9** | Pantalla de inicio rediseñada (tarjetas, recientes, nombres limpios). |
| **0.6.10** | **El guardarraya por fin cierra el ciclo.** Las dos versiones anteriores tenían tres fallos míos: el fallo se atribuía a una señal que casi nunca llegaba, el historial se borraba al tocar el diálogo y no quedaba evidencia. Los tres quedaron cerrados, con pausa de la resolución dinámica durante el intento pendiente. |
| **0.6.11** | OpenGL deja de morir por `gl_FragDepth` en Adreno; **cachés de Vulkan separadas por driver** (la causa más probable del «carga y se sale»); captura de la señal nativa del choque con el `.so` culpable; botón «Forzar otra vez». |
| **driver 2026-09-24** | Nace **Cenit Turnip**: primer driver compilado por mi CI (Mesa 25.2.8) en formato adrenotools con `vulkan.cenit.so` propio y símbolos aparte para diagnosticar. |

### Medir el motor (0.6.12 → 0.6.17)

| Versión | Qué sumó |
|---|---|
| **0.6.12** | **Ciclo EE por juego**, con getter de valor efectivo para que la resolución dinámica no pelee contra ti. La salida real de SOTC/GoW en gama media. Desde aquí, **cada release muestra APK y driver juntos**. |
| **0.6.13** | Instrumentación de la sincronía EE↔VU1 en HUD y reporte; corregido el regidor que recortaba resolución con la métrica de GPU ausente; diagnóstico de afinidad (clúster + frecuencia); auditoría de flags de compilación en el CI. |
| **0.6.14** | **El INI por-juego por fin se carga**: `FileExists()` no entendía rutas `saf://`, así que los ajustes por juego se guardaban pero nunca llegaban al emulador. Además build identificable y HUD con valores efectivos del núcleo. |
| **0.6.15** | Sonda VU **Fase 1**: interruptor, trazas pasivas de dispatcher, bloques y secuencias, informe en `vu_probe.txt`. Cero cambios de emulación con la sonda apagada. |
| **0.6.16** | **Fase 1.5**: forma del bloque (ops, ciclos, motivo de corte), ranking por trabajo y histograma de motivos. Pulido: `vu_probe.txt` viaja adjunto en el reporte, `emulog.txt` ya no se pierde al reiniciar en Huawei, HUD más corto, pinning sin «0 MHz» engañoso, ISO truncada deja de inundar el log. |
| **0.6.17** | **Fase 1.6**: aristas estáticas A→B que microVU enlaza al compilar (el ~97 % del tráfico que no se veía), **ranking de candidatos a fusión por beneficio** (`ftop`), ramas condicionales marcadas `[COND]` y separadas del total porque su beneficio está inflado, y resumen de aristas en el reporte. |

### Identidad y ajuste fino del driver (0.6.18 → 0.6.20)

| Versión | Qué sumó |
|---|---|
| **0.6.18** | Carátula por defecto con estética PS2 (14× más liviana) y tarjetas de biblioteca rediseñadas con foco en cian. |
| **0.6.19** | Textos de la app corregidos y en español, política de privacidad propia, identidad correcta al buscar carátulas y packs de texturas. |
| **0.6.20** | **Ajuste fino del driver (tres niveles).** Driver sobre **Mesa 25.3.6 + ThinLTO**; perfiles automáticos por gama; **caché de shaders en disco** activada para drivers importados; **reglas por juego** (banderas activas ya, correcciones driconf horneadas en la siguiente compilación); **medidor de FPS por configuración realmente aplicada**; y guardarraya que retira mi ajuste fino antes de condenar un driver. |

### Motor de superbloques VU (0.6.21)

| Versión | Qué sumó |
|---|---|
| **0.6.21** | **Motor de superbloques VU (Fases 2 a 5): experimental y APAGADO por defecto.** Con la evidencia de la sonda (0.6.15–0.6.17) construí el motor de fusión: **Registro** de aristas en tiempo real con umbral de calor, **Fusión** de cadenas rectas de bloques VU1 en un solo bloque compilado sin cortes intermedios (con límites duros de uniones, microinstrucciones y ciclos, y rechazo de cualquier unión con bit de espera, rama en retardo o patada pendiente), **Replay diferencial** —cada variante se compila como candidata privada y se valida contra el bloque normal comparando el estado de salida byte a byte antes de confiar en ella— y un **GATE**: la variante solo entra en juego tras validar, y ante tres divergencias en un PC el motor se autoapaga y quema ese PC. Identidad de caché persistente extendida (hash del microcódigo + versión del compilador + modo de punto flotante + gama del dispositivo) y programación del hilo anfitrión para el VU1 sin tocar el timing de la PS2. Todo detrás de un interruptor en Ajustes, apagado por defecto; sin una sola promesa de rendimiento sin medición en tu teléfono. El HUD muestra `VUSB: bld conf match div kill` y el reporte incluye el vuelco del motor, para que los datos de esta fase se midan igual que los de la sonda. |

### Canal de evidencia reparado + vigilante de congelación (0.6.22)

| Versión | Qué sumó |
|---|---|
| **0.6.22** | **El reporte de errores por fin trae el log del núcleo, y ahora captura la congelación en el momento.** En Android, el canal de archivo del registro estaba muerto: la línea de despacho envolvía *todo* el envío y solo espejaba al registro del sistema, así que `emulog.txt` nacía siempre en 0 bytes y el botón «Enviar registro» llegaba sin el log del núcleo — justo cuando más se necesita (congelación, cierre solo). Reparado: cada línea va ahora a logcat **y** al archivo (con flush por línea, sobrevive un cierre brusco del sistema). Además: **vigilante de congelación** —si el juego está corriendo, sin pausa, y el contador de cuadros del VM lleva 20 segundos quieto, Cenit toma la foto del registro EN ESE INSTANTE (incluido el búfer de fallos nativos de Android) en vez de esperar al siguiente reporte—; **captura de muertes inesperadas a media sesión**, que antes solo se fotografiaban si ocurrían en la ventana de arranque; **arreglado el tiempo jugado**, que nunca se guardaba porque la carpeta de ajustes no existía en Android; y la detección de hardware ya no repite sus mensajes cada segundo. Sin promesas: lo que la congelación de God of War es —driver, MTVU o memoria— lo dirá la evidencia que este build ya recoge. |

### Causa raíz del congelamiento encontrada y reparada + mandos que se apartan solos (0.6.23)

| Versión | Qué sumó |
|---|---|
| **0.6.23** | **El congelamiento de God of War tenía causa en Cenit, y ya está arreglada.** Con la evidencia que empezó a traer el reporte de 0.6.22 lo confirmé: el emulador **se cargaba su propio disco**. Cuando el juego ya estaba corriendo, cualquier consulta de datos del juego (identificador, CRC, ajustes por juego) **reabría y cerraba el ISO que el juego tenía abierto**, porque el lector de disco del núcleo es un único objeto compartido. A partir de ahí toda lectura del juego fallaba (`Block index is past the end of file!`) y el juego se quedaba esperando un dato que nunca llegaba: eso es la congelación. Lo mismo explica varios «se sale solito». El arreglo: **nunca tocar el disco con el juego delante** — la identidad del juego en marcha ya está en memoria, y para los demás juegos se usa solo la caché de la biblioteca. **Y los mandos en pantalla ahora se apartan solos:** si dejas de tocar la pantalla 10 segundos (por ejemplo porque jugabas con mando Bluetooth), desaparecen para dejar ver solo el juego; al volver a tocar, reaparecen. Con un dedo puesto sobre el joystick no se ocultan, y el botón de pausa y el de menú se quedan siempre a la mano. |

---

## Hoja de ruta

Lo que sigue es mi plan honesto: parte es ingeniería difícil, parte es directamente ambiciosa, y **nada de esto se anuncia como «ya funciona»**.

### Plan inmediato

1. **Validar el motor de superbloques VU con datos reales.** El motor (Fases 2 a 5) ya está construido en 0.6.21, con su replay diferencial y su GATE de auto-apagado — pero **sigue apagado por defecto** hasta demostrarlo: cero divergencias en los reportes y mejora sostenida medida en teléfonos reales (empezando por el mío) antes de encenderlo para nadie. Lo que falta no es código, es evidencia.
2. **Reglas por juego que se vuelvan configuración de fábrica.** Cada regla que demuestre ganar en el medidor la horneo en el siguiente Cenit Turnip, para que el beneficio llegue a todos sin que nadie edite nada. El canal ya está construido en 0.6.20.
3. **Más ajustes por juego con criterio propio:** lectura de texturas dentro de la gráfica (el de mayor salto en juegos con agua y reflejos), escalado nativo de sprites y salto de dibujos, todos guiados por la evidencia que ya juntan los perfiles de memoria.
4. **Ajuste fino por hardware:** perfiles específicos para cada plataforma (Mali, Exynos, Kirin), con la misma lógica de medición que ya usa Snapdragon.
5. **Tabla de rendimiento de la comunidad:** que lo que Cenit aprende en cada teléfono (escala sostenida, tipo de cuello de botella) se convierta en configuración sugerida por juego, compartida entre usuarios.
6. **Celular más frío, menos recortes:** colaboración con el sistema de energía de Android para que el procesador suba frecuencia *antes* del pico en lugar de recortarla después.
7. **Menos recortes en video y escenas:** que la resolución dinámica no baje durante cinemáticas ni cortes de escena.

### La gran apuesta: renderizar abajo, ver arriba

La idea central de mis próximas versiones grandes: **que el juego renderice por debajo de la resolución nativa de PS2 —incluso 0.5×, la más baja— y que Cenit reconstruya esa imagen en tiempo real para mostrarla nítida a 720p u 1080p en la pantalla del celular.**

Suena descabellado, y por eso nadie en un port de PS2 se había atrevido a intentarlo. Pero no es magia: es la misma familia de técnicas que usan los escaladores modernos (reconstruir una imagen apoyándose en *guías* reales del cuadro —profundidad, vectores de movimiento, historial de fotogramas—) y que emuladores de otras consolas aplican desde hace años para estirar resoluciones internas bajísimas. El truco no es estirar píxeles borrosos: es usar la información geométrica que el propio motor gráfico ya genera para reconstruir una imagen limpia donde el hardware no alcanza a dibujarla nativa.

Por qué importa en gama baja y media: dibujar un cuadro a 0.5× cuesta una fracción de dibujarlo a 1× o 2×. Menos cuadros perdidos, menos calor, menos batería. Si la reconstrucción funciona, un teléfono que hoy no pasa de 25 cuadros a 1× podría mover el mismo juego **más fluido y viéndose mejor que a resolución nativa**.

Cómo la voy a construir, sin apuro y sin humo:

1. **Desde cero, en ramas de experimentación**, no en la versión estable. Cada paso se mide contra el renderizado actual; si un paso no mejora, no avanza.
2. **Con la guía que ya existe dentro del motor.** El renderizador gráfico ya produce los datos que una reconstrucción necesita; el trabajo es capturarlos, moverlos a un pase de posprocesamiento barato en la GPU del teléfono y evaluar calidad real, cuadro a cuadro, contra el resultado nativo.
3. **Juego por juego, no global.** Habrá títulos donde se vea excelente y títulos donde no; la memoria por juego que Cenit ya tiene es la base para decidir dónde se activa.
4. **Con honestidad en cada release.** Mientras sea experimental, la opción estará marcada como tal y tendrá interruptor para volver al renderizado clásico. Nadie va a descubrir una regresión por sorpresa.

Es la meta más ambiciosa del proyecto y la que más tiempo va a llevar. También la que, de lograrse, más va a cambiar lo que un gama baja puede hacer con una PS2 en el bolsillo.

### Otras líneas en estudio

- **Caché de sombreadores compartida entre usuarios:** que el segundo jugador de un mismo juego no reconpile desde cero. (El primer tramo de esta idea ya está en marcha: la caché por dispositivo de 0.6.20; falta la parte de compartirla.)
- **Precisión de mezcla adaptativa:** bajar la emulación de mezcla de color solo en las zonas de la pantalla que no se notan, en vez de un ajuste global que castiga todo.
- **Audio con menos carga:** búferes dinámicos según si el juego va sobrado o ahogado, para recuperar cuadros sin cortes audibles.
- **Perfiles listos por tipo de juego:** un menú simple —«prioridad fluidez», «prioridad imagen», «equilibrado»— que aplique el conjunto correcto de todos los mandos de arriba.

**Estabilidad continua:** cada versión pasa por compilación y verificación automatizadas antes de publicarse, y el driver se compila desde la fuente oficial en mi propio CI, con sus símbolos guardados para poder leer cualquier cierre.

---

## Cómo trabaja este proyecto

Cuatro reglas que puedes auditar en el historial de commits:

1. **Medir antes de tocar.** Ningún cambio de sincronía, ciclos, afinidad o ABI entra sin una métrica que diga de antemano si puede ayudar y después si ayudó. Cuando una versión no trae rendimiento, trae instrumentación.
2. **Todo se puede apagar.** Cada mecanismo que construyo tiene su interruptor y su explicación en español, porque tu teléfono manda más que cualquier perfil general.
3. **Sin promesas sin datos.** Si algo no está medido en hardware real, lo describo como esperado o como experimental, nunca como arreglo. También digo en público mis bugs y mis versiones que no funcionaron (0.6.8, 0.6.9 y el INI por-juego hasta 0.6.14 están ahí con nombre y apellido).
4. **Reproducible por contrato.** CI compilando el emulador y el driver, registro de flags de compilación en el log, símbolos sin strip guardados, y releases que siempre llevan los dos archivos juntos.

---

## Autoría

**Cenit es mío: [HUEVOMAN77](https://github.com/HUEVOMAN77).** Diseño, código, ingeniería de rendimiento, drivers, interfaz, documentación y soporte. Este fork existe desde el primer día para tener identidad propia, y hoy es un proyecto independiente en todo menos en el linaje del código base: cada línea propia de Cenit, cada release y cada decisión de este repositorio me pertenecen.

## Créditos de terceros (ascendencia técnica)

Que el proyecto sea mío no borra de dónde salió el código base, y la licencia GPL-3.0 exige nombrarlo —lo hago con gusto, porque es parte del folio del proyecto—:

- **[PCSX2](https://github.com/PCSX2/pcsx2)** — el emulador del que desciende el motor.
- **[PCSX2_ARM64](https://github.com/pontos2024/PCSX2_ARM64)** — la compilación nativa para ARM64 sobre la que se apoya el adaptador Android.
- **[PSX2 (izzy2lost)](https://github.com/izzy2lost/PSX2)** — el proyecto Android del que este repositorio es bifurcación directa.
- **Mesa/Turnip (freedreno, MIT)** — base del driver Vulkan propio `Cenit Turnip`.
- **adrenotools** — el formato de driver-pack que Cenit importa.
- Y las bibliotecas de terceros de `app/src/main/cpp/3rdparty/`, cada una con su licencia original intacta.

Esa es la ascendencia. Todo lo demás —la identidad, la interfaz, el motor de rendimiento adaptativo, los ajustes por juego, los perfiles de hardware, el guardarraya, el ajuste fino del driver, la sonda y el futuro motor de superbloques VU— es de Cenit y de su autor.

## Aviso legal

Cenit es un proyecto educativo y de uso personal, sin vínculo con Sony Interactive Entertainment, con el equipo de PCSX2 ni con Google. No distribuye BIOS ni juegos: necesitas tu propia consola PS2 de la que extraer la BIOS y tus propias copias de los juegos.
