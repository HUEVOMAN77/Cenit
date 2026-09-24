# CENIT — Emulador de PlayStation 2 para Android

[![Licencia: GPL v3](https://img.shields.io/badge/Licencia-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Android](https://img.shields.io/badge/Android-8.0%20o%20superior-green.svg)](https://developer.android.com/)
[![ARM64](https://img.shields.io/badge/Procesador-arm64--v8a-orange.svg)](https://developer.arm.com/)
[![Última versión](https://img.shields.io/badge/versión%20actual-0.6.6-informational)](https://github.com/HUEVOMAN77/Cenit/releases/tag/base-0.6.6)

**Cenit** es un emulador de PlayStation 2 para celulares Android, hecho como proyecto independiente a partir de [PSX2](https://github.com/izzy2lost/PSX2) (el adaptador Android de PCSX2 2.7). Lo desarrolla una sola persona, pieza por pieza y en público, con una meta muy concreta: **que los juegos de PS2 se muevan fluidos en celulares de gama baja y media, que es donde ningún otro proyecto mira.**

No es una copia con otro logo. Cada versión suma mejoras de rendimiento y de uso que no existen ni en PSX2 ni en PCSX2, medidas sobre celulares reales.

**Cenit 0.6.6 es la primera versión pública de este proyecto.** Es una base que funciona y se nota, pero el camino recién empieza: poco a poco, versión tras versión, el foco va a seguir siendo el mismo —que los celulares de gama baja y media mejoren— y van a sumarse soporte fino para más marcas de procesador, más ajustes inteligentes y una tabla de rendimiento construida entre todos los que lo usen.

---

## Soporte por marca de procesador

Cenit reconoce y clasifica el hardware al instalar. Esto es lo que puedes esperar hoy:

| Marca | Estado |
|---|---|
| **Qualcomm Snapdragon (Adreno)** | Soporte completo: perfil de rendimiento dedicado, detección del modelo exacto (incluso con los apodos internos que usan muchas marcas) y controladores gráficos Turnip importables. |
| **MediaTek Helio / Dimensity (Mali)** | Detectado y clasificado por gama (desde Helio G/P económicos hasta Dimensity tope de línea) con su perfil de rendimiento. El ajuste fino específico para Mali está en el camino. |
| **Samsung Exynos** | Detectado y clasificado por gama, del Exynos 7870 al 2400, con perfil según su potencia. |
| **Huawei Kirin** | Detectado y clasificado; al ser un ecosistema con menos presencia global, su ajuste fino llega más adelante. |
| **Otros (Google Tensor, Apple en Mac, etc.)** | Si el teléfono corre Android de 64 bits, Cenit funciona con el perfil conservador de gama baja hasta tener clasificación propia. |

La regla del proyecto: si tu procesador no está catalogado, se asume el perfil más seguro (gama baja) para no prometer rendimiento que no puede dar, y se agrega en cuanto hay mediciones reales.

---

## Lo esencial

| | |
|---|---|
| **Versión actual** | 0.6.6 |
| **Descarga** | [Lanzamientos](https://github.com/HUEVOMAN77/Cenit/releases) — busca `Cenit-0.6.6.apk` |
| **Requiere** | Android 8 o superior, procesador de 64 bits y tu propia BIOS de PS2 |
| **No incluye** | BIOS, juegos ni ningún archivo con derechos de autor |
| **Firma** | Clave de prueba (es un proyecto personal, no una tienda) |
| **Licencia** | GPL-3.0, libre como el PCSX2 del que desciende |

---

## Por qué existe Cenit

PCSX2 es un emulador excelente, pero está pensado para computadoras. Al llevarlo a un celular arrastra supuestos que acá no valen: hilos de procesamiento que el sistema del teléfono manda a los núcleos lentos en mitad de una partida, esperas de la gráfica que traban todo, cargas de texturas que se comen la memoria compartida, y ajustes que el propio motor borra sin avisar.

En un gama alta eso se perdona porque sobra potencia. En un gama media o baja es exactamente la diferencia entre 25 cuadros por segundo con tirones y 50 estables. Cenit ataca ese problema: **reconoce tu hardware, aprende cómo se comporta cada juego en tu teléfono y adapta el motor solo**, sin que tengas que saber qué significa cada ajuste.

---

## Todo lo que incluye

### Rendimiento inteligente (lo que hace especial a Cenit)

- **Perfil automático por hardware.** Al instalarlo reconoce el procesador (incluidos los apodos internos de Qualcomm que muchas marcas no reportan bien) y deja lista la línea base: aceleradores de CPU seguros, recompilación completa y sin esperas inútiles a la gráfica. No hay que tocar nada: arranca ya configurado para tu gama.
- **Resolución dinámica.** Si un juego se atrasa, baja un escalón de resolución solo y lo devuelve cuando afloja. Respeta la escala que elegiste como techo, se congela si pausas o usas aceleración, y se rinde en paz si los ajustes del juego mandan más que él.
- **Memoria por juego.** Cenit recuerda qué resolución sostuvo cada juego *en tu teléfono*. La próxima partida arranca directo ahí, sin pelear desde el máximo. Y si un juego resultó limitado por CPU, deja de recortarle píxeles que no le sirven. Se borra manteniendo pulsado el interruptor de memoria.
- **Recorte térmico anticipado.** Muchos celulares bajan su potencia por calor sin avisar. Cenit detecta la caída silenciosa y baja un paso *antes* de que sientas el tirón, con un máximo por partida para no pasarse de listo.
- **Turbo en pantallas de carga.** Reconoce cuando el juego está cargando (pantalla quieta, gráfica sin trabajar) y acelera solo esos segundos; lo devuelve apenas vuelve la partida. Apagado por defecto, se activa en Ajustes.
- **Cuotas de CPU con evidencia.** Hay juegos que nunca llegan a tiempo aunque todo esté al máximo (Shadow of the Colossus es el clásico). Si Cenit nota que tu juego se quedó clavado, te lo dice con los segundos que midió y te sugiere probar las cuotas; si no, te recomienda dejarlas en Normal. Se guardan solo para el juego que tienes delante y se aplican al instante.
- **Hilos anclados al núcleo rápido.** Mantiene el motor del juego y la gráfica en los núcleos potentes del celular, sin que el sistema los mueva a los economizados a mitad de frame. Apagable para diagnosticar.
- **Ritmo de cuadro según tu gama.** En gama media/alta usa el ritmo óptimo (el mando responde antes); en gama baja deja dos cuadros de amortiguación, que es como absorbe los picos sin perder fluidez. Ajustable.
- **Carga de texturas por gama.** En gama baja solo sube a la gráfica las texturas que se van a ver: cientos de megas de memoria compartida libres y menos microcortes al entrar a zonas nuevas.
- **Plan B gráfico.** Si el renderizador automático no logra iniciar, prueba Vulkan y OpenGL entre sí y te avisa en pantalla, en vez de quedarse negro.

### Compatibilidad y control fino, juego por juego

- **Ajustes que se guardan por juego.** El desplazado de medio píxel y las cuotas de CPU se escriben en la configuración individual de cada juego y se aplican al vuelo, sin reiniciar. Antes el motor borraba esos valores; ahora Cenit los guarda donde el motor los respeta, conservando además los arreglos automáticos que cada juego ya traía.
- **Importación manual de controladores gráficos (drivers Vulkan).** En celulares Snapdragon/Adreno puedes cargar un controlador Turnip (Mesa) descargado por ti, en el formato de paquete estándar de la comunidad (el mismo de Yuzu, Strato y Vita3K): botón en Ajustes, eliges el `.zip`, se instala y se activa. Es la vía para ganar velocidad y corrección gráfica donde el controlador de fábrica se queda corto. Solo aplica a teléfonos con gráfica Adreno; en el resto, el botón no tiene efecto.
- **Guardarraya de drivers (desde 0.6.8).** Un controlador de terceros puede ser perfecto para diez juegos y reventar el onceavo durante el arranque, y ese cierre llega a matar la aplicación entera sin decir por qué. Ahora Cenit anota cada intento de arrancar con driver personalizado y solo lo da por bueno cuando el juego lleva un rato dibujando de verdad. Si el proceso muere antes, la próxima vez ese juego arranca con el driver del sistema —el resto de los juegos conserva tu driver— y te lo explica en un aviso. Volver a elegir el driver desde el diálogo lo intenta de nuevo desde cero.
- **Packs de texturas.** Importa y gestiona paquetes de texturas de alta resolución por juego, con carga asíncrona y precarga opcionales.
- **Arreglos automáticos de compatibilidad.** El motor aplica las correcciones conocidas para cada juego, y ninguna función de Cenit las rompe al tocar ajustes manuales.
- **Los dos aceleradores delicados, ahora con interruptor.** La lectura de disco acelerada (Fast CDVD) y el VU1 en hilo aparte (MTVU) son las dos ayudas que pueden romper un juego concreto: la primera recorta la espera simulada del DVD y hay títulos que leen sincronizado; la segunda puede colgar algunos. Hasta ahora Cenit los encendía por su cuenta y no había forma de apagarlos. Los dos están en Ajustes, con su explicación, y el de disco viene apagado por defecto: en un teléfono el ahorro es mínimo porque el juego ya es un archivo en memoria flash, así que no valía la pena el riesgo.
- **Trampas, tarjetas de memoria y estados de guardado.** Menú de trampas, administrador completo de memory cards (crear, importar, exportar) y guardado/cargado rápido en cualquier momento.
- **Logros (RetroAchievements).** Inicia sesión y juega por logros, con notificaciones dentro del juego.

### La experiencia de usarlo

- **Pantalla de inicio propia.** Biblioteca en cuadrícula con carátulas de cajas descargadas automáticamente, buscador en vivo, "Continuar" con lo último jugado y barra inferior: Inicio · Biblioteca · Carpetas · Ajustes. Los mandos táctiles solo aparecen con un juego en marcha.
- **Asistente de primera vez.** Tres pasos con progreso claro: BIOS, carpetas y listo. Nunca te deja atrapado ni te pide saber de emuladores.
- **Mandos en pantalla rediseñados.** Estilo fantasma transparente, con hombros y gatillos arriba (L3/L2/L1 · R2/R1/R3), cruceta y botones con los símbolos de PS2, y cruceta direccional en rombo sobre el stick.
- **Soporte de mandos externos.** Bluetooth y USB, con pantalla de prueba de botones.
- **Ajustes a pantalla completa** organizados por secciones, con todo lo de arriba al alcance y explicado en español.
- **Formatos de juego.** ISO, BIN/CUE, CHD y comprimidos.

---

## Cómo se comporta en gama baja y media

- **Gama baja (4 GB de RAM, Mali/Adreno de entrada):** arranca en 1x con la carga de texturas parcial, la resolución dinámica y el perfil de memoria por juego trabajan juntos; en la práctica, juegos 2D y 3D sencillos corren a tiempo y los pesados quedan jugables con baches menos frecuentes.
- **Gama media (Snapdragon 7-series, Helio G99 y similares):** el escenario donde Cenit más brilla — la segunda partida de un juego ya aprendido arranca en su escala sostenida, los tirones por calor se adelantan, y las cargas se aceleran solas si activas el turbo.
- **Gama alta:** también gana (ritmo óptimo, hilos anclados), pero su mérito es no estorbar: el techo de resolución siempre es el tuyo.

Lo decimos claro: ningún truco hace correr *God of War* a 60 cuadros en un teléfono de 100 dólares. Cenit reduce la distancia entre "no es jugable" y "se puede disfrutar", y te muestra la evidencia de cada decisión.

---

## Cómo empezar

1. Descarga `Cenit-0.6.6.apk` desde [Lanzamientos](https://github.com/HUEVOMAN77/Cenit/releases) e instálala (Android pedirá permitir apps de esta fuente la primera vez).
2. Abre la app y sigue el asistente: coloca la **BIOS extraída de tu propia PS2** y elige la carpeta de tus juegos.
3. Toca un juego de la biblioteca y listo. Con una partida abierta, el panel **Ajustes → Rendimiento** muestra todos los mandos de Cenit.
4. Consejo: la primera semana activa el "HUD de rendimiento" en Ajustes y verás qué está haciendo el motor por ti.

## ¿Algo no funciona? Avísame

Cenit lo desarrolla una sola persona, y eso tiene una ventaja directa: **no hay un formulario que se pierde en una bandeja compartida — tus reportes los lee el creador del proyecto.**

Para contar un problema o pedir una mejora, usa la pestaña **[Issues](https://github.com/HUEVOMAN77/Cenit/issues)** de este repositorio (Incidencias). No necesitas saber programar ni escribir en inglés; alcanza con contarlo en español. Para que el reporte sirva de verdad, trata de incluir:

- **Tu celular**: marca y modelo (por ejemplo, *Samsung A34*, *Redmi Note 12*).
- **El juego** que falla, y si te da igual con otro.
- **Qué esperabas y qué pasó**: "se queda en pantalla negra", "va a 40% de velocidad", "el audio se corta", "se cierra solo al guardar".
- **Si puedes, una captura** del HUD de rendimiento en pantalla (el interruptor está en Ajustes, sección "Cuenta y extras": "HUD de rendimiento").
- **Y lo más útil de todo: el registro.** Si un juego se cierra solo o no arranca, abre Ajustes → "Cuenta y extras" → **Enviar registro de errores**. Cenit arma un archivo con tu modelo de celular, la versión, el estado del registro en disco y las últimas líneas del registro del sistema — que sobrevive incluso cuando el juego muere antes de escribir cualquier archivo. Sin ese reporte, diagnosticar un cierre es adivinar; con él, casi siempre se ve la causa. Desde Android 11 no puedes sacar esos registros con un explorador de archivos — por eso el botón existe.

Los reportes de juegos específicos son los más valiosos que existen para este proyecto: son la materia prima con la que se construye la tabla de rendimiento y el ajuste fino por marca de procesador. Si tu gama baja corre algo que antes era injugable, también vale la pena contarlo.

Las versiones se publican en [Lanzamientos](https://github.com/HUEVOMAN77/Cenit/releases); cada una explica con letra clara qué cambió y qué sigue sin resolverse.

---

## Por qué la firma es de prueba

Cenit es un proyecto personal, sin cuenta de desarrollador ni intención de tienda. La firma de prueba es lo honesto para un proyecto comunitario: la APK se instala directa y no suplanta ninguna firma oficial. Si eso cambia algún día, se anunciará aquí y en cada lanzamiento.

## Lo que viene

Cenit es un **proyecto a largo plazo**. No se terminó con la 0.6.6: recién empieza. Optimizar un emulador de PS2 para celulares exige mucho conocimiento y mucho tiempo, y todo esto se construye poco a poco, versión a versión, midiendo sobre hardware real. Lo que sigue es el plan honesto — parte es ingeniería difícil, parte es directamente ambiciosa, y nada de esto se anuncia como "ya funciona":

### Plan inmediato

- **Más ajustes por juego con criterio propio:** lectura de texturas dentro de la gráfica (el de mayor salto en juegos con agua y reflejos), escalado nativo de sprites y salto de dibujos, todos con la evidencia que ya juntan los perfiles de memoria como guía.
- **Ajuste fino por marca de procesador:** perfiles específicos para MediaTek/Mali y Samsung/Exynos (hoy reciben el perfil general según su gama), con la misma lógica de medición que ya usa Snapdragon.
- **Tabla de rendimiento de la comunidad:** que lo que Cenit aprende en cada teléfono (escala sostenida, tipo de cuello de botella) se convierta en una configuración sugerida por juego, compartida entre usuarios.
- **Celular más frío, menos recortes:** colaboración con el sistema de energía de Android para que el procesador suba frecuencia *antes* del pico en lugar de recortarla después.
- **Menos recortes en video y escenas:** que la resolución dinámica no baje durante cinemáticas ni cortes de escena.

### La gran apuesta: renderizar abajo, ver arriba (escalamiento con guía de cuadros)

La idea central de las próximas versiones grandes es esta: **que el juego renderice a una resolución por debajo de la nativa de PS2 —incluso 0.5x, la más baja— y que Cenit reconstruya esa imagen en tiempo real para mostrarla nítida a 720p u 1080p en la pantalla del celular.**

Suena descabellado, y por eso mismo nadie en un port de PS2 se había atrevido a intentarlo. Pero no es magia ni fantasía: es la misma familia de técnicas que ya usan los escaladores modernos (reconstruir una imagen completa apoyándose en *guías* reales del cuadro: profundidad, vectores de movimiento, historial de fotogramas) y que los emuladores de otras consolas aplican desde hace años para estirar resoluciones internas bajísimas. El truco no es "estirar píxeles borrosos": es usar la información geométrica que el propio motor gráfico ya genera para reconstruir una imagen limpia donde el hardware no alcanza para dibujarla nativa.

¿Por qué esto importa en gama baja y media? Porque el costo de dibujar un cuadro a 0.5x es una fracción del de dibujarlo a 1x o 2x: menos cuadros por segundo perdidos, menos calor, menos batería. Si la reconstrucción funciona, un teléfono que hoy no pasa de 25 cuadros a 1x podría mover el mismo juego **más fluido y viéndose mejor que a resolución nativa**. Es exactamente el tipo de salto que un gama baja necesita y que ningún ajuste tradicional le puede dar.

Cómo se va a construir, sin apuro y sin humo:

1. **Desde cero, en versiones de prueba.** Primero como experimento en ramas de desarrollo y builds alternativos, no en la versión estable. Cada paso se mide contra el renderizado actual, y si un paso no mejora, no avanza.
2. **Con la guía que ya existe dentro del motor.** El renderizador gráfico del emulador ya produce los datos que una reconstrucción necesita; el trabajo es capturarlos, moverlos a un pase de posprocesamiento barato en la GPU del celular y evaluar calidad real, cuadro a cuadro, contra el resultado nativo.
3. **Juego por juego, no global.** Habrá títulos donde la reconstrucción se vea excelente y títulos donde no; la memoria por juego que Cenit ya tiene es la base para decidir dónde se activa.
4. **Con honestidad en cada release.** Mientras sea experimental, la opción estará marcada como tal en Ajustes y tendrá un interruptor para volver al renderizado clásico. Nadie va a descubrir una regresión por sorpresa.

Es la meta más ambiciosa del proyecto y la que más tiempo va a llevar. También es la que, de lograrse, más va a cambiar lo que un gama baja puede hacer con una PS2 en el bolsillo.

### Otras líneas de mejora en estudio

- **Caché de sombreadores compartida entre usuarios:** que el segundo jugador de un mismo juego no reconpile desde cero (la técnica ya existe en otros emuladores; el reto es el tamaño y la red).
- **Precisión de mezcla adaptativa:** bajar la emulación de mezcla de color solo en las zonas de la pantalla que no se notan, en vez de un ajuste global que castiga todo.
- **Audio con menos carga:** búferes dinámicos según si el juego va sobrado o ahogado, para recuperar cuadros sin cortes audibles.
- **Perfiles listos por tipo de juego:** un menú simple —"prioridad fluidez", "prioridad imagen", "equilibrado"— que aplique el conjunto correcto de todos los mandos de arriba.

**Mejoras de estabilidad continua:** cada versión pasa por compilación y pruebas automatizadas antes de publicarse.

## Créditos y ascendencia

- **[PCSX2](https://github.com/PCSX2/pcsx2)** — el emulador; todo el mérito del motor es de sus autores.
- **[PCSX2_ARM64](https://github.com/pontos2024/PCSX2_ARM64)** — la compilación nativa para ARM64 sobre la que se apoya el adaptador Android.
- **[PSX2 (izzy2lost)](https://github.com/izzy2lost/PSX2)** — el proyecto Android del que este repositorio es bifurcación directa.

Cenit es una capa propia (identidad, interfaz, motor de rendimiento adaptativo, ajustes por juego y perfiles de hardware) sobre esa base, publicada bajo la misma licencia GPL-3.0 y mantenida por una sola persona, poco a poco. Los problemas del motor son del motor; las ideas de este proyecto, de este fork.

## Aviso legal

Cenit es un proyecto educativo y de uso personal, sin vínculo con Sony Interactive Entertainment, con el equipo de PCSX2 ni con Google. No distribuye BIOS ni juegos: necesitas tu propia consola PS2 de la que extraer la BIOS y tus propias copias de los juegos.
