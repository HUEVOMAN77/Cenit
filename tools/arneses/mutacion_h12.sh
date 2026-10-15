#!/bin/bash
# Mutation testing de Harness12 (herramienta de VERIFICACION, no de medicion).
#
# Que es: dos fases.
#
# FASE 1 — el encabezado memoizado (MvuFastPath.h). Treinta roturas PROPOSITAS, una
# por cada decision de la clase: cada comprobacion de Serve, cada guarda de Record, el
# indice, la mascara, el umbral, el marcador de vacio, el orden de Invalidate/Reset y
# las dos guardas del llamador (DecideAsk/EpochStable, que subieron al header
# exactamente para poder romperse aqui) y, desde 0.6.36, los contadores del denominador
# (m27..m30 rompen LA CUENTA sin tocar la decision: es el unico modo de probar que el
# numero que el telefono imprime significa lo que dice). Se muta el encabezado ORIGINAL del repo, se
# compila el arnes contra el MUTANTE y se exige que se ponga rojo. Cada mutante tiene
# escrita al lado la comprobacion que se espera que reviente; si revienta otra, el
# arnes tiene un verde por accidente y hay que mirar por que.
#
# FASE 2 — el TU del motor (microVU-arm64.cpp). El arnes de host NO puede analizar
# ese archivo: tira de <arm_neon.h>, y el host no tiene NEON (intentarlo da "fatal
# error: arm_neon.h: No such file"). Lo que si se puede fijar aqui, y se fija, es que
# el cable del motor siga exactamente como lo pinó Harness12: la llamada CONTADA
# (AskCounted) con el getLookupCount() vivo, y que la linea de cierre lleve el
# denominador impreso a la vista, la relectura EpochStable del seqlock, los dos bumps de
# epoca (mVUreset con Invalidate, mVUclose antes de mVUdeleteProg), la microMemWriteGen
# como primera linea de mVUclear, la ventana cacheEnd = x86end + safe zone, que el
# bookkeeping (prog.isSame / prog.cur / quick.block / observed) sigue DESPUES de la
# decision del memo y no dentro de un camino propio, la guarda !doConstProp en los dos
# sitios, y el alta del header en CMakeLists. Son comprobaciones de TEXTO, no de
# compile: se dicen asi, con esa limitacion escrita, en vez de fingir que cubren lo
# que el CI cubre.
#
# Un arnes que queda verde con el codigo saboteado no verifica nada, asi que esto es
# parte de la evidencia y no un extra: sin esta lista, los "756 comprobaciones, 0
# fallos" de Harness12 son solo un numero.
#
# Que NO es: no prueba rendimiento (eso lo mide tools/medicion), no prueba nada en el
# telefono, y no prueba el motor completo. Un mutante que escapa aqui significa que el
# arnes tiene un hueco de cobertura; se arregla el arnes antes de cerrar la version, no
# se "documenta" el escape como aceptable.
#
# Uso: bash tools/arneses/mutacion_h12.sh  (RC=0 solo si los TREINTA mutantes de
# fase 1 fueron detectados, las DIECIOCHO anclas de fase 2 aparecieron, y ambos controles
# dieron verde/rojo lo que tienen que dar). Requiere g++ en el host.
#
# Trampa que este script ya piso y por eso esta escrita aqui: en h11 la primera version
# copiaba el encabezado ORIGINAL dentro de la caja en cada ejecucion, o sea por encima
# del mutante, y siete mutantes salieron "NO DETECTADO" no por arnes debil sino porque
# se ejecutaba codigo limpio. Aqui la caja se limpia UNA vez y mut() es lo unico que
# escribe MvuFastPath.h; ademas el CONTROL (caja sin mutar) corre primero, porque sin
# el, un RC=1 por cualquier otra causa contaria como "detectado".
#
# Otra trampa, propia de esta fase 2: el primer intento usaba anclas largas con tabs
# copiados a mano y fallaba por el espaciado, no por el codigo. Las anclas de aqui son
# cadenas cortas y unicas, y las que necesitan orden (bookkeeping despues de la
# decision, microMemWriteGen primero en mVUclear) se comprueban con posicion relativa,
# no con un bloque literal.
set -u

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
cd "$root" || exit 1

