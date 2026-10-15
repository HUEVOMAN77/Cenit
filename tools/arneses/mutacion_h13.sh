#!/bin/bash
# Mutation testing de Harness13 (herramienta de VERIFICACION, no de medicion).
#
# Que es: dos fases, igual que mutacion_h12.sh.
#
# FASE 1 — el encabezado (CenitPresentStats.h). Ocho roturas PROPOSITAS: cada una
# mueve la decision de casilla (nativo/apagado/hecho/fallado) o se lleva un
# incremento de la cuenta. Los mutantes de cuenta (m5..m8) son los importantes y
# son los que un arnes de "solo logica" no veria: el clasificador sigue dando la
# misma respuesta en pantalla, pero el numero que el telefono imprime pasa a
# significar otra cosa. Eso es exactamente lo que esta version existe para evitar,
# asi que se prueba.
#
# FASE 2 — el TU del present (GSRenderer.cpp y GSRenderer.h). El arnes de host NO
# puede analizar ese archivo: tira de Vulkan/ImGui/GSState. Lo que se fija aqui es
# el TEXTO del cable: que la cuenta se haga con los DOS hechos separados (porton y
# sub-nativo) y no con una condicion sola, que el tallado este en el camino que
# presenta (no en el de frame-skip), que la linea de cierre imprima el resultado de
# Closes() y no lo calcule en silencio, que Destroy() siga llamando a
# GSCapture::EndCapture() (romperlo seria un leak de captura, no una medicion), y
# que el header nuevo este dado de alta en CMakeLists y en el include de GSRenderer.h.
# Son comprobaciones de TEXTO, no de compile: se dicen asi, con su limitacion
# escrita, en vez de fingir que cubren lo que cubre el CI.
#
# Un arnes verde con el codigo saboteado no verifica nada: sin esta lista, los
# "34 Comprobaciones, 0 Fallos" de Harness13 serian solo un numero.
#
# Que NO es: no prueba que EASU se vea mejor (eso es imagen en pantalla y solo lo
# dice el telefono), no prueba cuanto se usa en un juego (eso lo dice la linea de
# cierre en el log), no toca el rendimiento.
#
# Uso: bash tools/arneses/mutacion_h13.sh  (RC=0 solo si los OCHO mutantes de fase 1
# fueron detectados en la comprobacion que se nombra a su lado, y las ONCE anclas de
# fase 2 aparecieron). Requiere g++ en el host.
#
# Trampa heredada de h12: la caja se limpia UNA vez y mut() es lo unico que escribe
# el header; el CONTROL (caja sin mutar) corre primero, porque sin el cualquier RC=1
# contaria como "detectado".
set -u

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
cd "$root" || exit 1

SRC=app/src/main/cpp/pcsx2/GS/Renderers/Common/CenitPresentStats.h
GS=app/src/main/cpp/pcsx2/GS/Renderers/Common/GSRenderer.cpp
GSH=app/src/main/cpp/pcsx2/GS/Renderers/Common/GSRenderer.h
CML=app/src/main/cpp/pcsx2/CMakeLists.txt
VM=app/src/main/cpp/pcsx2/VMManager.cpp
TEST=tools/arneses/h13-src/present_test.cpp
box=/tmp/h13mut

for f in "$SRC" "$GS" "$GSH" "$CML" "$VM" "$TEST"; do
  [ -f "$f" ] || { echo "mutacion h13: no existe $f (ruta relativa al repo)"; exit 1; }
done
command -v g++ >/dev/null 2>&1 || { echo "mutacion h13: hace falta g++ en el host"; exit 1; }

rm -rf "$box"; mkdir -p "$box"
cp "$TEST" "$box/"

undetected=0; total=0

# m <nombre> <descripcion> <ancla> <reemplazo> <rojo esperado>: aplica el mutante y
# lo corre. Si la ancla no aparece o aparece dos veces, se CUENTA como caso no
# detectado: un mutante que no se aplico no probo nada, y dejarlo fuera del recuento
# es la manera facil de cerrar con RC=0 sin haber roto lo que se creia roto.
m() {
  local name=$1 desc=$2 a=$3 b=$4 exp=$5
  total=$((total+1))
  if ! mut "$a" "$b"; then
    echo "MUTANTE $name ($desc): ANCLA ROTA => NO DETECTADO (el mutante no se aplico)"
    undetected=$((undetected+1)); return
  fi
  run_mut_body "$name" "$desc" "$exp"
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
open('$box/CenitPresentStats.h','w').write(s.replace(a,b,1))
" "$1" "$2"
}

