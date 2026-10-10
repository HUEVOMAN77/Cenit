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
# Requiere JDK 21 (javac/java) en PATH o en /usr/lib/jvm/temurin-21.
set -u
JAVAC=${JAVAC:-/usr/lib/jvm/temurin-21/bin/javac}
JAVA=${JAVA:-/usr/lib/jvm/temurin-21/bin/java}
[ -x "$JAVAC" ] || JAVAC=javac
[ -x "$JAVA" ] || JAVA=java

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../app/src/main/java/com/izzy2lost/psx2" && pwd)
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

ONLY=${ARNES:-all}
case "$ONLY" in
  all|h5|h6|h7|h8) ;;
  # Un nombre mal escrito tiene que ruidar: si no, "corrí los arneses" se convierte
  # en "no corrí ninguno y volvi con RC=0", que es exactamente el verde vacio que
  # este script existe para evitar.
  *) echo "baseline.sh: ARNES='$ONLY' no existe (valores: all h5 h6 h7 h8)"; exit 2 ;;
esac
[ "$ONLY" = all ] || [ "$ONLY" = h5 ] && run h5 Harness5 GameLoadClassifier CenitLoadSample LoadPolicy LoadTrend CenitTelemetry
[ "$ONLY" = all ] || [ "$ONLY" = h6 ] && run h6 Harness6 DynamicResolutionGovernor GameLoadClassifier AdaptiveProfile CenitLoadSample LoadPolicy LoadTrend CenitTelemetry GameSerialUtils
[ "$ONLY" = all ] || [ "$ONLY" = h7 ] && run h7 Harness7 CenitLoadSample LoadPolicy GameLoadClassifier CenitTelemetry LoadTrend
# h8 (0.6.31): el SINTONIZADOR real. Ninguno de los tres de arriba compila
# CpuVUTuner.java — h6 lo sustituye por un stub —, asi que sin h8 "el tuner del repo
# compila" no estaria cubierto por ninguna linea base. TunerProfileStore tampoco.
[ "$ONLY" = all ] || [ "$ONLY" = h8 ] && run h8 Harness8 CpuVUTuner TunerProfileStore AdaptiveProfile GameSerialUtils CenitLoadSample LoadPolicy GameLoadClassifier LoadTrend CenitTelemetry

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
