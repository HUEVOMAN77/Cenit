#!/bin/bash
# Mutation testing de Harness11 (herramienta de VERIFICACION, no de medicion).
#
# Que es: dos fases. La primera, nueve roturas PROPOSITAS del indice PC->hueco
# (MvuSbIndex.h): se muta el encabezado original, se compila el arnes contra el MUTANTE
# y se mira si el arnes se pone rojo. La segunda, los dos acoples que el arnes NO puede
# ver porque viven en el TU del motor: se muta el DATO del header (kSlots, kMaxSlotId) y
# se espera que MvuSuperblock-arm64.cpp se caiga al compilar.
#
# Un arnes que queda verde con el codigo saboteado no verifica nada, asi que esto es
# parte de la evidencia y no un extra: sin esta lista, los "14067 comprobaciones, 0
# fallos" de Harness11 son solo un numero.
#
# Que NO es: no prueba rendimiento (eso lo mide tools/medicion/sb_resolve_bench.cpp),
# no prueba nada en el telefono, y no prueba el motor completo: solo el componente
# indice contra su modelo. Un mutante que escapa aqui significa que el arnes tiene un
# hueco de cobertura; se arregla el arnes antes de cerrar la version, no se "documenta"
# el escape como aceptable.
#
# Uso: bash tools/arneses/mutacion_h11.sh   (RC=0 solo si los ONCE mutantes —9 del arnes
# y 2 del TU del motor— fueron detectados y todas las anclas se encontraron). Requiere
# g++ en el host.
#
# Trampa que este script ya piso y por eso esta escrita aqui: la primera version copiaba
# el encabezado ORIGINAL dentro de la caja en cada ejecucion, o sea por encima del
# mutante. Los siete mutantes salieron "NO DETECTADO" no porque el arnes fuera debil,
# sino porque se ejecutaba el codigo limpio. Un verde puede ser mentira por el script y
# no por el codigo: por eso aqui la caja se limpia UNA vez y mut() es lo unico que
# escribe MvuSbIndex.h.
set -u

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
cd "$root" || exit 1

SRC=app/src/main/cpp/pcsx2/arm64/MvuSbIndex.h
TEST=tools/arneses/h11-src/sb_index_test.cpp
box=/tmp/h11mut

[ -f "$SRC" ]  || { echo "mutacion h11: no existe $SRC (ruta relativa al repo)"; exit 1; }
[ -f "$TEST" ] || { echo "mutacion h11: no existe $TEST"; exit 1; }
command -v g++ >/dev/null 2>&1 || { echo "mutacion h11: hace falta g++ en el host para compilar el arnes mutado"; exit 1; }

rm -rf "$box"; mkdir -p "$box"
cp "$TEST" "$box/"

undetected=0; escaped=0; esc2=0; total=0

# mut <ancla vieja> <ancla nueva>: parte SIEMPRE del archivo original del repo y
# escribe solo la caja. RC=3 si la ancla no aparece (el mutante no se aplico, y
# cualquier resultado posterior seria de otro codigo).
mut() {
  python3 -c "
import sys
s=open('$SRC').read()
a,b=sys.argv[1],sys.argv[2]
if a not in s:
    sys.stderr.write('ANCLA NO ENCONTRADA: '+repr(a[:70])+'\n'); sys.exit(3)
open('$box/MvuSbIndex.h','w').write(s.replace(a,b,1))
" "$1" "$2"
}

# run_mut <nombre> <descripcion>: compila y ejecuta el arnes contra lo que hay en la caja.
run_mut() {
  local name=$1 desc=$2
  total=$((total+1))
  if ! g++ -std=c++17 -Wall -Wextra -I"$box" -o "$box/h11_$name" "$box/sb_index_test.cpp" 2>"$box/err_$name"; then
    echo "MUTANTE $name ($desc): no compila => DETECTADO en el compile"
    head -4 "$box/err_$name"
    return
  fi
  local out
  out=$("$box/h11_$name" 2>/dev/null | tail -1)
  if [ -z "$out" ]; then
    # Sin recuesto no hay comprobacion: puede ser caida por escribir fuera del arreglo.
    # Se cuenta como NO detectado y se investiga, no se celebra.
    echo "MUTANTE $name ($desc): SIN RECUESTO => se cuenta como NO detectado (el arnes no termino)"
    undetected=$((undetected+1)); return
  fi
  echo "MUTANTE $name ($desc): $out"
  case "$out" in
    *", 0 Fallos") echo "  -> NO DETECTADO (verde con el codigo roto): el arnes no cubre esto"; undetected=$((undetected+1));;
    *) echo "  -> DETECTADO";;
  esac
}

