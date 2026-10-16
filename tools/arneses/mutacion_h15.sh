#!/bin/bash
# Mutation testing de Harness15 (herramienta de VERIFICACION, no de medicion).
#
# Que se prueba aqui, en una linea: 0.6.36 decidia el perfil del telefono con el
# APELLIDO del chipset. La copia fiel de esa ley vive dentro de h15 y se compara contra
# la medida sobre los mismos aparatos; este script rompe, una por una, las piezas de la
# ley nueva para comprobar que el arnes tiene ojos.
#
# FASE 1 — el encabezado puro (CenitDeviceClass.h). Veinticuatro roturas PROPOSITAS:
#   m1..m3 la RETIRADA de clase (quitarla, invertir `>=`/`>`, y retirar también cuando el
#     reloj del desconocido no se leyo — la tercera es la que convierte un permiso cerrado
#     del sysfs en una opinion sobre el hardware),
#   m4..m6 el AGREGADO de clusters (reloj filtrado por clase, nucleos contados sin clase,
#     y la bandera de clase afirmada para las clases que no son la maxima),
#   m7..m12 los SEIS UMBRALES numericos (2 nucleos grandes, 4 nucleos, 1.8 GHz, 2 GHz,
#     3072 MB, 4096 MB),
#   m13..m15 el ORDEN de la cascada (RAM antes que nucleos, frecuencia antes que clase, y
#     la clase media cayendose dentro del paquete de alta) — son los mutantes de
#     TELEMETRIA: el tier final puede seguir siendo el mismo y la razon impresa ser otra,
#     que es exactamente la forma en que un registro puede estar roto sin que truene nada,
#   m16..m17 las CUATRO PROYECCIONES invertidas (`!=` por `==`), que es el defecto de
#     0.6.36 al reves: un Qualcomm de entrada sin protecciones y con la cola optima,
#   m18 la COHERENCIA debanderada (dejar de exigir bandera/centinela en la RAM),
#   m19 el TIER DERIVADO (kFreqOnlyCapable subido a alta: habria dos razones que prometen
#     2x),
#   m20 una ETIQUETA duplicada en el registro,
#   m21..m22 la IMPRESION de los MIDR crudos (ancho de campo y retroceso al ultimo cluster
#     completo: un numero truncado impreso es un numero inventado),
#   m23 la TABLA MIDR perdiendo una fila, m24 la MASCARA del MIDR desplazada.
#   Cada mutante tiene un rojo NOMBRADO y el nombre se exige por texto: un rojo en otra
#   comprobacion significa que el arnes capto el cambio por accidente.
#
# FASE 2 — el cable que host NO puede analizar (AndroidDeviceDetection.cpp tira de
#   cpuinfo/GetPhysicalMemory, native-lib.cpp y ImGuiOverlays.cpp del arbol completo de
#   PCSX2). Aqui se fija el TEXTO, no la conducta: que la frecuencia se lea de
#   cpuinfo_max_freq y no del campo que en este arbol queda sin llenar, que la medida se
#   calcule UNA vez, que las cuatro casillas se pidan por su funcion Wants* y no con un
#   `== 0`/`>= 1` escrito a mano, que la ley vieja no quede como segunda copia (ni
#   IsHighEndSnapdragon ni kCapable[]/kWeak[] ni la cadena "helio"), que el registro lleve
#   los denominadores y la linea de MIDR crudos, que el contrato JNI siga devolviendo
#   0/1/2 y que h15 este en la linea base. Son comprobaciones de TEXTO y se dicen asi, con
#   su limitacion escrita.
#
# Un arnes verde con el codigo saboteado no verifica nada: sin esta lista, los
# «65248 Comprobaciones, 0 Fallos» de Harness15 serian solo un numero grande.
#
# Que NO es: no prueba que un telefono tenga tal topologia (eso lo dice su log), ni que
# el perfil medido se aplique en el telefono (eso lo decide el CI al compilar y el usuario
# al leer `Cenit equipo:`), ni rendimiento.
#
# Uso: bash tools/arneses/mutacion_h15.sh  (RC=0 solo si los VEINTICUATRO mutantes de fase
# 1 cayeron en la comprobacion nombrada y las anclas de fase 2 aparecieron).
# Requiere g++ en el host.
#
# Trampa heredada de h12/h13/h14: la caja se limpia UNA vez y mut() es lo unico que
# escribe el header; el CONTROL (caja sin mutar) corre primero, porque sin el cualquier
# RC=1 contaria como "detectado". Y en fase 1 el rojo se busca en la PRIMERA linea FALLO.
set -u

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
cd "$root" || exit 1

