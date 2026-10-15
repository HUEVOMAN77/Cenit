#!/bin/bash
# Mutation testing de Harness14 (herramienta de VERIFICACION, no de medicion).
#
# Que se prueba aqui y por que es distinto a h11/h12/h13: las tres cadenas de
# elegibilidad del superbloque (armar / unir / cerrar) vivian dentro de
# MvuSuperblock-arm64.cpp como comprobaciones sueltas que movian UNO de cuatro
# contadores multiproposito. La partida real de God of War (SCES-53133) con el
# motor encendido volco, linea 304 del registro:
#   vuSB: intentos=120542 construidos=0 uniones=0 ops=0
#         rechazos{struct=47460 cold=21128 budget=1 ident=51953}
# Cuatro cubos para doce comprobaciones, y con eso no se podia decir NINGUNA de
# las dos cosas que importan: por que no se armo nada, y si siquiera se miro. Este script rompe, una por una, las decisiones
# que ese cambio introduce.
#
# FASE 1 — el encabezado puro (MvuSbEligibility.h). Veintiuna roturas PROPOSITAS
#   en la ley: tumbar una guarda de cortocircuito (m1..m2), aflojar una comparacion
#   (m3..m6, m9), quitar un techo o un piso (m7, m8), adelantar una comprobacion en
#   la cadena (m10..m12), invertir la asimetria `>=`/`>` que el motor heredo
#   (m13..m15), romper la puerta (m16..m19) y mentir en una etiqueta o en una
#   clasificacion (m20, m21). Cada una tiene que dar un FALLO CON NOMBRE, y el
#   nombre se exige por texto: un rojo en otra comprobacion significa que el arnes
#   capto el cambio por accidente, y un verde con el codigo roto significa que la
#   comprobacion es decorativa.
#   Los mutantes de ORDEN (m10..m12) son los que un arnes de "causas alcanzables"
#   no veria: el veredicto final sigue siendo rechazo, pero se cuenta en OTRA
#   casilla, que es exactamente la forma en que una telemetria puede estar rota sin
#   que nada truene. Y m21 es el caso inverso: una causa que deja de ser rechace
#   hace que la cadena deje de cuadrar contra su denominador, o sea un numero que
#   se lee mal sin que cambie nada en pantalla.
#
# Los cuatro primeros nombres que este script exigia estaban mal elegidos (m3, m6,
# m12, m17): el rojo salia en la comprobacion ANTERIOR de la misma familia, y el
# script lo canto como "detectado pero en otra" en vez de dejarmelo pasar. Las
# expectativas se corrigieron contra el rojo real, no al reves — y queda aqui escrito
# porque un mutante que se "arregla" moviendo la expectatina es exactamente el verde
# vacio que este archivo existe para impedir.
#
# FASE 2 — los tres TU que el arnes de host NO puede analizar (microVU-arm64.cpp
#   tira de <arm_neon.h> y vixl; MvuSuperblock-arm64.cpp idem por include chain).
#   Aqui se fija el TEXTO del cable, no la conducta: que el hook de armado este
#   llamado en los TRES caminos (lento + stub con sonda + stub sin sonda), que la
#   condicion de exito de la resolucion normal se capture ANTES de que SbResolve
#   pise el puntero (sin eso el armado se apoyaria en un prog.cur que este
#   despacho no resolvio: un crash que solo un telefono puede mostrar), que toda
#   salida terminal mueva su casilla, y que las cuatro sumas de cierre se impriman
#   con su veredicto. Son comprobaciones de TEXTO y se dicen asi, con su limitacion
#   escrita, en lugar de fingir que cubren lo que cubre el CI.
#
# Un arnes verde con el codigo saboteado no verifica nada: sin esta lista, los
# «15752094 Comprobaciones, 0 Fallos» de Harness14 serian solo un numero grande.
#
# Que NO es: no prueba que un telefono construya superbloques (eso solo lo dice el
# log, y es el punto donde este proyecto se nego dos veces a afirmar sin evidencia);
# no prueba que los datos que el motor pasa a DecideArm sean los vivos (eso lo mira
# el propio cierre impreso: un DESAJUSTE lo canta); no toca rendimiento.
#
# Uso: bash tools/arneses/mutacion_h14.sh  (RC=0 solo si los DIECISEIS mutantes de
# fase 1 cayeron en la comprobacion nombrada y las anclas de fase 2 aparecieron).
# Requiere g++ en el host.
#
# Trampa heredada de h12/h13: la caja se limpia UNA vez y mut() es lo unico que
# escribe el header; el CONTROL (caja sin mutar) corre primero, porque sin el
# cualquier RC=1 contaria como "detectado". Y en fase 1 el rojo se busca en la
# PRIMERA linea FALLO del binaario (no en un grep -m1 sobre stderr), porque con el
# diferencial corriendo hay miles de comprobaciones y un "detectado pero en otra"
# tiene que verse como lo que es.
set -u

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
cd "$root" || exit 1