run_mut_body() {
  local name=$1 desc=$2 expect=$3
  if ! g++ -std=c++17 -Wall -Wextra -I"$box" -o "$box/h13_$name" "$box/present_test.cpp" 2>"$box/err_$name"; then
    echo "MUTANTE $name ($desc): no compila => DETECTADO en el compile"
    head -3 "$box/err_$name"
    return
  fi
  local out
  out=$("$box/h13_$name" 2>/dev/null | grep -m1 "^FALLO:")
  if [ -z "$out" ]; then
    if "$box/h13_$name" >/dev/null 2>&1; then
      echo "MUTANTE $name ($desc): VERDE con el codigo roto => NO DETECTADO"
    else
      echo "MUTANTE $name ($desc): SIN RECUESTO (RC!=0 sin FALLO) => NO DETECTADO, investigar"
    fi
    undetected=$((undetected+1)); return
  fi
  case "$out" in
    *"$expect"*) echo "MUTANTE $name ($desc): DETECTADO en la comprobacion esperada";;
    *) echo "MUTANTE $name ($desc): DETECTADO pero en otra comprobacion (esperada ~\"$expect\")"
       echo "  rojo real: ${out:0:160}";
       undetected=$((undetected+1));;
  esac
}

echo "== mutacion Harness13 / CenitPresentStats.h (fase 1) =="
echo

cp "$SRC" "$box/CenitPresentStats.h"
if g++ -std=c++17 -Wall -Wextra -I"$box" -o "$box/h13_control" "$box/present_test.cpp" 2>/dev/null \
   && "$box/h13_control" 2>/dev/null | grep -q ", 0 Fallos"; then
  echo "CONTROL (caja sin mutar): verde => un rojo de abajo es del mutante."
else
  echo "CONTROL (caja sin mutar): NO ESTA VERDE => todo lo de abajo no vale nada"
  "$box/h13_control" 2>/dev/null | head -5
  exit 1
fi
echo

# --- mutantes de DECISION: cambian que casilla toca -------------------------------

# m1: el no-sub-nativo se clasifica por el porton en vez de por el cuadro. Coste:
# los cuadros nativos de una sesion con la opcion encendida se van a sub-apagado, y
# la linea de cierre deja de cuadra con frames == ... (no, se sigue moviendo una
# sola casilla: por eso hace falta la comprobacion por casilla, no solo el cierre).
m m1 'nativo se decide por el porton' \
  '		if (!sub_native)' \
  '		if (!gate_open)' \
  'no sub-nativo, porton abierto, cadena falsa'

# m2: un sub-nativo con el porton cerrado se cuenta como HECHO. Es el mutante de la
# mentira favorable: EASU aparece usandose donde nunca pudo correr.
m m2 'porton cerrado se cuenta como hecho' \
  '		if (!gate_open)' \
  '		if (false)' \
  'sub-nativo con porton cerrado'

# m3: invertido hecho/fallado. El telefono diria "fallo" en cada cuadro bueno.
m m3 'hecho y fallado invertidos' \
  '		return chain_ran ? kSubHecho : kSubFallado;' \
  '		return chain_ran ? kSubFallado : kSubHecho;' \
  'sub-nativo pedido y fallado'

# m4: un fallo se cuenta como hecho. Es el mutante PELIGROSO: esconde el peor caso,
# el cuadro sub-nativo que pidio la cadena y se estiro igual sin aviso.
m m4 'el fallo se disfraza de exito' \
  '		return chain_ran ? kSubHecho : kSubFallado;' \
  '		return kSubHecho;' \
  'sub-nativo pedido y fallado'

# --- mutantes de CUENTA: la decision esta bien, el numero no ---------------------

# m5: frames no sube. La linea de cierre diria menos cuadros de los presentados y
# el denominador del porcentaje se lee mal. No toca ninguna decision: si el arnes
# solo probara decisiones, escapa.
m m5 'frames no sube (decide bien, cuenta mal)' \
  '			frames++;' \
  '			;' \
  'cuadros tallados'

# m6: se tallan DOS casillas para un cuadro. El cierre deja de cuadrar.
m m6 'un cuadro mueve dos casillas' \
  '			by_reason[Classify(sub_native, gate_open, chain_ran)]++;' \
  '			by_reason[Classify(sub_native, gate_open, chain_ran)]++;
			by_reason[kNativo]++;' \
  'casilla nativo =='

# m7: Closes() debilitado a <=. Acepta casillas infladas, o sea acepta la
# contabilidad hinchada — que es el modo en que un "verificado" deja de serlo.
m m7 'el cierre acepta suma menor' \
  '			return sum == frames;' \
  '			return sum <= frames;' \
  'NO cierra (control negativo)'

# m8: sub-nativo incluye los nativos. El porcentaje de uso de EASU sale inflado,
# que es literalmente el defecto de denominador de 0.6.35.
m m8 'sub-nativo se infla con los nativos' \
  '			return by_reason[kSubApagado] + by_reason[kSubHecho] + by_reason[kSubFallado];' \
  '			return frames;' \
  'sub-nativo es la suma de las tres casillas'

echo
echo "== fase 2: el cable del present (texto de GSRenderer.cpp / .h) =="
echo
# Cada ancla se dice con su limitacion: esto no prueba que el motor llame bien,
# prueba que el texto del motor sigue siendo el que Harness13 pinó.
anchors=0; anclas_fallan=0