SRC=app/src/main/cpp/pcsx2/arm64/MvuFastPath.h
CPP=app/src/main/cpp/pcsx2/arm64/microVU-arm64.cpp
CML=app/src/main/cpp/pcsx2/CMakeLists.txt
TEST=tools/arneses/h12-src/fastpath_test.cpp
box=/tmp/h12mut

[ -f "$SRC" ]  || { echo "mutacion h12: no existe $SRC (ruta relativa al repo)"; exit 1; }
[ -f "$CPP" ]  || { echo "mutacion h12: no existe $CPP"; exit 1; }
[ -f "$CML" ]  || { echo "mutacion h12: no existe $CML"; exit 1; }
[ -f "$TEST" ] || { echo "mutacion h12: no existe $TEST"; exit 1; }
command -v g++ >/dev/null 2>&1 || { echo "mutacion h12: hace falta g++ en el host"; exit 1; }

rm -rf "$box"; mkdir -p "$box"
cp "$TEST" "$box/"

undetected=0; escaped=0; total=0

# mut <ancla> <reemplazo>: parte SIEMPRE del archivo original del repo y escribe solo
# la caja. RC=3 si la ancla no aparece exacta (el mutante no se aplico y cualquier
# resultado posterior seria de otro codigo). RC=4 si aparece mas de una vez: un
# replace(...,1) silencioso sobre un ancla ambigua dejaria de saber que se rompio.
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
open('$box/MvuFastPath.h','w').write(s.replace(a,b,1))
" "$1" "$2"
}

# run_mut <nombre> <descripcion> <rojo esperado>: compila y ejecuta el arnes contra lo
# que hay en la caja. El "rojo esperado" es lo que tiene que aparecer en la primera
# linea de fallo; si el arnes se pone rojo en OTRA parte, se dice, porque puede ser
# senal de un verde por accidente en la comprobacion que se queria probar.
run_mut() {
  local name=$1 desc=$2 expect=$3
  total=$((total+1))
  if ! g++ -std=c++17 -Wall -Wextra -I"$box" -o "$box/h12_$name" "$box/fastpath_test.cpp" 2>"$box/err_$name"; then
    echo "MUTANTE $name ($desc): no compila => DETECTADO en el compile"
    head -3 "$box/err_$name"
    return
  fi
  local out red
  out=$("$box/h12_$name" 2>/dev/null | grep -m1 "^FALLO:")
  if [ -z "$out" ]; then
    # Verde o caida sin comprobacion. Un arnes que no termina (segfault por escribir
    # fuera del arreglo) TAMPOCO es un rojo valido: se cuenta como no detectado y se
    # investiga, no se celebra.
    if "$box/h12_$name" >/dev/null 2>&1; then
      echo "MUTANTE $name ($desc): VERDE con el codigo roto => NO DETECTADO"
    else
      echo "MUTANTE $name ($desc): SIN RECUESTO (RC!=0 sin FALLO) => NO DETECTADO, investigar"
    fi
    undetected=$((undetected+1)); return
  fi
  case "$out" in
    *"$expect"*) echo "MUTANTE $name ($desc): DETECTADO en la comprobacion esperada";;
    *) echo "MUTANTE $name ($desc): DETECTADO pero en otra comprobacion (esperada ~\"$expect\")"
       echo "  rojo real: ${out:0:150}";;
  esac
}

echo "== mutacion Harness12 / MvuFastPath.h (fase 1) =="
echo

# CONTROL: la caja sin mutar tiene que dar verde. Sin esto, un rojo de abajo puede ser
# del script y no del mutante.
cp "$SRC" "$box/MvuFastPath.h"
if g++ -std=c++17 -Wall -Wextra -I"$box" -o "$box/h12_control" "$box/fastpath_test.cpp" 2>/dev/null \
   && "$box/h12_control" 2>/dev/null | grep -q ", 0 Fallos"; then
  echo "CONTROL (caja sin mutar): verde => un rojo de abajo es del mutante."
else
  echo "CONTROL (caja sin mutar): NO ESTA VERDE => todo lo de abajo no vale nada"
  "$box/h12_control" 2>/dev/null | head -5
  escaped=$((escaped+1))