SRC=app/src/main/cpp/CenitDeviceClass.h
ADD=app/src/main/cpp/AndroidDeviceDetection.cpp
ADH=app/src/main/cpp/AndroidDeviceDetection.h
NLB=app/src/main/cpp/native-lib.cpp
HUD=app/src/main/cpp/pcsx2/ImGui/ImGuiOverlays.cpp
MVO=app/src/main/cpp/pcsx2/arm64/microVU-arm64.cpp
CML=app/src/main/cpp/pcsx2/CMakeLists.txt
JAV=app/src/main/java/com/izzy2lost/psx2/NativeApp.java
TEST=tools/arneses/h15-src/dev_test.cpp
BASE=tools/arneses/baseline.sh
box=/tmp/h15mut

for f in "$SRC" "$ADD" "$ADH" "$NLB" "$HUD" "$MVO" "$CML" "$JAV" "$TEST" "$BASE"; do
  [ -f "$f" ] || { echo "mutacion h15: no existe $f (ruta relativa al repo)"; exit 1; }
done
command -v g++ >/dev/null 2>&1 || { echo "mutacion h15: hace falta g++ en el host"; exit 1; }

rm -rf "$box"; mkdir -p "$box"
cp "$TEST" "$box/dev_test.cpp"

undetected=0; total=0

m() { # <nombre> <descripcion> <ancla> <reemplazo> <rojo esperado>
  local name=$1 desc=$2 a=$3 b=$4 exp=$5
  total=$((total+1))
  if ! mut "$a" "$b"; then
    echo "MUTANTE $name ($desc): ANCLA ROTA => NO DETECTADO (el mutante no se aplico)"
    undetected=$((undetected+1)); return
  fi
  if ! g++ -std=c++17 -Wall -Wextra -O1 -I"$box" -o "$box/h15_$name" "$box/dev_test.cpp" 2>"$box/err_$name"; then
    echo "MUTANTE $name ($desc): no compila => DETECTADO en el compile"
    head -3 "$box/err_$name"
    return
  fi
  local out
  out=$("$box/h15_$name" 2>/dev/null | grep -m1 "^FALLO:")
  if [ -z "$out" ]; then
    if "$box/h15_$name" >/dev/null 2>&1; then
      echo "MUTANTE $name ($desc): VERDE con el codigo roto => NO DETECTADO"
    else
      echo "MUTANTE $name ($desc): SIN RECUESTO (RC!=0 sin FALLO) => NO DETECTADO, investigar"
    fi
    undetected=$((undetected+1)); return
  fi
  case "$out" in
    *"$exp"*) echo "MUTANTE $name ($desc): DETECTADO en la comprobacion esperada";;
    *) echo "MUTANTE $name ($desc): DETECTADO pero en otra comprobacion (esperada ~\"$exp\")"
       echo "  rojo real: ${out:0:180}"
       undetected=$((undetected+1));;
  esac
}

mut() {
  python3 -c "
import sys
s=open('$SRC').read()
a,b=sys.argv[1],sys.argv[2]
n=s.count(a)
if n==0:
    sys.stderr.write('ANCLA NO ENCONTRADA: '+repr(a[:70])+'\n'); sys.exit(3)
if n>1:
    sys.stderr.write('ANCLA AMBIGUA (%d apariciones): '%n+repr(a[:70])+'\n'); sys.exit(4)
open('$box/CenitDeviceClass.h','w').write(s.replace(a,b,1))
" "$1" "$2"
}

