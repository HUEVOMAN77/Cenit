# CENIT — Emulador de PlayStation 2 para Android

[![Licencia: GPL v3](https://img.shields.io/badge/Licencia-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Android](https://img.shields.io/badge/Android-8.0%20o%20superior-green.svg)](https://developer.android.com/)
[![ARM64](https://img.shields.io/badge/Procesador-arm64--v8a-orange.svg)](https://developer.arm.com/)
[![Última versión](https://img.shields.io/badge/versión%20actual-0.6.6-informational)](https://github.com/HUEVOMAN77/Cenit/releases/tag/base-0.6.6)

**Cenit** es un emulador de PlayStation 2 para celulares Android, hecho como proyecto independiente a partir de [PSX2](https://github.com/izzy2lost/PSX2) (el adaptador Android de PCSX2 2.7). Lo desarrolla una sola persona, pieza por pieza y en público, con una meta muy concreta: **que los juegos de PS2 se muevan fluidos en celulares de gama baja y media, que es donde ningún otro proyecto mira.**

No es una copia con otro logo. Cada versión suma mejoras de rendimiento y de uso que no existen ni en PSX2 ni en PCSX2, medidas sobre celulares reales.

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

- **Perfil automático por hardware.** Al instalarlo reconoce el procesador (incluidos los apodos internos de Qualcomm que muchas marcas no reportan bien) y deja lista la línea base: aceleradores de CPU seguros, recompilación completa, sin esperas inútiles a la gráfica y registros silenciados. No hay que tocar nada: arranca ya configurado para tu gama.
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
- **Packs de texturas.** Importa y gestiona paquetes de texturas de alta resolución por juego, con carga asíncrona y precarga opcionales.
- **Arreglos automáticos de compatibilidad.** El motor aplica las correcciones conocidas para cada juego, y ninguna función de Cenit las rompe al tocar ajustes manuales.
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
4. Consejo: la primera semana activa el mostrador de velocidad en pantalla y verás qué está haciendo el motor por ti.

## Por qué la firma es de prueba

Cenit es un proyecto personal, sin cuenta de desarrollador ni intención de tienda. La firma de prueba es lo honesto para un proyecto comunitario: la APK se instala directa y no suplanta ninguna firma oficial. Si eso cambia algún día, se anunciará aquí y en cada lanzamiento.

## Lo que viene

En orden de impacto medido:

- **Más ajustes por juego con criterio propio:** lectura de texturas dentro de la gráfica (el de mayor salto en juegos con agua y reflejos), escalado nativo de sprites y salto de dibujos, todos con la evidencia que ya juntan los perfiles de memoria como guía.
- **Tabla de rendimiento de la comunidad:** que lo que Cenit aprende en cada teléfono (escala sostenida, tipo de cuello de botella) se convierta en una configuración sugerida por juego, compartida entre usuarios.
- **Celular más frío, menos recortes:** colaboración con el sistema de energía de Android para que el procesador suba frecuencia *antes* del pico en lugar de recortarla después.
- **Menos recortes en video y escenas:** que la resolución dinámica no baje durante cinemáticas ni cortes de escena.
- **Mejoras de estabilidad continua:** cada versión pasa por compilación y pruebas automatizadas antes de publicarse.

## Créditos y ascendencia

- **[PCSX2](https://github.com/PCSX2/pcsx2)** — el emulador; todo el mérito del motor es de sus autores.
- **[PCSX2_ARM64](https://github.com/pontos2024/PCSX2_ARM64)** — la compilación nativa para ARM64 sobre la que se apoya el adaptador Android.
- **[PSX2 (izzy2lost)](https://github.com/izzy2lost/PSX2)** — el proyecto Android del que este repositorio es bifurcación directa.

Cenit es una capa propia (identidad, interfaz, motor de rendimiento adaptativo, ajustes por juego y perfiles de hardware) sobre esa base, publicada bajo la misma licencia GPL-3.0 y mantenida por una sola persona, poco a poco. Los problemas del motor son del motor; las ideas de este proyecto, de este fork.

## Aviso legal

Cenit es un proyecto educativo y de uso personal, sin vínculo con Sony Interactive Entertainment, con el equipo de PCSX2 ni con Google. No distribuye BIOS ni juegos: necesitas tu propia consola PS2 de la que extraer la BIOS y tus propias copias de los juegos.