SRC=app/src/main/cpp/pcsx2/arm64/MvuSbEligibility.h
SB=app/src/main/cpp/pcsx2/arm64/MvuSuperblock-arm64.cpp
MVU=app/src/main/cpp/pcsx2/arm64/microVU-arm64.cpp
CML=app/src/main/cpp/pcsx2/CMakeLists.txt
H10=tools/arneses/h10-src/hdr_tu.cpp
TEST=tools/arneses/h14-src/elig_test.cpp
BASE=tools/arneses/baseline.sh
box=/tmp/h14mut

for f in "$SRC" "$SB" "$MVU" "$CML" "$H10" "$TEST" "$BASE"; do
  [ -f "$f" ] || { echo "mutacion h14: no existe $f (ruta relativa al repo)"; exit 1; }
done
command -v g++ >/dev/null 2>&1 || { echo "mutacion h14: hace falta g++ en el host"; exit 1; }

rm -rf "$box"; mkdir -p "$box"
cp "$TEST" "$box/elig_test.cpp"

undetected=0; total=0

m() { # <nombre> <descripcion> <ancla> <reemplazo> <rojo esperado>
  local name=$1 desc=$2 a=$3 b=$4 exp=$5
  total=$((total+1))
  if ! mut "$a" "$b"; then
    echo "MUTANTE $name ($desc): ANCLA ROTA => NO DETECTADO (el mutante no se aplico)"
    undetected=$((undetected+1)); return
  fi
  if ! g++ -std=c++17 -Wall -Wextra -O1 -I"$box" -o "$box/h14_$name" "$box/elig_test.cpp" 2>"$box/err_$name"; then
    echo "MUTANTE $name ($desc): no compila => DETECTADO en el compile"
    head -3 "$box/err_$name"
    return
  fi
  local out
  out=$("$box/h14_$name" 2>/dev/null | grep -m1 "^FALLO:")
  if [ -z "$out" ]; then
    if "$box/h14_$name" >/dev/null 2>&1; then
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
open('$box/MvuSbEligibility.h','w').write(s.replace(a,b,1))
" "$1" "$2"
}

echo "== mutacion Harness14 / MvuSbEligibility.h (fase 1) =="
echo

cp "$SRC" "$box/MvuSbEligibility.h"
if g++ -std=c++17 -Wall -Wextra -O1 -I"$box" -o "$box/h14_control" "$box/elig_test.cpp" 2>/dev/null \
   && "$box/h14_control" 2>/dev/null | grep -q ", 0 Fallos"; then
  echo "CONTROL (caja sin mutar): verde => un rojo de abajo es del mutante."
else
  echo "CONTROL (caja sin mutar): NO ESTA VERDE => todo lo de abajo no vale nada"
  "$box/h14_control" 2>/dev/null | head -5
  exit 1
fi
echo

# --- mutantes de CORTOCIRCUITO: cambian que causa se cuenta ----------------------

# m1: la identidad viva deja de cortocircuitar. El candidato ya enlazado volveria a
# pasar por toda la politica y se contaria como "armado" sobre si mismo.
m m1 'vivo deja de cortocircuitar' \
  '		if (in.already_live)
			return ArmCause::kLive;' \
  '		if (false)
			return ArmCause::kLive;' \
  'vivo: candidato en el PC'