fi
echo

# m1: IdxOf >>4. El dominio de VU1 es de a 8 bytes; con >>4 la mitad de los PCs de
# despacho mira la casilla de otro PC. Rojo esperado: la bijeccion del dominio.
mut 'return (pcBytes >> 3) & kSlotMask;' 'return (pcBytes >> 4) & kSlotMask;' \
	&& run_mut m1 "IdxOf >>4 en vez de >>3" "IdxOf(8)=0" || escaped=$((escaped+1))

# m2: la mascara del flaghack se ignora y siempre se compara exacto. Con el speedhack
# de banderas puesto, el memo negaria aciertos que el barrido si da.
mut 'return flagHack ? ~kFlagMask : ~0ull;' 'return ~0ull;' \
	&& run_mut m2 "MaskOf ignora el flaghack" "MaskOf(1)" || escaped=$((escaped+1))

# m3: lo contrario: la mascara siempre activa, o sea el memo serviria por una clave que
# el barrido exacto considera DISTINTA. Este es el rojo gordo: ejecutar otro bloque.
mut 'return flagHack ? ~kFlagMask : ~0ull;' 'return ~kFlagMask;' \
	&& run_mut m3 "MaskOf enmascara siempre" "MaskOf(0)" || escaped=$((escaped+1))

# m4: umbral 8 -> 1. Memorizar aciertos de barrido corto: el memo pasa a costar mas que
# lo que reemplaza (la ley de costo escrita en el header).
mut 'static constexpr uint32_t kMinRecordPos = 8u;' 'static constexpr uint32_t kMinRecordPos = 1u;' \
	&& run_mut m4 "kMinRecordPos 8 -> 1" "umbral" || escaped=$((escaped+1))

# m5: borde del umbral roto dentro de DecideAsk (la "<" pasa a "<="). La condicion ya
# no vive en un bool de una linea: desde 0.6.36 DecideAsk es LA decision y ShouldAsk es
# su proyeccion, asi que el ancla es la linea del umbral. El rojo esperado sigue siendo
# ShouldAsk, y esto es una mejora real del arnes: al romper el nucleo se rompen las dos
# proyecciones a la vez, o sea una guarda no se puede romper "solo para la telemetria".
mut '			if (lookupCount < static_cast<int>(kMinRecordPos))' \
	'			if (lookupCount <= static_cast<int>(kMinRecordPos))' \
	&& run_mut m5 "DecideAsk: borde del umbral < por <=" "ShouldAsk" || escaped=$((escaped+1))

# m6: DecideAsk deja de excluir los despachos exactos. La rama exacta compara 96 bytes
# y promociona MRU en fBlockList: memoizarla sin replicar esos efectos es otra semantica.
# Se anula la guarda, no se borca el case: borrar el case dejaria el switch sin cubrir
# kSkipExact y el compiler avisaria, o sea el mutante se detectaria por compile y no por
# comprobacion — que es un verde falso sobre lo que se quiere probar aqui.
mut '			if (needExactMatch != 0u)
				return kSkipExact;' '			if (false)
				return kSkipExact;' \
	&& run_mut m6 "DecideAsk acepta exactos" "ShouldAsk" || escaped=$((escaped+1))

# m7: EpochStable siempre verdadero: el seqlock del llamador deja de descartar un
# acierto leido a mitad de mVUreset.
mut 'return epochNow == epochAtAsk;' 'return true;' \
	&& run_mut m7 "EpochStable siempre true" "EpochStable" || escaped=$((escaped+1))

# m8: marcador de casilla vacia = 0. El PC 0 es un PC de despacho VALIDO, asi que una
# entrada real del PC 0 se volvera invisible y, peor, una casilla a medio escribir se
# leera como "PC 0".
mut 'static constexpr uint32_t kEmptyPc = 0xffffffffu;' 'static constexpr uint32_t kEmptyPc = 0u;' \
	&& run_mut m8 "kEmptyPc = 0" "kEmptyPc" || escaped=$((escaped+1))

