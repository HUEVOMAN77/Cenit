#!/bin/bash
# Linea base de arneses (Cenit). Versionada en el repo para que el ritual del prompt
# —"correr los arneses actuales" antes de cerrar cada fase— se pueda hacer en otro
# sitio y en otra sesion, no solo en la /tmp de quien escribio el codigo.
#
# Como funciona: cada arnes tiene AQUI su stub/arnes propio (HarnessN.java y los
# android/*.java de juguete, mas los mocks de NativeApp). Las CLASES DEL REPO no se
# versionan dentro del arnes: se copian de app/src/main/java en cada corrida. Si un
# arnes necesitara una clase que el repo ya no tiene, la copia falla visiblemente en
# vez de aprobarse contra una copia vieja. Y si una copia del arnes NO coincide con
# el repo (alguien la edito a mano), se avisa: un verde con copia desincronizada es
# la peor manera de aprobar una version.
#
# Uso:  bash tools/arneses/baseline.sh          (todas)
#       ARNES=h8 bash tools/arneses/baseline.sh (una)
# Requiere JDK 21 (javac/java) en PATH o en /usr/lib/jvm/temurin-21, y g++ de host
# para h9 (el arnes de la maquina de estados del superbloque es C++ puro).
set -u
JAVAC=${JAVAC:-/usr/lib/jvm/temurin-21/bin/javac}
JAVA=${JAVA:-/usr/lib/jvm/temurin-21/bin/java}
[ -x "$JAVAC" ] || JAVAC=javac
[ -x "$JAVA" ] || JAVA=java
CXX=${CXX:-g++}
command -v "$CXX" >/dev/null 2>&1 || CXX=

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../app/src/main/java/com/izzy2lost/psx2" && pwd)
CPP=$(cd "$HERE/../../app/src/main/cpp/pcsx2/arm64" && pwd)
WORK=${WORK:-/tmp/cenit-arneses}
RC=0

run() { # dir-arnes main clases-del-repo-a-sincronizar...
  local d=$1 main=$2; shift 2
  local box=$WORK/$d
  rm -rf "$box"; mkdir -p "$box/src" "$box/build"
  cp -r "$HERE/$d-src/." "$box/src/" || { echo "$d: no se pudo traer el arnes"; RC=1; return; }
  for c in "$@"; do
    if [ ! -f "$SRC/$c.java" ]; then echo "$d: FALTA $c.java en el repo"; RC=1; return 1; fi
    cp "$SRC/$c.java" "$box/src/com/izzy2lost/psx2/" || { echo "$d: no se pudo copiar $c"; RC=1; return 1; }
  done
  if ! "$JAVAC" -d "$box/build" $(find "$box/src" -name "*.java") 2>"$box/compile.err"; then
    echo "$d: FALLO DE COMPILACION"; head -40 "$box/compile.err"; RC=1; return 1
  fi
  OUT=$("$JAVA" -cp "$box/build" "com.izzy2lost.psx2.$main" 2>/dev/null | grep -E "Comprobaciones|FALLO")
  echo "$OUT"
  echo "$OUT" | grep -qE ", [1-9][0-9]* Fallos" && RC=1
  echo "$OUT" | grep -q "Comprobaciones" || { echo "$d: el arnes no imprime recuento"; RC=1; }
}