# m2: la quemadura se ignora: re-armar un PC ya juzgado, compilaciones variantes
# infinitas sobre la misma forma rechazada.
m m2 'quemado se ignora' \
  '		if (in.burned)
			return ArmCause::kBurned;' \
  '		if (false)
			return ArmCause::kBurned;' \
  'quemado: PC ya juzgado'

# m3: una arista condicional (kind 2) se acepta como incondicional: fusionar por
# conjetura, que es el caso que la regla de oro no deja tocar.
m m3 'kind!=1 se afloja a kind>=1' \
  '		if (in.edge_kind != 1u)' \
  '		if (in.edge_kind > 2u)' \
  'sin-arista: kind=0'

# m4: el origen sin forma analizada pasa: se leeria hotness de un bloque que la
# sonda nunca midio.
m m4 'solo se mira la forma del origen' \
  '		if (in.shape_a_reason == 0u || in.shape_b_reason == 0u)' \
  '		if (in.shape_a_reason == 0u)' \
  'sin-forma: destino'

# m5: el piso del destino se cae: B puede tener una ejecucion y llamarse caliente.
m m5 'el piso de B desaparece' \
  '		if (in.exec_a < static_cast<uint64_t>(p.hot_entries) ||
			in.exec_b < static_cast<uint64_t>(p.target_min))' \
  '		if (in.exec_a < static_cast<uint64_t>(p.hot_entries))' \
  'bajo-piso: destino'

# m6: el techo del ratio se afloja un paso: un destino 5 veces mas caliente que el
# origen pasa a ser elegible, y eso es fusionar sobre un bloque que no es sucesor.
m m6 'techo de ratio +1' \
  '			ratio > static_cast<uint64_t>(p.max_ratio_pct))' \
  '			ratio > static_cast<uint64_t>(p.max_ratio_pct) + 1u)' \
  'ratio-fuera: por encima'

# m7: el piso del ratio se cae (orden de la cadena: 79 999/200 000 pasa a armar).
m m7 'piso de ratio se ignora' \
  '		if (ratio < static_cast<uint64_t>(p.min_ratio_pct) ||' \
  '		if (false ||' \
  'ratio-fuera: por abajo'

# m8: el techo del par de ops se cae: un area que no cabe en presupuesto se arma
# igual y el rechazo del cierre pasa a ser culpa del area, no de la politica.
m m8 'techo de ops del par se ignora' \
  '		if (static_cast<uint64_t>(in.ops_a) + in.ops_b > p.max_ops ||' \
  '		if (false ||' \
  'par-grande: un ops pasado'

# m9: el piso de hotness se compara con <= : el valor exacto del piso pasa a ser
# rechazo, y eso cambia cuales PCs se arman (no solo cual casilla se mueve).
m m9 'piso de hotness se vuelve <= (excluye el borde)' \
  '		if (in.exec_a < static_cast<uint64_t>(p.hot_entries) ||' \
  '		if (in.exec_a <= static_cast<uint64_t>(p.hot_entries) ||' \
  'caso base elegible'

# --- mutantes de ORDEN: mismo veredicto, otra casilla ----------------------------

# m10: quemado se mira antes que vivo. Ambos son rechaces de identidad, así que
# NINGUN cambio de conducta se ve en pantalla: solo la causa que el log imprime.
# Es el mutante que un arnes sin comprobaciones de precedencia aprueba.
m m10 'orden: quemado antes que vivo' \
  '		if (in.already_live)
			return ArmCause::kLive;
		if (in.burned)
			return ArmCause::kBurned;' \
  '		if (in.burned)
			return ArmCause::kBurned;
		if (in.already_live)
			return ArmCause::kLive;' \
  'precedencia: vivo > quemado'