echo "== mutacion Harness11 / MvuSbIndex.h =="
echo

# m1: Bind sobre casilla ocupada debe reventar la invariante, no pisar el enlace. Si
# pisa en silencio, un hueco deja de ser alcanzable por su PC real (despacho perdido).
mut 'if (cur != kEmpty)
				return -2;' 'if (false)
				return -2;' && run_mut m1 "Bind pisa en silencio otro enlace" || escaped=$((escaped+1))

# m2: Unbind debe borrar solo si la casilla apunta a ESE hueco; si no, libera el enlace
# de otro hueco.
mut 'if (at[s] != want)
				return false;' 'if (false)
				return false;' && run_mut m2 "Unbind sin comprobar el dueno" || escaped=$((escaped+1))

# m3: el dominio de PCs es /8 (entradas alineadas); >>4 repartiria mal y perderia hits.
mut '(pc_bytes >> 3) & (kSlots - 1u)' '(pc_bytes >> 4) & (kSlots - 1u)' && run_mut m3 "IdxOf >>4 en vez de >>3" || escaped=$((escaped+1))

# m4: se guarda id+1 porque 0 es "vacio". Guardar id hace que el hueco 0 sea invisible.
mut 'static_cast<uint16_t>(slotId + 1u)' 'static_cast<uint16_t>(slotId)' && run_mut m4 "id+1 perdido (hueco 0 == vacio)" || escaped=$((escaped+1))

# m5: ClearAll a media tabla deja enlaces fantasma tras ResetTableForEnable.
mut 'for (uint32_t i = 0; i < kSlots; i++)' 'for (uint32_t i = 0; i < kSlots / 2u; i++)' && run_mut m5 "ClearAll limpia media tabla" || escaped=$((escaped+1))

# m6: Count invertido; el arnes lo caza porque Count() se contrasta casilla por casilla.
mut 'if (at[i] != kEmpty)' 'if (at[i] == kEmpty)' && run_mut m6 "Count cuenta casillas vacias" || escaped=$((escaped+1))

# m7: SlotOf sin mascara lee fuera del arreglo para idx fuera de dominio. Este mutante
# ESCAPO verde en la primera version del arnes (el arnes nunca probaba idx fuera de
# dominio); se cerro anadiendo el plegado explicito, no bajando la mutacion.
mut 'return idx & (kSlots - 1u);' 'return idx;' && run_mut m7 "SlotOf sin mascara" || escaped=$((escaped+1))

# m8/m9: el techo de id. Un slotId > kMaxSlotId trunca id+1 a 0 == kEmpty: Bind sobre
# casilla vacia se reportaria "idempotente" habiendo enlazado NADA, y Unbind reportaria
# verdadero por un enlace que no existe. Inalcanzable desde el motor (kSbMaxVariants +
# static_assert), pero es la puerta del componente: no se confia en el llamador.
mut '			if (slotId > kMaxSlotId)
				return -2;
' '' && run_mut m8 "guard del techo de id fuera de Bind" || escaped=$((escaped+1))

mut '			if (slotId > kMaxSlotId)
				return false;
' '' && run_mut m9 "guard del techo de id fuera de Unbind" || escaped=$((escaped+1))

echo
echo "== fase 2: los dos static_assert DEL MOTOR sobre el indice =="
# Los dos acoples de dominio y techo NO los puede probar el arnes de host: MvuSbIndex.h
# es libre de dependencias a proposito, asi que no ve ni la sonda ni kSbMaxVariants. Los
# vigila el TU del motor (MvuSuperblock-arm64.cpp, lineas ~204/~207). Aqui se muta el
# MISMO header y se espera que el TU se caiga AL COMPILAR: un acople que no se prueba
# rompiendolo es una decoracion, y eso es justo lo que esta version viene a cerrar.
#
# Trampa propia de esta fase, ya pisada: el primer intento mutaba la COMPARACION del
# assert (kSbMaxVariants + 1u <= kMaxSlotId) y salia RC=0 — porque 256+1 sigue cabiendo
# en 65534, o sea el "mutante" no violaba ninguna invariante y su verde no probaba nada.
# Se muta el DATO (el header), no la ley, que es lo que un cambio real podria mover.
tu_fail() { # <nombre> <descripcion> <ancla> <reemplazo>
  local name=$1 desc=$2 old=$3 new=$4
  local box2=/tmp/h11mut_tu
  rm -rf "$box2"; mkdir -p "$box2"
  if ! cp "$SRC" "$ARM_DIR/MvuSbLife.h" "$ARM_DIR/MvuSuperblock-arm64.h" \
     "$ARM_DIR/MvuTraceProbe-arm64.h" "$ARM_DIR/MvuSuperblock-arm64.cpp" "$box2/" 2>/dev/null; then
    echo "MUTANTE $name ($desc): no se pudo armar la caja del TU"; esc2=$((esc2+1)); total=$((total+1)); return
  fi
  if ! python3 -c "
import sys
p=sys.argv[1]
s=open(p).read()
old,new=sys.argv[2],sys.argv[3]
if old not in s:
    sys.stderr.write('ANCLA NO ENCONTRADA: '+repr(old)+'\n'); sys.exit(3)
open(p,'w').write(s.replace(old,new,1))
" "$box2/MvuSbIndex.h" "$old" "$new"; then
    echo "MUTANTE $name ($desc): ANCLA NO ENCONTRADA"; esc2=$((esc2+1)); total=$((total+1)); return
  fi
  local err rc
  err=$(g++ -std=gnu++20 -fsyntax-only -msse4.1 -mpopcnt $TU_INC "$box2/MvuSuperblock-arm64.cpp" 2>&1)
  rc=$?
  total=$((total+1))
  if [ $rc -ne 0 ]; then
    echo "MUTANTE $name ($desc): TU del motor RC=$rc => DETECTADO en el compile"
    echo "$err" | grep -m1 "static assertion failed"
  else
    echo "MUTANTE $name ($desc): TU VERDE con el dato cambiado => NO DETECTADO (el acople no mira)"
    undetected=$((undetected+1))
  fi
}