ancla() { # <nombre> <archivo> <cadena>
  total=$((total+1))
  if grep -qF "$3" "$2"; then
    echo "ANCLA $1: presente"
    anchors=$((anchors+1))
  else
    echo "ANCLA $1: NO ENCONTRADA => el cable cambio y Harness13 ya no lo cubre"
    anclas_fallan=$((anclas_fallan+1))
  fi
}

# 1: los DOS hechos se calculan aparte; una sola condicion no puede distinguir
# "apagado" de "fallado", que es el caso que el log de 0.6.34 no podia ver.
ancla f1-el-porton "$GS" 'const bool easu_gate_open = GSConfig.EASUReconstruct && g_gs_device->Features().easu_reconstruct;'
ancla f2-la-entrada-sub "$GS" 'const bool easu_sub_native ='
# 3: el gate real sigue siendo porton Y sub-nativo (no se ancho el if a una sola).
ancla f3-la-conjuncion "$GS" 'if (easu_gate_open && easu_sub_native)'
# 4: la cuenta se hace SIEMPRE que se presenta un cuadro, no solo cuando la cadena
# corrio. El ancla lleva la INDENTACION (tres tabs) a proposito: se probo a mover el
# Tally dentro del if —el defecto de denominador de 0.6.35, otra vez— y una ancla sin
# tabs segua dando verde, o sea era decorativa. Con los tabs, el mutante ruida.
# Y la forma de anclar importa, porque la primera version de esta ancla era
# decorativa sin parecerlo: grep -F busca SUBCADENA, asi que mover el Tally dentro
# del if (que le pone un tab mas) seguia "conteniendo" los tres tabs. Se ancla al
# principio de linea Y con la cuenta exacta de tabs, en python: grep -E no tiene por
# que interpretar \t como tab (portable: es indefinido), y un ancla que nunca
# coincidio por eso daria rojo permanente, que es otro modo de tener un verificador
# roto. Las dos formas de ancla decorativa se probaron aqui, una tras otra.
total=$((total+1))
if python3 -c "
import sys
ok = any(l.startswith('\t\t\tm_present_stats.Tally(easu_sub_native, easu_gate_open, easu_ran);')
         for l in open('$GS'))
sys.exit(0 if ok else 1)
"; then
  echo "ANCLA f4-el-tallado-fuera-del-if: presente"
  anchors=$((anchors+1))
else
  echo "ANCLA f4-el-tallado-fuera-del-if: NO => el Tally se movio dentro de un if y el denominador volvio a estar roto"
  anclas_fallan=$((anclas_fallan+1))
fi
# 5: el cierre imprime el veredicto de Closes() (no lo calcula en silencio).
ancla f5-el-cierre "$GS" 'ps.Closes() ? "ok" : "DESAJUSTE");'
# 6: el orden: la cadena se pide antes de tallar (si se tallara antes, easu_ran
# seria false por construccion y "fallado" mentiria).
total=$((total+1))
if python3 -c "
import sys
s=open('$GS').read()
i=s.index('easu_ran = g_gs_device->EASUChain')
j=s.index('m_present_stats.Tally(easu_sub_native, easu_gate_open, easu_ran);')
sys.exit(0 if i < j else 1)
" 2>/dev/null; then
  echo "ANCLA f6-orden-el-tally-DESPUES-de-la-cadena: presente"
  anchors=$((anchors+1))
else
  echo "ANCLA f6-orden-el-tally-DESPUES-de-la-cadena: NO => easu_ran seria false por construccion y 'fallado' mentiria"
  anclas_fallan=$((anclas_fallan+1))
fi
# 7: Destroy sigue cerrando la captura: la linea de telemetria no puede pagar el
# precio de romper el camino normal.
ancla f7-endcapture-intacto "$GS" '	GSCapture::EndCapture();
}'
# 8: el header nuevo esta dado de alta donde se compila (CMake) y donde se usa.
ancla f8-cmake "$CML" '	GS/Renderers/Common/CenitPresentStats.h'
ancla f9-include "$GSH" '#include "GS/Renderers/Common/CenitPresentStats.h"'
# 9: el aviso de "below native" sigue DISTINGUIENDO el caso EASU. No es decorativo:
# en el log del telefono aparece dos veces, en los dos recortes del regidor, y con
# la opcion encendida el texto "this will break rendering" describe otra cosa.
ancla f10-aviso-condicional "$VM" 'const bool easu_pedida = EmuConfig.GS.EASUReconstruct &&'
# ... y el aviso ORIGINAL no se borro: sigue ahi para EASU apagado y para otro
# backend, que es donde si puede romper cosas. Sin esta segunda ancla, "arreglar"
# el aviso borrando el viejo pasaria verde.
ancla f11-aviso-viejo-intacto "$VM" '"Upscale multiplier is below native, this will break rendering."'

echo
echo "Total: $total casos (8 mutantes + $anchors anclas), no detectados: $undetected, anclas rotas: $anclas_fallan"
if [ "$undetected" -eq 0 ] && [ "$anclas_fallan" -eq 0 ]; then
  echo "RC=0: los ocho mutantes cayeron en su comprobacion nombrada y las anclas del cable estan."
  exit 0
fi
exit 1
