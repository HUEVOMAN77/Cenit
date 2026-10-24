#!/usr/bin/env bash
# tools/medicion/perfil_android.sh — perfil de una sesion de juego en el telefono.
#
# Uso:
#   tools/medicion/perfil_android.sh [segundos] [dir_salida]
#
# Requiere: telefono conectado por adb, el juego corriendo en pantalla, y un APK
# instalado con simbolos (gradle -PcenitEdge -PcenitProfile). Sin simbolos el
# reporte sale con direcciones y no sirve para decidir nada.
#
# Que junta, y por que:
#   - simpleperf del proceso completo (todos los hilos): donde se van los ms.
#   - PerfLog del emulog: fps/EE/GS/VU/GPU y sincronia MTVU (wait/exec). Es la
#     unica fuente que dice si el EE esperaba al VU1 o el VU1 trabajaba.
#   - termico, frecuencias y gpubusy: sin esto no se puede distinguir "el juego
#     pide mas de lo que hay" de "el telefono esta throttlando".
#
# No cambia nada en el telefono: solo lee y graba en /data/local/tmp.
set -u
export MSYS_NO_PATHCONV=1

PKG=${PKG:-com.huevoman77.cenit}
SECS=${1:-30}
STAMP=$(date +%Y%m%d_%H%M%S)
OUT=${2:-"$(cd "$(dirname "$0")/../.." && pwd)/perfil_$STAMP"}

die() { echo "ERROR: $*" >&2; exit 1; }

command -v adb >/dev/null 2>&1 || die "adb no esta en el PATH"
[ "$(adb get-state 2>/dev/null | tr -d '\r')" = "device" ] || die "no hay telefono conectado"

PID=$(adb shell pidof "$PKG" 2>/dev/null | tr -d '\r')
[ -n "$PID" ] || die "$PKG no esta corriendo; abri el juego y volve a correr esto"

NDK_ROOT=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}/ndk
SP=$(ls -1 "$NDK_ROOT"/*/simpleperf/bin/android/arm64/simpleperf 2>/dev/null | sort -V | tail -1)
[ -n "$SP" ] && [ -f "$SP" ] || die "no encontre simpleperf en $NDK_ROOT"

mkdir -p "$OUT" || die "no pude crear $OUT"
echo "== Cenit perfil: pid $PID, ${SECS}s, salida $OUT"

# 1) simpleperf (se sube una vez; si ya esta, se reusa)
if ! adb shell "test -x /data/local/tmp/simpleperf" >/dev/null 2>&1; then
	adb push "$SP" /data/local/tmp/simpleperf >/dev/null || die "no pude subir simpleperf"
	adb shell chmod 755 /data/local/tmp/simpleperf
fi

# 2) grabacion: cpu-clock a 500 Hz con pila en cada muestra
adb shell "/data/local/tmp/simpleperf record -p $PID -e cpu-clock -f 500 -g \
	--duration $SECS -o /data/local/tmp/cenit_perf.data" 2>&1 | tail -5

adb pull /data/local/tmp/cenit_perf.data "$OUT/perf.data" >/dev/null 2>&1 \
	|| die "no pude bajar perf.data"

# 3) reportes: por funcion, por biblioteca y por hilo
adb shell "/data/local/tmp/simpleperf report -i /data/local/tmp/cenit_perf.data \
	--sort symbol --percent-limit 0.3 -n" > "$OUT/por_funcion.txt" 2>/dev/null
adb shell "/data/local/tmp/simpleperf report -i /data/local/tmp/cenit_perf.data \
	--sort dso -n" > "$OUT/por_biblioteca.txt" 2>/dev/null
adb shell "/data/local/tmp/simpleperf report -i /data/local/tmp/cenit_perf.data \
	--sort thread --percent-limit 0.5 -n" > "$OUT/por_hilo.txt" 2>/dev/null

# 4) contexto del aparato
{
	echo "== modelo";            adb shell getprop ro.product.model
	echo "== android";           adb shell getprop ro.build.version.release
	echo "== termico";           adb shell dumpsys thermalservice 2>/dev/null | grep -iE "current temperatures|thermal status|severity" | head -12
	echo "== freq cpu";          adb shell "for c in /sys/devices/system/cpu/cpu[0-7]/cpufreq/scaling_cur_freq; do echo -n \"\$c \"; cat \$c; done" 2>/dev/null
	echo "== gpubusy";          adb shell "cat /sys/class/kgsl/kgsl-3d0/gpubusy" 2>/dev/null
	echo "== gpu freq";         adb shell "cat /sys/class/kgsl/kgsl-3d0/gpuclk" 2>/dev/null
	echo "== bateria";          adb shell dumpsys battery 2>/dev/null | grep -E "level|status|temperature" | head -5
} > "$OUT/contexto.txt" 2>&1

# 5) PerfLog del emulog (fps, EE/GS/VU/GPU, sincronia MTVU)
LOG=$(adb shell "ls /sdcard/Android/data/$PKG/files/logs/emulog.txt 2>/dev/null" | tr -d '\r')
if [ -n "$LOG" ]; then
	adb shell "grep -a 'PerfLog' $LOG | tail -60" > "$OUT/perflog.txt" 2>/dev/null
else
	echo "aviso: no encontre emulog.txt (Android/data restringido); el PerfLog queda vacio" > "$OUT/perflog.txt"
fi

echo "== Top funciones"
head -25 "$OUT/por_funcion.txt" 2>/dev/null
echo
echo "== Top bibliotecas"
head -12 "$OUT/por_biblioteca.txt" 2>/dev/null
echo
echo "== PerfLog (ultimas lineas)"
tail -8 "$OUT/perflog.txt" 2>/dev/null
echo
echo "listo: $OUT"