ARM_DIR=$(cd "$(dirname "$SRC")" && pwd)
FMT_INC="$root/app/src/main/cpp/3rdparty/fmt/include"
# Mismas rutas y mismo estandar que la pata C de h10 (-std=gnu++20, -msse4.1 -mpopcnt:
# los flags del clang++ del NDK leidos del log del run #64). La caja de mutacion va
# PRIMERA en -I: si fuera al reves el TU veria los headers del repo y la fase entera
# daria verde pase lo que pase — otra forma de verde mentiroso, que es lo que este
# script existe para cazar.
TU_INC="-I/tmp/h11mut_tu -I$root/app/src/main/cpp -I$root/app/src/main/cpp/pcsx2 -I$root/app/src/main/cpp/common -I$ARM_DIR -I$FMT_INC"
if [ ! -f "$FMT_INC/fmt/base.h" ]; then
  echo "  SIN FASE 2: no hay fmt en $FMT_INC, el TU del motor no se pudo analizar."
  echo "  Los dos static_assert del motor NO se verificaron en esta corrida: RC=1 a proposito."
  esc2=$((esc2+1))
else
  # Control ANTES que los mutantes: con la caja sin mutar el TU tiene que dar RC=0. Sin
  # este control, un RC=1 por cualquier otro motivo (ruta mal armada, falta un include)
  # contaria como "detectado" y los dos mutantes serian decorativos.
  rm -rf /tmp/h11mut_tu; mkdir -p /tmp/h11mut_tu
  cp "$SRC" "$ARM_DIR/MvuSbLife.h" "$ARM_DIR/MvuSuperblock-arm64.h" \
     "$ARM_DIR/MvuTraceProbe-arm64.h" "$ARM_DIR/MvuSuperblock-arm64.cpp" /tmp/h11mut_tu/
  if g++ -std=gnu++20 -fsyntax-only -msse4.1 -mpopcnt $TU_INC /tmp/h11mut_tu/MvuSuperblock-arm64.cpp >/dev/null 2>&1; then
    echo "CONTROL fase 2 (caja sin mutar): TU RC=0 => un RC=1 de abajo es del mutante."
  else
    echo "CONTROL fase 2 (caja sin mutar): TU NO COMPILA LIMPIO => la fase 2 no vale nada"
    esc2=$((esc2+1))
  fi
  tu_fail m10 "kSlots 2048 -> 1024 (el dominio ya no es el de la sonda)" \
    "static constexpr uint32_t kSlots = 2048u;" "static constexpr uint32_t kSlots = 1024u;"
  tu_fail m11 "kMaxSlotId 65534 -> 200 (la tabla de 256 no cabe en el tipo)" \
    "kMaxSlotId = 65534u;" "kMaxSlotId = 200u;"
fi

echo
echo "mutacion h11: no detectados=$undetected anclas-rotas=$((escaped+esc2)) sobre $total mutantes (esperados 11: 9 arnes + 2 acoples del motor)"
[ "$total" -eq 11 ] || { echo "mutacion h11: la cuenta de mutantes no cuadra"; exit 1; }
[ $undetected -eq 0 ] && [ $((escaped+esc2)) -eq 0 ] || exit 1
echo "mutacion h11: los 11 fueron detectados por comprobacion o por compile."