echo "== mutacion Harness15 / CenitDeviceClass.h (fase 1) =="
echo

cp "$SRC" "$box/CenitDeviceClass.h"
if g++ -std=c++17 -Wall -Wextra -O1 -I"$box" -o "$box/h15_control" "$box/dev_test.cpp" 2>/dev/null \
   && "$box/h15_control" 2>/dev/null | grep -q ", 0 Fallos"; then
  echo "CONTROL (caja sin mutar): verde => un rojo de abajo es del mutante."
else
  echo "CONTROL (caja sin mutar): NO ESTA VERDE => todo lo de abajo no vale nada"
  "$box/h15_control" 2>/dev/null | head -5
  exit 1
fi
echo

# --- mutantes de RETIRADA: la regla que evita inventarse el nucleo grande -------------

# m1: la retirada desaparece. Un 778G que lee sus Kryo como parts que la tabla del arbol
# no conoce se clasificacion con la clase del cluster CHICO (el unico nombrable), o sea
# "nucleo-chico, gama baja" sobre un A78.
m m1 'la retirada de clase se va' \
  '		if (freq_unknown > 0u && freq_unknown >= freq_known)
			best = BigClass::kUnknown;' \
  '		if (false)
			best = BigClass::kUnknown;' \
  'Kryo sin tabla: NO se puede afirmar clase chica'

# m2: `>=` por `>`: con empate a reloj la afirmacion se queda, y ahi afirmar es un 50/50.
m m2 'retirada: >= se vuelve > (el empate se queda afirmado)' \
  '		if (freq_unknown > 0u && freq_unknown >= freq_known)' \
  '		if (freq_unknown > 0u && freq_unknown > freq_known)' \
  'empate a reloj con un cluster no nombrable: se retira'

# m3: se quita el `freq_unknown > 0u`: un desconocido SIN reloj (0 kHz, sysfs cerrado)
# cumple `0 >= freq_known`... no, pero con dos clusters sin reloj cumple `0 >= 0` y retira
# una clase si se leyo. Es convertir un permiso en un dato del hardware.
m m3 'retirada sin exigir que el desconocido tenga reloj' \
  '		if (freq_unknown > 0u && freq_unknown >= freq_known)' \
  '		if (freq_unknown >= freq_known)' \
  'sin reloj en NINGUN cluster: la clase leida no se retira'

# --- mutantes del AGREGADO: las dos asimetrias de la segunda pasada -------------------

# m4: el reloj se filtra por la clase reclamada. `big_freq_khz` pasa a 0 cuando la clase no
# se afirmo, y la casilla "solo-frecuencia-alta" se vuelve inalcanzable: el telefono cae a
# nucleo-desconocido TENIENDO 2.4 GHz leidos.
m m4 'el reloj deja de agregarse sobre todos los clusters' \
  '			if (c.frequency_khz > freq)
				freq = c.frequency_khz;

			if (!out.class_measured)
				continue;' \
  '			if (!out.class_measured)
				continue;

			if (c.frequency_khz > freq)
				freq = c.frequency_khz;' \
  'el reloj se agrega sobre TODOS los clusters'

# m5: los nucleos grandes se suman aunque la clase no este afirmada: "grandes=4" impreso
# como si fuera una medida, sobre clusters que la tabla no sabe que clase son.
m m5 'los nucleos grandes se cuentan sin clase' \
  '			if (!out.class_measured)
				continue;

			if (ClassOfCluster(c) != best)' \
  '			if (false)
				continue;

			if (ClassOfCluster(c) != best)' \
  'sin clase no se sabe cuales son los grandes: 0, no 8'