# m11: la forma se mira antes que la arista. Con arista ausente y forma nula, el
# viejo decia sin-arista; esto diria sin-forma: el log contaria otra cadena.
m m11 'orden: forma antes que arista' \
  '		if (in.edge_kind != 1u)
			return ArmCause::kNoEdge;
		if (in.shape_a_reason == 0u || in.shape_b_reason == 0u)
			return ArmCause::kNoShape;' \
  '		if (in.shape_a_reason == 0u || in.shape_b_reason == 0u)
			return ArmCause::kNoShape;
		if (in.edge_kind != 1u)
			return ArmCause::kNoEdge;' \
  'precedencia: sin-arista > sin-forma'

# m12: el presupuesto del par se mira antes que el ratio. El rechazo de 2 000 000
# con par enorme pasa de ratio-fuera a par-grande.
m m12 'orden: par antes que ratio' \
  '		const uint64_t ratio = in.exec_b * 100ull / denom;
		if (ratio < static_cast<uint64_t>(p.min_ratio_pct) ||
			ratio > static_cast<uint64_t>(p.max_ratio_pct))
			return ArmCause::kRatio;' \
  '		const uint64_t ratio = in.exec_b * 100ull / denom;
		if (static_cast<uint64_t>(in.ops_a) + in.ops_b > p.max_ops ||
			static_cast<uint64_t>(in.cycles_a) + in.cycles_b > p.max_cycles)
			return ArmCause::kPair;
		if (ratio < static_cast<uint64_t>(p.min_ratio_pct) ||
			ratio > static_cast<uint64_t>(p.max_ratio_pct))
			return ArmCause::kRatio;
		if (false)
			return ArmCause::kPair;' \
  'ratio se cuenta aunque el par sea enorme'

# --- mutantes de UNION/CIERRE: la asimetria `>=`/`>` que el motor heredó ---------

# m13: la union pasa de >= a > : con el area YA en el techo se concede la union, el
# area crece por encima de politica y el rechazo se va al cierre.
m m13 'union: >= se vuelve >' \
  '		if (in.region_ops >= p.max_ops || in.region_cycles >= p.max_cycles)' \
  '		if (in.region_ops > p.max_ops || in.region_cycles > p.max_cycles)' \
  'sin-hueco-de-area: ops en el techo'

# m14: el cierre pasa de > a >= : adopta areas de exactamente 193 ops... no, de
# 192+1; y sobre todo rompe el pin del arnes, que exige la comparacion inclusiva.
m m14 'cierre: > se vuelve >=' \
  '		if (in.ops > p.max_ops || in.cycles > p.max_cycles)' \
  '		if (in.ops >= p.max_ops || in.cycles > p.max_cycles)' \
  '192 exacto: adopta (techo inclusivo)'

# m15: el tope de uniones se mira con > : un candidato con 5 uniones pide la 6 y le
# pasa por el techo — presupuesto roto en el area.
m m15 'tope de uniones: >= se vuelve >' \
  '		if (in.junctions_so_far >= p.max_junctions)' \
  '		if (in.junctions_so_far > p.max_junctions)' \
  'tope-uniones en 5'

# m16: la puerta deja de ser envoltura-arriba: una resta u32. Al dar la vuelta el
# contador (unas 26 horas de partida a 44 000 consideraciones/s) la comparacion
# miente al reves y la puerta se cierra para los 2048 PCs en silencio.
m m16 'puerta: comparacion sin envoltura' \
  '			const int32_t delta = static_cast<int32_t>(considered - next[i]);
			if (delta < 0)
				return false;' \
  '			if (considered < next[i])
				return false;' \
  'a traves del vuelco la puerta abre UNA vez'

# m17: la puerta no consume su ventana (nunca sube `considered`): se abriria en cada
# llegada, o sea el costo del armado lento volveria a estar en el despacho del stub
# — exactamente lo que la puerta existe para acotar.
m m17 'puerta: no sube el contador' \
  '			const uint32_t i = IdxOf(pc_bytes);
			considered++;' \
  '			const uint32_t i = IdxOf(pc_bytes);' \
  'un PC se re-julga 10 veces en 10 periodos'