# m9: Serve deja de mirar q.exact. Un despacho con needExactMatch recibiria una respuesta
# de la rama rapida: el peor caso, porque el exacto puede tener otra hostEntry.
mut '			if (q.exact)
			{
				stats.rejectExact++;
				return false;
			}' '' \
	&& run_mut m9 "Serve ignora exact" "needExactMatch" || escaped=$((escaped+1))

# m10: la comprobacion del PC fuera. Con IdxOf biyectiva parece redundante — y por eso
# se queda: es lo unico que para el desastre si alguien mueve kSlots o el corrimiento.
mut '			if (e.pc != q.pc)
			{
				stats.rejectPc++;
				return false;
			}' '' \
	&& run_mut m10 "Serve no comprueba el PC" "PC de al lado" || escaped=$((escaped+1))

# m11: la comprobacion del VU fuera (una tabla de VU1 contestando a VU0).
mut '			if (e.vuId != q.vuId)
			{
				stats.rejectVu++;
				return false;
			}' '' \
	&& run_mut m11 "Serve no comprueba el VU" "otro VU" || escaped=$((escaped+1))

# m12: las epocas fuera. Sin cacheEpoch, una entrada de antes de un reset seguiria
# sirviendo una direccion de un code cache ya destruido; sin candidateEpoch, lo mismo con
# microcodigo reescrito. Este mutante elimina AMBAS porque viven en la misma linea.
mut '			if (e.cacheEpoch != q.cacheEpoch || e.candidateEpoch != q.candidateEpoch)
			{
				stats.rejectEpoch++;
				return false;
			}' '' \
	&& run_mut m12 "Serve no comprueba epocas" "epoca" || escaped=$((escaped+1))

# m13: progId fuera. El allocator puede reasignar la misma direccion de programa en otra
# epoca; con la epoca ignorada aqui, la entrada vieja describe otro programa.
mut '			if (e.progId != q.progId)
			{
				stats.rejectProg++;
				return false;
			}' '' \
	&& run_mut m13 "Serve no comprueba progId" "programa" || escaped=$((escaped+1))

# m14: mgrId fuera, el caso del gestor reasignado con la MISMA direccion.
mut '			if (e.mgrId != q.mgrId)
			{
				stats.rejectMgr++;
				return false;
			}' '' \
	&& run_mut m14 "Serve no comprueba mgrId" "gestor" || escaped=$((escaped+1))

# m15: la clave fuera. Es la comprobacion que hace que el memo sea el barrido y no un
# adivino: sin quickKey, la casilla sirve lo que fuera que se registro en ese PC.
mut '			if (e.flagHack != q.flagHack ||
				e.quickKey != (q.quick64 & MaskOf(q.flagHack)))
			{
				stats.rejectKey++;
				return false;
			}' '' \
	&& run_mut m15 "Serve no comprueba la clave" "otra mascara" || escaped=$((escaped+1))

# m16: la ventana de limites fuera. Un hostEntry de un cache ya no existente es un salto
# a codigo libre: este es el rojo que no es "otro bloque" sino EjECUCION de nada.
mut '			if (e.hostEntry < q.cacheStart || e.hostEntry >= q.cacheEnd)
			{
				// El puntero no esta dentro del cache vivo: la casilla ya no
				// describe nada ejecutable. Se limpia ahi mismo para que no se
				// vuelva a intentar con el mismo contenido.
				stats.rejectBounds++;
				e.pc = kEmptyPc;
				return false;
			}' '' \
	&& run_mut m16 "Serve no comprueba bounds" "[start, end)" || escaped=$((escaped+1))

# m17: bounds al reves (acepta fuera, rechaza dentro).
mut 'if (e.hostEntry < q.cacheStart || e.hostEntry >= q.cacheEnd)' \
	'if (e.hostEntry >= q.cacheStart && e.hostEntry < q.cacheEnd)' \
	&& run_mut m17 "bounds invertido en Serve" "no hablan en los mismos terminos" || escaped=$((escaped+1))

# m18: bounds no limpia la casilla. El contrato dice que si: sin eso, cada despacho
# siguiente paga el mismo rechazo y el memo es solo costo.
mut '				stats.rejectBounds++;
				e.pc = kEmptyPc;' '				stats.rejectBounds++;' \
	&& run_mut m18 "bounds no limpia la casilla" "sobrevivio" || escaped=$((escaped+1))