# m6: la bandera de clase se afirma solo para rendimiento: un A75 o un A55 medidos quedan
# "sin clase" y el telefono se lee desconocido teniendo el registro delante.
m m6 'class_measured se restringe a la clase maxima del techo' \
  '		out.class_measured = (best != BigClass::kUnknown);' \
  '		out.class_measured = (best == BigClass::kPerformance);' \
  'desconocido sin reloj leido: no se retira la clase'

# --- mutantes de UMBRAL: los seis numeros de la ley -----------------------------------

m m7 'piso de nucleos grandes 2 -> 1' \
  '	static const uint8_t kBigCoresFloorHigh = 2u;' \
  '	static const uint8_t kBigCoresFloorHigh = 1u;' \
  'un solo nucleo de rendimiento: grande-pero-corto'

m m8 'piso de nucleos totales 4 -> 3' \
  '	static const uint8_t kTotalCoresFloor = 4u;' \
  '	static const uint8_t kTotalCoresFloor = 3u;' \
  'tres nucleos: pocos-nucleos'

# m9: el rojo real sale ANTES de lo que este script esperaba — en el fixture del Dimensity
# 720 (2x A76 a 2.00 GHz exactos), que corre en la prueba de agregado y no en la de bordes
# de la cascada. La expectativa se corrijo contra el rojo real, no al reves: lo que se
# quiere fijar es que el piso de 2 GHz es un NUMERO que un telefono puede traer clavado, y
# eso lo rompe primero la topologia de ficha publica.
m m9 'piso de frecuencia para 2x +1 kHz' \
  '	static const uint32_t kFreqHighKhz = 2000000u;' \
  '	static const uint32_t kFreqHighKhz = 2000001u;' \
  'A76 justo a 2.00 GHz sigue siendo A76'

m m10 'piso de frecuencia aprovechable -1 kHz' \
  '	static const uint32_t kFreqWeakKhz = 1800000u;' \
  '	static const uint32_t kFreqWeakKhz = 1799999u;' \
  '1799999 kHz: frecuencia-baja'

m m11 'corte de RAM floja 3072 -> 3071' \
  '	static const uint32_t kRamTightMb = 3072u;' \
  '	static const uint32_t kRamTightMb = 3071u;' \
  '3071 MB: poca-ram'

m m12 'piso de RAM para 2x 4096 -> 3072' \
  '	static const uint32_t kRamFloorHighMb = 4096u;' \
  '	static const uint32_t kRamFloorHighMb = 3072u;' \
  'G99 con 3 GB: NO alta (la RAM manda)'

# --- mutantes de ORDEN: mismo tier, otra razon impresa ----------------------------------

# m13: la RAM se mira antes que los nucleos. Con dos impedimentos el log pasa de
# "pocos-nucleos" a "poca-ram": el tier no cambia, la explicacion si.
m m13 'orden: RAM antes que nucleos' \
  '		if (m.total_cores < kTotalCoresFloor)
			return Reason::kFewCores;' \
  '		if (m.ram_measured && m.total_ram_mb < kRamTightMb)
			return Reason::kRamTight;
		if (m.total_cores < kTotalCoresFloor)
			return Reason::kFewCores;
		if (false)
			return Reason::kRamTight;' \
  'pocos-nucleos manda sobre todo lo demas'

# m14: la frecuencia ilegible se mira antes que la clase chica. Mismo tier (baja/media),
# otra casilla del registro.
m m14 'orden: frecuencia antes que clase' \
  '		if (m.big_class == BigClass::kWeak)
			return Reason::kBigClassWeak;' \
  '		if (!m.big_freq_measured)
			return Reason::kBigFreqUnmeasured;
		if (m.big_class == BigClass::kWeak)
			return Reason::kBigClassWeak;
		if (false)
			return Reason::kBigFreqUnmeasured;' \
  'nucleo-chico manda sobre frecuencia-no-leida'