# m18: Clear no limpia las ventanas: al re-encender el motor la tabla heredaria las
# puertas cerradas de la sesion anterior.
m m18 'puerta: Clear no limpia next' \
  '			for (uint32_t i = 0; i < kGateSlots; i++)
				next[i] = 0u;
			considered = 0u;' \
  '			considered = 0u;' \
  'tras Clear las 2048 ventanas estan abiertas'

# m19: IdxOf deja de desplazar: los PCs contiguos (cada 8 bytes) plegarian en la
# misma casilla y un PC nunca veria su ventana.
m m19 'puerta: IdxOf sin el >>3' \
  '			return (pc_bytes >> 3) & kGateSlotMask;' \
  '			return pc_bytes & kGateSlotMask;' \
  'IdxOf es biyectivo en el dominio de PCs de VU1'

# --- mutantes de ETIQUETA: la decision esta bien, el log dice otra cosa ----------

# m20: dos causas comparten nombre. El informe imprimiria "vivo" para lo vivo y para
# lo quemado: una cuenta que se lee mal, y la unica comprobacion que lo pillaria es
# la de unicidad por pares.
m m20 'dos causas de armar comparten nombre' \
  '			case ArmCause::kBurned: return "quemado";' \
  '			case ArmCause::kBurned: return "vivo";' \
  'armado: nombres de causa unicos'

# m21: IsArmRejection empieza a llamar exito al rechazo por desenlace. La cadena
# dejaria de contar kDesync y `intentos == Σ casillas` dejaria de cuadrar: el cierre
# del informe diria DESAJUSTE, que es el unico modo honesto de decirlo.
m m21 'kDesync deja de ser rechace' \
  '		return c != ArmCause::kPolicyPass && c != ArmCause::kArmed;' \
  '		return c != ArmCause::kPolicyPass && c != ArmCause::kArmed && c != ArmCause::kDesync;' \
  'paridad arm: el viejo rechazo y el nuevo aprueba'

echo
echo "== fase 2: el cable (texto de microVU-arm64.cpp / MvuSuperblock-arm64.cpp) =="
echo
# Cada ancla se dice con su limitacion: esto no prueba que el motor llame bien,
# prueba que el texto del motor sigue siendo el que Harness14 pinó. Lo que el
# host NO puede comprobar (los tres caminos de despacho, la re-entrada del
# compile variante) lo compila el CI y lo decide el log.
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

# 1-3: el armado tiene que llegar por TRES caminos. La cuenta por origen
# (lento/stub) es la evidencia de cobertura que 0.6.36 no tenia; si uno de los tres
# desaparece, el motor vuelve a mirar una fraccion de los despachos y el numero de
# intentos pasa a significar otra cosa sin que nadie lo vea.
ancla f1-hook-lento "$MVU" 'mVUSuperblock::SbConsiderArm(maskedPC, (const void*)pState, false)' \
  'el camino lento volvio a SbArmCompile directo: se pierde el origen y la puerta'
ancla f2-funcion-del-hook "$MVU" 'static void MvuSbArmFromStub(u32 maskedPC, bool resolved_normal)' \
  'no existe el helper unico del armado desde el stub (los dos caminos divergen)'
ancla f3-llamada-en-los-dos-stub "$MVU" 'MvuSbArmFromStub(maskedPC, resolved_normal);' \
  'el armado desde el stub desaparecio'
total=$((total+1))
if [ "$(grep -cF 'MvuSbArmFromStub(maskedPC, resolved_normal);' "$MVU")" = "2" ]; then
  echo "ANCLA f3b-las-dos-ramas (2 llamadas: con sonda y sin sonda): presente"
  anchors=$((anchors+1))
else
  echo "ANCLA f3b-las-dos-ramas: NO => el armado solo vive en uno de los dos stubs y con la sonda apagada el motor no construye"
  anclas_fallan=$((anclas_fallan+1))
fi

# 4: el hecho "la cadena normal resolvio" se captura ANTES de SbResolve. Sin ese
# orden, el armado se apoyaria en mVU.prog.cur de OTRO despacho: un crash que solo
# ocurre en un telefono. La ancla es de ORDEN, no de existencia — por eso lleva el
# indice de ambas lineas.
total=$((total+1))
if python3 -c "
import sys
s=open('$MVU').read()
i=s.index('const bool resolved_normal = (r != nullptr);')
j=s.index('void* sb = mVUSuperblock::SbResolve(maskedPC')
sys.exit(0 if i < j else 1)
"; then
  echo "ANCLA f4-orden-resolved_normal-ANTES-de-SbResolve: presente"
  anchors=$((anchors+1))