# m19: Record sin el umbral de posicion: memoriza barridos cortos.
mut 'if (q.exact || q.vuId != kVU1 || walkPos < kMinRecordPos)' \
	'if (q.exact || q.vuId != kVU1)' \
	&& run_mut m19 "Record sin umbral de posicion" "posicion 0" || escaped=$((escaped+1))

# m20: Record sin la ventana. Escribe una direccion que no esta en el cache vivo.
mut 'if (hostEntry < q.cacheStart || hostEntry >= q.cacheEnd)' \
	'if (false)' \
	&& run_mut m20 "Record sin ventana" "limite alto" || escaped=$((escaped+1))

# m21: Record guarda la clave SIN mascara. Al servir, Serve compara enmascarada contra
# guardada sin enmascarar: con el speedhack puesto el memo se calla para siempre (el
# fallo silencioso de una optimizacion muerta).
mut 'e.quickKey = q.quick64 & MaskOf(q.flagHack);' 'e.quickKey = q.quick64;' \
	&& run_mut m21 "Record guarda la clave sin mascara" "primer match" || escaped=$((escaped+1))

# m22: Record no guarda la epoca del microcodigo: toda entrada queda con candidateEpoch
# 0 y un mundo con generacion != 0 no se sirve nunca... o se sirve contra otro micro.
mut 'e.candidateEpoch = q.candidateEpoch;' 'e.candidateEpoch = 0;' \
	&& run_mut m22 "Record no guarda candidateEpoch" "no hablan en los mismos terminos" || escaped=$((escaped+1))

# m23: Record olvida escribir hostEntry. Sin esto la salida de Serve es basura.
mut 'e.hostEntry = hostEntry;' 'e.hostEntry = 0;' \
	&& run_mut m23 "Record no guarda hostEntry" "sirvio" || escaped=$((escaped+1))

# m24: Serve consulta la casilla de OTRO hueco (idx+1). Un indice mal usado: la casilla
# del vecino puede tener otro PC, y ahi entra rejectPc; el arnes lo caza porque espera
# que el acierto este en la suya.
mut 'Entry& e = at[IdxOf(q.pc)];

			if (e.pc == kEmptyPc)' 'Entry& e = at[(IdxOf(q.pc) + 1u) & kSlotMask];

			if (e.pc == kEmptyPc)' \
	&& run_mut m24 "Serve lee la casilla del vecino" "no hablan en los mismos terminos" || escaped=$((escaped+1))

# m25: Invalidate borra tambien los contadores. La telemetria del cierre dejaria de ser
# de sesion: cada cambio de config o carga de estado reinicia las cuentas, y el informe
# del telefono diria "casi nadie uso el memo".
mut '			for (uint32_t i = 0; i < kSlots; i++)
				at[i].pc = kEmptyPc;
			stats.invalidations++;' '			for (uint32_t i = 0; i < kSlots; i++)
				at[i].pc = kEmptyPc;
			std::memset(&stats, 0, sizeof(stats));
			stats.invalidations++;' \
	&& run_mut m25 "Invalidate borra contadores" "sesion" || escaped=$((escaped+1))

# m26: Reset no limpia stats: los contadores de una corrida anterior sobreviven al
# arranque del VM, y las guardas "consults == 0 con lista corta" dejan de significar
# nada porque llegan con saldo viejo.
mut '			for (uint32_t i = 0; i < kSlots; i++)
				at[i].pc = kEmptyPc;
			std::memset(&stats, 0, sizeof(stats));
		}

		static uint32_t IdxOf' '			for (uint32_t i = 0; i < kSlots; i++)
				at[i].pc = kEmptyPc;
		}

		static uint32_t IdxOf' \
	&& run_mut m26 "Reset no limpia stats" "consults" || escaped=$((escaped+1))

# ---- Los cuatro de abajo son de Cenit 0.6.36 y rompen LA CUENTA, no la decision.
# ---- Valen la pena porque el producto de esta fase es un numero que el telefono
# ---- imprime: un denominador mal sumado no tira nada, solo miente, y un arnes que
# ---- solo mira "que se sirvio" no lo ve. Cada uno apunta a un CHECK distinto del
# ---- despacho compuesto (h12-src/fastpath_test.cpp, DispatchLikeTheCaller).