runh9() { # arnes C++ puro de host: la maquina de estados del superbloque
  local box=$WORK/h9
  if [ -z "$CXX" ]; then
    # Ausencia de g++ NO es un verde: es una linea base que no corro. Lo grito y
    # dejo RC=1, porque "los arneses pasaron" sin h9 seria exactamente el verde
    # vacio que este script existe para evitar.
    echo "h9: SIN COMPILAR - no hay g++ de host (CXX='$CXX'); la maquina de estados del superbloque NO se verifico en esta corrida"
    RC=1
    return
  fi
  rm -rf "$box"; mkdir -p "$box"
  cp "$HERE/h9-src/sb_life_test.cpp" "$box/" || { echo "h9: no se pudo traer el arnes"; RC=1; return; }
  # Misma regla anti-copia-vieja que los arneses de Java: la maquina se trae DEL
  # REPO en cada corrida, nunca del arnes.
  if [ ! -f "$CPP/MvuSbLife.h" ]; then echo "h9: FALTA MvuSbLife.h en el repo ($CPP)"; RC=1; return; fi
  cp "$CPP/MvuSbLife.h" "$box/" || { echo "h9: no se pudo copiar MvuSbLife.h"; RC=1; return; }
  if ! "$CXX" -std=c++17 -Wall -Wextra -I"$box" -o "$box/h9" "$box/sb_life_test.cpp" 2>"$box/compile.err"; then
    echo "h9: FALLO DE COMPILACION"; head -40 "$box/compile.err"; RC=1; return
  fi
  OUT=$("$box/h9" 2>/dev/null | grep -E "Comprobaciones|FALLO")
  echo "$OUT"
  echo "$OUT" | grep -qE ", [1-9][0-9]* Fallos" && RC=1
  echo "$OUT" | grep -q "Comprobaciones" || { echo "h9: el arnes no imprime recuento"; RC=1; }
}

ONLY=${ARNES:-all}
case "$ONLY" in
  all|h5|h6|h7|h8|h9) ;;
  # Un nombre mal escrito tiene que ruidar: si no, "corrí los arneses" se convierte
  # en "no corrí ninguno y volvi con RC=0", que es exactamente el verde vacio que
  # este script existe para evitar.
  *) echo "baseline.sh: ARNES='$ONLY' no existe (valores: all h5 h6 h7 h8 h9)"; exit 2 ;;
esac
[ "$ONLY" = all ] || [ "$ONLY" = h5 ] && run h5 Harness5 GameLoadClassifier CenitLoadSample LoadPolicy LoadTrend CenitTelemetry
[ "$ONLY" = all ] || [ "$ONLY" = h6 ] && run h6 Harness6 DynamicResolutionGovernor GameLoadClassifier AdaptiveProfile CenitLoadSample LoadPolicy LoadTrend CenitTelemetry GameSerialUtils
[ "$ONLY" = all ] || [ "$ONLY" = h7 ] && run h7 Harness7 CenitLoadSample LoadPolicy GameLoadClassifier CenitTelemetry LoadTrend
# h8 (0.6.31): el SINTONIZADOR real. Ninguno de los tres de arriba compila
# CpuVUTuner.java — h6 lo sustituye por un stub —, asi que sin h8 "el tuner del repo
# compila" no estaria cubierto por ninguna linea base. TunerProfileStore tampoco.
[ "$ONLY" = all ] || [ "$ONLY" = h8 ] && run h8 Harness8 CpuVUTuner TunerProfileStore AdaptiveProfile GameSerialUtils CenitLoadSample LoadPolicy GameLoadClassifier LoadTrend CenitTelemetry
# h9 (0.6.32): LA MAQUINA por candidato del superbloque VU1. Es C++ puro y vive en
# el arbol del nativo, asi que NINGUNO de los arneses de Java la cubre — y el CI no
# la puede cubrir tampoco: CI compila para arm64 con el toolchain de Android, y lo
# que se quiere verificar aqui es la LOGICA de estados/promocion, no la emision.
# Por eso se compila con g++ de host contra el header real del repo.
[ "$ONLY" = all ] || [ "$ONLY" = h9 ] && runh9

# Verificadores que no necesitan javac: las tablas que cruzan la frontera JNI.
# jnisig: cada `native` de NativeApp.java tiene su Java_...() con el mismo numero de
# argumentos (un desajuste NO rompe CI: revienta en el telefono). jnidict: los
# diccionarios de estados/modos dicen lo mismo en Java, en el HUD C++ y en arrays.xml.
[ "$ONLY" = all ] && {
  python3 "$HERE/jnisig.py"  | tail -2
  python3 "$HERE/jnidict.py" | tail -1 || RC=1
  python3 "$HERE/jnisig.py"  | grep -q HAY && RC=1
}

echo "baseline.sh: RC=$RC"
exit $RC