else
  echo "ANCLA f4-orden-resolved_normal-ANTES-de-SbResolve: NO => resolved_normal contaria una variante como resolucion normal"
  anclas_fallan=$((anclas_fallan+1))
fi
# 5: el guard del helper: sin resolucion normal NO se arma (es la condicion que el
# hook viejo llevaba como `result &&`).
ancla f5-el-guard "$MVU" 'if (!resolved_normal || !mVUSuperblock::IsEnabled() || microVU1.sbSlot >= 0)' \
  'el armado desde el stub perdio la condicion de exito normal o el no-re-arme'
# 6: la compilacion variante restaura lpState y sbSlot: sin el restore se envenena
# la clave de pareo del despacho siguiente.
ancla f6-restaura-lpstate "$MVU" 'microVU1.prog.lpState = sbSaved;' \
  'la compilacion variante dejo de restaurar lpState (clave de pareo pisada)'
ancla f7-restaura-sbslot "$MVU" '	microVU1.sbSlot = -1;' \
  'sbSlot quedo apuntando a un candidato en vuelo'

# 8-11: el cable de la cuenta. Cada cadena tiene que mover SU casilla terminal y
# cada denominador tiene que moverse en la entrada, no en el exito.
ancla f8-cuenta-arm "$SB" 'st.arm_cause[static_cast<u32>(cause)].fetch_add(1, std::memory_order_relaxed);' \
  'el rechazo de armado dejo de tener casilla (volvimos a un cubo multiproposito)'
ancla f9-cuenta-union "$SB" 'st.jun_cause[static_cast<u32>(cause)].fetch_add(1, std::memory_order_relaxed);' \
  'el rechazo de union dejo de tener casilla'
ancla f10-cuenta-cierre "$SB" 'st.close_cause[static_cast<u32>(ccause)].fetch_add(1, std::memory_order_relaxed);' \
  'el rechazo de cierre dejo de tener casilla'
ancla f11-denominadores "$SB" 'st.arm_from[from_stub ? 1u : 0u].fetch_add(1, std::memory_order_relaxed);' \
  'el denominador por origen de llegada desaparecio'
total=$((total+1))
if [ "$(grep -cF 'st.junction_requests.fetch_add(1, std::memory_order_relaxed);' "$SB")" = "1" ] \
   && [ "$(grep -cF 'st.close_calls.fetch_add(1, std::memory_order_relaxed);' "$SB")" = "1" ]; then
  echo "ANCLA f11b-un-solo-llamado-para-cada-denominador: presente"
  anchors=$((anchors+1))
else
  echo "ANCLA f11b-un-solo-llamado-para-cada-denominador: NO => un pedido contado dos veces invalida el cierre de la cadena"
  anclas_fallan=$((anclas_fallan+1))
fi
# 12: la puerta se limpia con la tabla.
ancla f12-puerta-se-limpia "$SB" 's_arm_gate.Clear();' \
  're-encender el motor heredaria las ventanas cerradas de la sesion anterior'
# 13: el presupuesto del despacho tiene cubo propio (kDispatch), no se suma a los
# rechazos de forma: son unidades distintas (pausar vs no-fusionar).
ancla f13-cubo-del-despacho "$SB" 'cubo[static_cast<u32>(mVUSbElig::Bucket::kDispatch)] =
					s.dispatch_budget.load(std::memory_order::relaxed);' \
  'el rechazo de despacho volvio a mezclarse con el presupuesto de forma'
# 14: los cuatro cierres se imprimen con el veredicto (ok/DESAJUSTE), no se calculan
# en silencio.
total=$((total+1))
if [ "$(grep -cF '[cierre=%s]' "$SB")" = "4" ] && [ "$(grep -cF 'puerta=%s' "$SB")" = "1" ]; then
  echo "ANCLA f14-cuatro-cierres-de-informe-mas-el-de-la-consola: presentes (4 + 1)"
  anchors=$((anchors+1))