# m15: la clase media se resuelve dentro del paquete de alta. Un A75 con 4 GB pasa de
# nucleo-medio a grande-pero-corto: la razon sigue siendo media pero con otro nombre.
m m15 'la clase media cae dentro del paquete de alta' \
  '		if (m.big_class == BigClass::kMainstream)
			return Reason::kMainstreamCapable;' \
  '		if (false)
			return Reason::kMainstreamCapable;' \
  'G88 con 4 GB: media por clase, no por nombre'

# --- mutantes de PROYECCION: el defecto de 0.6.36, al reves ----------------------------

# m16: la cola optima se aplica cuando NO hay alta. Es literalmente lo que la ley vieja
# hacia con todo Qualcomm por apellido, incluyendo el de entrada.
m m16 'WantsOptimalPacing invierte su condicion' \
  '	inline bool WantsOptimalPacing(Reason r)
	{
		return TierOf(r) == Tier::kHigh;' \
  '	inline bool WantsOptimalPacing(Reason r)
	{
		return TierOf(r) != Tier::kHigh;' \
  'pacing y tier se contradicen'

# m17: las protecciones se aplican cuando SI hay alta. La otra mitad de la misma inversion.
m m17 'WantsFastSavestate invierte su condicion' \
  '	inline bool WantsFastSavestate(Reason r)
	{
		return TierOf(r) != Tier::kHigh;' \
  '	inline bool WantsFastSavestate(Reason r)
	{
		return TierOf(r) == Tier::kHigh;' \
  'protecciones y tier se contradicen'

# --- mutante de COHERENCIA: la bandera y su numero, desacoplados ------------------------

# m18: Coherent deja de exigir que ram_measured y total_ram_mb digan lo mismo. Vuelve el
# centinela-vuelto-dato (ram=0 impreso como si el telefono tuviera 0 MB), y la linea
# `coherente=si` del registro pasa a ser decoracion.
m m18 'Coherent suelta la bandera de la RAM' \
  '		if (m.ram_measured != (m.total_ram_mb > 0u))
			return false;' \
  '		if (false)
			return false;' \
  'ram_medido con ram=0 es el centinela'

# --- mutante del TIER DERIVADO: una segunda puerta a 2x ---------------------------------

# m19: kFreqOnlyCapable sube a alta. Habria DOS razones que prometen 2x, y una de ellas es
# "no se que nucleo es pero el reloj es alto" — justo lo que la ley se nego a prometer.
m m19 'solo-frecuencia-alta pasa a Tier::kHigh' \
  '			case Reason::kFreqOnlyCapable:
				return Tier::kMid;' \
  '			case Reason::kFreqOnlyCapable:
				return Tier::kHigh;' \
  'desconocido con reloj alto: media, nunca alta'

# --- mutante de ETIQUETA: la decision esta bien, el log dice otra cosa ------------------

m m20 'dos razones comparten nombre en el registro' \
  '			case Reason::kRamTight: return "poca-ram";' \
  '			case Reason::kRamTight: return "ram-no-leida";' \
  'dos razones comparten nombre en el registro'

# --- mutantes de IMPRESION: el numero crudo que hace mantenible lo "universal" ----------

# m21: se pierde el ancho fijo. `midr=0x4100d410` se imprime sin los ceros delante y la
# fila deja de ser comparable con la tabla: extender la tabla contra ese numero es
# copiar un numero mal leido.
m m21 'FormatClusters pierde el ancho del MIDR' \
  '"%s#%u midr=0x%08x n=%u khz=%u clase=%s"' \
  '"%s#%u midr=0x%x n=%u khz=%u clase=%s"' \
  'registro sin leer y reloj sin leer se imprimen como lo que son'

# m22: el retroceso al ultimo cluster COMPLETO se afloja de `>=` a `>`. Con hueco JUSTO para
# el separador mas el cluster (55 bytes para 55 caracteres), snprintf escribe 54 y cierra con
# NUL: con `>` eso se canta como que si cabia y el registro sale con un MIDR partido a la
# mitad. Un numero truncado impreso es un numero inventado.
m m22 'FormatClusters imprime un cluster truncado' \
  '			if (need >= cap - used)
			{
				// snprintf ya escribio el trozo que cabia y lo termino en NUL: se retrocede el' \
  '			if (need > cap - used)
			{
				// snprintf ya escribio el trozo que cabia y lo termino en NUL: se retrocede el' \
  'hueco justo para el separador mas el cluster: no entra, se declara recorte'

# --- mutantes de TABLA/MASCARA: la identidad del nucleo ---------------------------------

# m23: la tabla deja de conocer el Cortex-A320. Un sucesor del A55 que se leeria
# "desconocido" cambia la clasificacion de un telefono; la fila de kTabla es la que
# recuerda que este header se copia de uarch.c y no de la memoria.
m m23 'la tabla pierde Cortex-A320' \
  '					case 0xd8f: // Cortex-A320           (cortex_a320)
' \
  '					// 0xd8f retirado de la tabla
' \
  'Cortex-A320'

# m24: la mascara del MIDR se desplaza. Un A72 r0p3 deja de dar su part, o sea la tabla
# se consulta contra un numero que ningun registro produce.
m m24 'MidrPart usa el desplazamiento equivocado' \
  '	inline uint32_t MidrPart(uint32_t midr) { return (midr >> 4) & 0xfffu; }' \
  '	inline uint32_t MidrPart(uint32_t midr) { return (midr >> 8) & 0xfffu; }' \
  'part de un A72 r0p3'

echo
echo "== fase 2: el cable (AndroidDeviceDetection / native-lib / HUD / CMake / Java) =="
echo
# Cada ancla se dice con su limitacion: esto no prueba que el telefono lea bien /sys ni
# que las cuatro casillas se apliquen — prueba que el TEXTO del cable sigue siendo el que
# Harness15 pinó. Lo que el host NO puede comprobar lo decide el CI al compilar y el log
# al imprimir `Cenit equipo:`.
anchors=0; anclas_fallan=0

ancla() { # <nombre> <archivo> <cadena> <por que importa>
  total=$((total+1))
  if grep -qF "$3" "$2"; then
    echo "ANCLA $1: presente"
    anchors=$((anchors+1))
  else
    echo "ANCLA $1: NO ENCONTRADA => $4"
    anclas_fallan=$((anclas_fallan+1))
  fi
}

ancla_absente() { # <nombre> <archivo> <regex> <por que importa>
  total=$((total+1))
  if grep -qE "$3" "$2"; then
    echo "ANCLA $1: todavia VIVA => $4"
    anclas_fallan=$((anclas_fallan+1))
  else
    echo "ANCLA $1: muerta como debe (nada coincide con /$3/)"
    anchors=$((anchors+1))
  fi
}

ancla_codigo() { # <nombre> <archivo> <regex> <por que importa>  (busca SIN los comentarios)
  total=$((total+1))
  if python3 -c "
import re,sys
s=open('$2').read()
s=re.sub(r'/\*.*?\*/','',s,flags=re.S)
s=re.sub(r'(?m)//.*$','',s)
sys.exit(0 if re.search(r'''$3''',s) else 1)
"; then
    echo "ANCLA $1: VIVA en el codigo => $4"
    anclas_fallan=$((anclas_fallan+1))
  else
    echo "ANCLA $1: ausente del codigo (los comentarios que la nombran no cuentan)"
    anchors=$((anchors+1))
  fi
}

cuenta() { # <nombre> <archivo> <cadena> <n> <por que importa>
  total=$((total+1))
  local n
  n=$(grep -cF "$3" "$2")
  if [ "$n" = "$4" ]; then
    echo "ANCLA $1: $n apariciones, exactamente las esperadas"
    anchors=$((anchors+1))
  else
    echo "ANCLA $1: $n apariciones (esperadas $4) => $5"
    anclas_fallan=$((anclas_fallan+1))
  fi
}

# 1-2: el recolector lee cpuinfo_max_freq y NUNCA el campo del arbol. Este es el punto
# exacto donde 0.6.37 podia haberse equivocado sin enterarse: en linux/init.c el literal
# del cluster no inicializa .frequency, asi que fiarse de cpuinfo_cluster::frequency da 0
# kHz en TODOS los telefonos y — con la bandera encendida — un "0 kHz" impreso como medida.
ancla f1-lee-cpuinfo-max-freq "$ADD" '/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq' \
  'la frecuencia dejo de leerse del sysfs: el perfil medido se apoya en un campo sin llenar'
ancla_absente f2-no-usa-cluster-frequency-asignado "$ADD" 'frequency[[:space:]]*=[[:space:]]*(c|cluster)->frequency' \
  'alguien volvio a fiarse de cpuinfo_cluster::frequency, que en este arbol queda en 0'

# 3: la medida se calcula UNA vez (magic static). microVU llama a GetDeviceTier() al
# calcular el sentinel de opciones de un programa VU1, o sea en el hilo del VU: un fopen
# por programa seria un costo en el camino caliente.
ancla f3-magic-static "$ADD" 'static const Measured value = BuildMeasured();' \
  'la clasificacion volvio a calcularse por llamada: fopen en el hilo del VU'

# 4-7: las cuatro casillas se piden por su funcion, y el sitio de aplicacion no conserva
# una copia de la condicion. En 0.6.36 las cuatro se escribian a mano (`GetDeviceTier() ==
# 0` y `tier >= 1`) y asi un Snapdragon de entrada se quedaba sin protecciones y con la
# cola optima encendida.
cuenta f4-cuatro-llamadas-a-wants "$NLB" 'AndroidDeviceDetection::DeviceWants' 4 \
  'una de las cuatro casillas volvio a decidir su condicion en el sitio de aplicacion'
ancla_codigo f5-ni-igual-cero-ni-mas-uno-en-native-lib "$NLB" 'GetDeviceTier\(\) *== *0|tier *>= *1' \
  'la condicion vieja vive otra vez en el CODIGO de native-lib.cpp: dos copias de la ley'
ancla f6-razon-en-la-linea-de-perfil "$NLB" 'perfil medido=%s razon=%s' \
  'el registro de arranque dejo de decir POR QUE salio ese perfil'
cuenta f7-cuatro-delegaciones-en-el-recolector "$ADD" 'return Wants' 4 \
  'una proyeccion del header perdio su delegacion (native-lib no la puede pedir)'

# 8-10: la ley vieja NO queda como segunda copia. Es la leccion de h14: dos copias de una
# decision siempre acaban diciendo cosas distintas. Aqui se exige ausencia, no presencia.
ancla_absente f8-is-high-end-fuera-del-recolector "$ADD" '^[[:space:]]*(static )?(bool|int) IsHighEndSnapdragon' \
  'IsHighEndSnapdragon resucito: volveria a decidir el perfil por numero de modelo'
ancla_absente f9-listas-de-nombres-fuera "$ADD" 'static const char\* const k(Capable|Weak)\[\]' \
  'volvio una lista de nombres de SoC: la clasificacion dejaria de ser medida'
ancla_absente f10-cadena-helio-fuera "$ADD" '"helio",' \
  'la cadena suelta "helio" arrastra otra vez a toda la serie G por el apellido'
ancla_absente f11-header-sin-nombres-de-vendedor "$SRC" '"(snapdragon|helio|dimensity|exynos|kirin)",' \
  'CenitDeviceClass.h empezo a comparar nombres: la tabla MIDR dejaria de ser la unica ley'

# 11: el GetDeviceTier que ve Java sigue derivando de la MISMA razon medida (el contrato
# 0/1/2 no cambia; lo que cambia es de donde sale el numero).
ancla f12-tier-derivado-de-la-razon "$ADD" 'return static_cast<int>(TierOf(GetMeasured().r));' \
  'GetDeviceTier volvio a tener su propia decision, separada de la del registro'
cuenta f13-microvu-sigue-pidiendo-el-tier "$MVO" 'AndroidDeviceDetection::GetDeviceTier()' 1 \
  'microVU perdio su lectura del tier (el sentinel de opciones de un programa VU1)'
ancla f14-contrato-jni-intacto "$JAV" 'public static native int getDevicePerformanceTier();' \
  'la firma JNI se movio: el telefono revienta en tiempo de enlace, no al compilar'

# 15-18: el registro. Cuatro lineas nuevas, cada una con su denominador explicito; sin
# ellas un perfil conservador se lee como una opinion sobre el hardware.
ancla f15-sin-medir-en-el-registro "$ADD" 'sin-medir=%u/4 coherente=%s' \
  'el registro dejo de decir cuantos campos no se pudieron leer'
ancla f16-ruido-de-incoherencia "$ADD" 'Cenit equipo: INCOHERENCIA entre la razon y los datos medidos ' \
  'una ley y unos datos contradictorios volverian a imprimirse sin ruido'
ancla f17-la-linea-de-midr-crudos "$ADD" 'Cenit equipo: midr %s' \
  'sin los MIDR crudos, un clase=desconocida no se puede atribuir a un part'
ancla f18-denominadores-de-clusters "$ADD" 'Cenit equipo: clusters usados=%u/%u impresos=%u recortado=%s' \
  'el recorte de clusters vuelve a ser silencioso'

# 19: el HUD. Con los centinelas -reloj/-ram escritos: el 0 de un campo sin medir nunca
# tiene que verse como un dato del telefono.
ancla f19-fila-del-hud "$HUD" 'Equipo={} ({}) nuc={}/{} reloj={} ram={} sin-medir={}' \
  'el HUD dejo de imprimir la medida con la que se esta corriendo'
ancla f20-centinela-de-reloj-en-el-hud "$HUD" '-reloj' \
  'el HUD volveria a imprimir 0 MHz cuando el sysfs no dijo nada'

# 21: el alta de la ley en CMake, para que el header se vea/abra como las demas piezas.
ancla f21-cmake "$CML" '../CenitDeviceClass.h' \
  'CenitDeviceClass.h no esta dado de alta: nadie lo ve salvo quien abra el .cpp'

# 22: h15 esta en la linea base. Sin esto, "corri los arneses" podria seguir queriendo
# decir "no corri el arnes nuevo".
ancla f22-h15-en-la-base "$BASE" '[ "$ONLY" = all ] || [ "$ONLY" = h15 ] && runh15' \
  'h15 no esta en la linea base: su verde no se obtiene corriendo baseline.sh'
ancla f23-h15-en-el-case "$BASE" 'all|h5|h6|h7|h8|h9|h10|h11|h12|h13|h14|h15) ;;' \
  'el case de ARNES no conoce h15: "baseline.sh h15" saldria con exito 2... o peor, con 0'

echo
echo "Limitacion escrita de esta fase: las anclas son de TEXTO, no de compilacion ni de"
echo "conducta. Ninguna prueba que el telefono pueda abrir cpuinfo_max_freq (muchos OEM lo"
echo "cierran: por eso existe la casilla frecuencia-no-leida), ni que las cuatro casillas"
echo "lleguen a su ajuste, ni que el HUD se dibuje. Eso lo dice el CI al compilar y el log"
echo "al imprimir 'Cenit equipo: perfil=... razon=...' con sus banderas."
echo
echo "Total: $total casos (24 mutantes + $anchors anclas), no detectados: $undetected, anclas rotas: $anclas_fallan"
if [ "$undetected" -eq 0 ] && [ "$anclas_fallan" -eq 0 ]; then
  echo "RC=0: los veinticuatro mutantes cayeron en su comprobacion nombrada y las anclas del cable estan."
  exit 0
fi
exit 1