# m27: el denominador no se suma. Es exactamente el defecto que esta version viene a
# cerrar, hecho codigo: la tabla funciona, la decision funciona, y el log dice
# lookups=0 para siempre — o sea "nadie uso el memo" sin ser verdad.
mut '			stats.lookups++;
' '' \
	&& run_mut m27 "AskCounted no suma lookups" "sumo" || escaped=$((escaped+1))

# m28: el salto corto se decide pero no se cuenta. skip-short=0 en un juego cuyos PCs
# nunca llegan al umbral se lee como "el filtro esta roto" cuando lo que esta roto es la
# guarda que lo cuenta.
mut '				case kSkipShort:
					stats.skipShort++;
					break;' '				case kSkipShort:
					break;' \
	&& run_mut m28 "no-preguntar corto no se cuenta" "invariante del cierre" || escaped=$((escaped+1))

# m29: el motivo se cuenta en el contador EQUIVOCADO (un exacto suma a skip-short). La
# suma total sigue cuadrada — o sea este defecto es invisible para un invariante de
# sesion y solo lo ve la comprobacion por despacho que mira CUAL se movio.
mut '				case kSkipExact:
					stats.skipExact++;
					break;' '				case kSkipExact:
					stats.skipShort++;
					break;' \
	&& run_mut m29 "exacto contado como salto corto" "era EXACTO" || escaped=$((escaped+1))

# m30: AskCounted pregunta SIN haberlo decidido (el case kAsk cae al break). Aqui no hay
# suma que valga: es la guarda de dominio rota por la via del switch, y se ve en el
# borde de ShouldAsk de testDomain, que corre antes que cualquier despacho.
mut '				case kAsk:
					return true;' '				case kAsk:
					stats.skipShort++;
					break;' \
	&& run_mut m30 "AskCounted dice no-preguntar y pregunta igual" "salto sin motivo" || escaped=$((escaped+1))