else
  echo "ANCLA f14-cuatro-cierres: NO => hay una cadena cuyo cierre se calcula sin imprimirse"
  anclas_fallan=$((anclas_fallan+1))
fi
# 15: el resumen del emulog lleva los tres numeros de cobertura: es lo unico que
# viaja con el boton "Enviar registro".
ancla f15-linea-de-cobertura "$SB" 'puerta{lento=%" PRIu64 " stub=%" PRIu64 " dejo=%" PRIu64 ' \
  'el emulog dejo de reportar la cobertura del armado (la pregunta de 0.6.37)'
# 16: la lista de cero de ResetStats es por TAMANO, no a mano: una causa nueva sin
# vuelco quedaria con el numero de la sesion anterior.
ancla f16-cero-por-tamano "$SB" 'for (u32 k = 0; k < mVUSbElig::kArmCauseCount; k++)
					s.arm_cause[k].store(0);' \
  'el vuelco del contador por causas volvio a ser una lista a mano'
# 17: los cuatro contadores viejos NO vuelven: si alguien los resucita, habra dos
# cuentas de la misma decision (la leccion de DecideAsk en MvuFastPath.h).
total=$((total+1))
if [ "$(grep -c 'std::atomic<u64> rejected_' "$SB" | head -1)" = "0" ] \
   && [ "$(grep -c 'std::atomic<u64> rejected_' app/src/main/cpp/pcsx2/arm64/MvuSuperblock-arm64.h)" = "0" ]; then
  echo "ANCLA f17-los-cubos-viejos-no-resucitan: presente"
  anchors=$((anchors+1))
else
  echo "ANCLA f17-los-cubos-viejos-no-resucitan: NO => hay dos contadores para la misma decision"
  anclas_fallan=$((anclas_fallan+1))
fi
# 18: el header esta dado de alta donde se compila y donde se fija el contrato.
ancla f18-cmake "$CML" '	arm64/MvuSbEligibility.h' \
  'MvuSbEligibility.h no esta en CMake (la lista de fuentes no lo ve)'
ancla f19-contrato-h10 "$H10" 'static_assert(static_cast<unsigned>(mVUSbElig::ArmCause::kCount) == 12u,' \
  'h10 ya no fija cuantas casillas de armado hay (el informe recorreria otra cantidad)'
# 20: la politica del puente sigue siendo la del motor: siete constantes, ninguna
# tunneada por la decision pura.
ancla f20-politica-pineada "$H10" 'static_assert(mVUSuperblock::kSbPolicy.max_ops == mVUSuperblock::kSbMaxOps,' \
  'h10 dejo de fijar que la politica del header sea la del motor'
# 21: h14 esta en la linea base: sin esto, "corri los arneses" podria seguir
# queriendo decir "no corri el arnes nuevo".
ancla f21-h14-en-la-base "$BASE" '[ "$ONLY" = all ] || [ "$ONLY" = h14 ] && runh14' \
  'h14 no esta en la linea base: su verde no se obtiene corriendo baseline.sh'

echo
echo "Limitacion escrita de esta fase: las anclas son de TEXTO, no de compilacion."
echo "Ninguna prueba que el telefono entre por los tres caminos ni que la"
echo "compilacion variante conserve lpState — eso lo dice el CI al compilar y el"
echo "log al imprimir 'puerta{lento=.. stub=.. dejo=..}'. Lo que fijan es que el"
echo "cable sigue teniendo la forma que Harness14 pinó; si cambia, esto ruida."
echo
echo "Total: $total casos (21 mutantes + $anchors anclas), no detectados: $undetected, anclas rotas: $anclas_fallan"
if [ "$undetected" -eq 0 ] && [ "$anclas_fallan" -eq 0 ]; then
  echo "RC=0: los veintiun mutantes cayeron en su comprobacion nombrada y las anclas del cable estan."
  exit 0
fi
exit 1