echo
echo "== fase 2: anclas del TU del motor (microVU-arm64.cpp / CMakeLists) =="
# El TU no se puede analizar en host: microVU-arm64.h incluye <arm_neon.h>, que no esta
# en el host (el intento da fatal error). Se comprueba el TEXTO del cable, con la
# limitacion escrita: esto fija que la integracion es la que Harness12 modela, no que el
# motor compile (eso lo hace el CI).
anchor() { # <nombre> <descripcion> <archivo> <ancla> [vezes]
	local name=$1 desc=$2 file=$3 a=$4 want=${5:-1}
	total=$((total+1))
	local n
	n=$(python3 -c "
import sys
print(open(sys.argv[1]).read().count(sys.argv[2]))
" "$file" "$a")
	if [ "$n" -eq "$want" ]; then
		echo "ANCLA $name ($desc): $n aparicion(es) => FIJADA"
	else
		echo "ANCLA $name ($desc): $n aparicion(es), se esperaba $want => NO FIJADA"
		escaped=$((escaped+1))
	fi
}

order() { # <nombre> <descripcion> <archivo> <antes> <despues>
	local name=$1 desc=$2 file=$3 a=$4 b=$5
	total=$((total+1))
	local r
	# El segundo ancla se busca DESPUES de la primera (find(b, i)), no desde el
	# principio. Es la diferencia entre "van en este orden" y "ambas existen": el
	# primer intento de esta funcion buscaba b desde 0 y f15/f17 salian mal porque
	# "mVU.prog.quick[i].block = NULL;" aparece tres veces (mVUclear tiene dos ramas)
	# y "mVU.prog.isSame = -1;" dos. Con busqueda desde i el ancla sigue anclando y
	# el que se prueba es el orden de verdad.
	r=$(python3 -c "
import sys
s=open(sys.argv[1]).read()
i=s.find(sys.argv[2])
j=s.find(sys.argv[3], i + 1) if i >= 0 else -1
print('ok' if (i>=0 and j>=0) else 'mal(%d,%d)'%(i,j))
" "$file" "$a" "$b")
	if [ "$r" = "ok" ]; then
		echo "ANCLA $name ($desc): orden correcto => FIJADA"
	else
		echo "ANCLA $name ($desc): $r => NO FIJADA"
		escaped=$((escaped+1))
	fi
}

anchor f1 "la guarda del llamador usa la version CONTADA con el getLookupCount() vivo" "$CPP" \
	"mVUFastPath::g_vu1.AskCounted(key->needExactMatch, block->getLookupCount())"
anchor f2 "el seqlock re-lee la epoca despues del acierto" "$CPP" \
	"mVUFastPath::Table::EpochStable(epochBefore, mVU.codeCacheEpoch)"
anchor f3 "el memo se apaga con doConstProp (guarda de plantilla)" "$CPP" \
	"if constexpr (vuIndex == 1 && !doConstProp)" 2
anchor f4 "el bump de epoca en mVUreset va con Invalidate" "$CPP" \
	"mVUFastPath::g_vu1.Invalidate();"
anchor f5 "los dos bumps de epoca (mVUreset y mVUclose)" "$CPP" "mVU.codeCacheEpoch++;" 2
anchor f6 "la epoca nace en 0 en mVUinit" "$CPP" "mVU.codeCacheEpoch = 0;"
anchor f7 "la tabla se Reset()ea en mVUinit" "$CPP" "mVUFastPath::g_vu1.Reset();"
anchor f8 "candidateEpoch = generacion de escrituras micro" "$CPP" \
	"ask.q.candidateEpoch = mVU.microMemWriteGen;"
anchor f9 "cacheEnd = x86end + safe zone (la misma ventana que mVUexecute)" "$CPP" \
	"ask.q.cacheEnd = reinterpret_cast<u64>(mVU.prog.x86end) + (mVUcacheSafeZone * _1mb);"
anchor f10 "Record solo con epoca estable de principio a fin" "$CPP" \
	"if (ask.asked && mVU.codeCacheEpoch == ask.epochAtAsk)"
anchor f11 "search pide la posicion solo cuando se pregunto" "$CPP" \
	"block->search(mVU, (microRegInfo*)pState, ask.asked ? &walkPos : nullptr)"
anchor f12 "la telemetria de sesion se vuelca en el cierre CON el denominador" "$CPP" \
	"Console.WriteLn(\"Cenit fastpath VU1: lookups=%llu (consults=%llu + skip-exact=%llu + skip-short=%llu)"
anchor f13 "el header esta dado de alta en el build" "$CML" "arm64/MvuFastPath.h"
anchor f14 "la generacion micro sube en mVUclear" "$CPP" "mVU.microMemWriteGen++;"

# Ordenes: tres cosas que solo existen como orden, y un memo con el orden roto es otro
# semantica, no una optimizacion.
order f15 "la microMemWriteGen sube ANTES de invalidar quick[] en mVUclear" "$CPP" \
	"mVU.microMemWriteGen++;" "mVU.prog.quick[i].block = NULL;"
order f16 "el bump+Invalidate de mVUreset vive ANTES de rebobinar el cursor del cache" "$CPP" \
	"mVUFastPath::g_vu1.Invalidate();" "armSetAsmPtr(mVU.cache, cacheCapacity, nullptr);"
order f17 "el bookkeeping compartido va DESPUES de la decision del memo" "$CPP" \
	"MvuFastPathRecord<vuIndex>(mVU, ask, entry, walkPos);" "mVU.prog.isSame = -1;"
order f18 "el bump de mVUclose va ANTES de los unicos free() de programa/gestor" "$CPP" \
	"mVU.codeCacheEpoch++;" "mVUdeleteProg(mVU, prog);"

echo
echo "mutacion h12: no detectados=$undetected anclas-fallidas=$((escaped)) sobre $total casos (esperados 30 mutantes + 18 anclas)"
[ "$total" -eq 48 ] || { echo "mutacion h12: la cuenta de casos no cuadra"; exit 1; }
[ $undetected -eq 0 ] && [ "$escaped" -eq 0 ] || exit 1
echo "mutacion h12: los 30 mutantes fueron detectados y las 18 anclas fijadas."
