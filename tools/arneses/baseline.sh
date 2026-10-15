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
# para h9, h10 y h11 (los tres del superbloque: la maquina de estados, la sintaxis
# del area arm64 y la logica del indice PC->hueco). Sin g++ esos tres avisan y
# dejan RC=1: no compilarlos NO es un verde.
set -u
JAVAC=${JAVAC:-/usr/lib/jvm/temurin-21/bin/javac}
JAVA=${JAVA:-/usr/lib/jvm/temurin-21/bin/java}
[ -x "$JAVAC" ] || JAVAC=javac
[ -x "$JAVA" ] || JAVA=java
CXX=${CXX:-g++}
command -v "$CXX" >/dev/null 2>&1 || CXX=

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC=$(cd "$HERE/../../app/src/main/java/com/izzy2lost/psx2" && pwd)
CPP=$(cd "$HERE/../../app/src/main/cpp/pcsx2/arm64" && pwd)
GCEN=$(cd "$HERE/../../app/src/main/cpp/pcsx2/GS/Renderers/Common" && pwd)
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

runh10() { # arnes de SINTAXIS del area arm64: tres patas, y cada una cuenta.
  #
  # Pata A (autocontencion): cada cabecera del area se compila SOLA. CI no hace esto —
  #   compila los .cpp, y un header que "casualmente" funciona porque otro ya trajo sus
  #   tipos se cae en el telefono de otra manera o no, pero nunca lo ves.
  # Pata B (contrato): un TU que fija con static_assert las leyes de promocion, los
  #   ocho ordinales, la mascara de 14 regiones y las FIRMAS del puente. Las firmas se
  #   comprueban asignando punteros de funcion: una firma distinta no se puede asignar,
  #   asi que cambiar SbFinish/HudSummary sin cambiar al llamador se ve aqui, no 25
  #   minutos despues en CI.
  # Pata C (el TU del motor ENTERO en host): MvuSuperblock-arm64.cpp vive libre de vixl
  #   y de arm_neon a proposito, asi que se puede analizar con g++ de x86 contra el
  #   arbol REAL del repo, con el fmt real de 3rdparty (no hay shim: fmt esta en el
  #   arbol, lo descubri al intentar el uno). El arnes nunca enlaza, y eso acota lo que
  #   prueba: sintaxis y tipos del TU completo, no el contenido de las cadenas de
  #   formato. El camino printf de Console (DevCon.WriteLn / Console.Error, que es el
  #   que usa el motor) NO lleva atributo format en el repo, asi que quitar un argumento
  #   aqui pasa verde — se probo, y por eso la cuenta especificador/argumento la hace
  #   jnidict.py a mano sobre HudSummary y la auditoria de SbPrintf sigue siendo humana.
  #   -std=gnu++20 y -msse4.1 son LOS FLAGS DEL CI, leidos del log del run #64 (clang++
  #   del NDK compila pcsx2 con -std=gnu++20) y de common/VectorIntrin.h. Con -std=c++17
  #   el arbol del repo no pasa (usa std::memory_order::relaxed con qualificador, que es
  #   C++20): un standard elegido a ojo daria rojos que no son defectos nuestros.
  #
  # Nace de dos fallos reales de 0.6.32: una linea de prosa en espanol quedo FUERA de un
  # comentario en MvuSuperblock-arm64.h, y despues se uso `i32`, que no existe en PCSX2
  # (los alias son s32/u32). h5..h8 son Java y h9 solo mira MvuSbLife.h, asi que ni la
  # cabecera publica del motor ni su .cpp los verificaba nadie en host; el CI los tardo
  # 25 minutos y el segundo ni siquiera llego a verse (ninja paro antes). La pata C
  # encontro un TERCERO que el CI aun no habia visto: dos memset que borraban el
  # candidato a cero DESPUES de que Candidate pasara a contener la maquina, cuyos
  # valores por defecto ya no son cero (Evidence::windows=1 y gainPermille=-1 «sin
  # medir»). Borrado a bytes, el motor habria dicho «ganancia medida 0» de un candidato
  # que nunca se midio. Y la mutacion de control encontro un defecto DEL VERIFICADOR:
  # el filtro de rutas era [A-Za-z-]* y no aceptaba el «64» del nombre de archivo, o sea
  # que dejaba pasar cualquier aviso del area. Un verde que no mira es peor que no medir.
  local box=$WORK/h10
  if [ -z "$CXX" ]; then
    echo "h10: SIN COMPILAR - no hay g++ de host (CXX='$CXX'); las cabeceras del area arm64 NO se verificaron en esta corrida"
    RC=1
    return
  fi
  local FMT="$ROOT/app/src/main/cpp/3rdparty/fmt/include"
  if [ ! -f "$FMT/fmt/base.h" ]; then
    # Sin fmt no se pasa de common/Console.h, y por tanto NO se puede mirar el TU del
    # motor. Un verde sin esta comprobacion seria el verde vacio del que este script
    # existe para huir: se ruida y se deja RC=1.
    echo "h10: SIN COMPROBACION DE TU - no hay fmt en $FMT; el arbol del motor no se analizo"
    RC=1
    return
  fi
  # -std=gnu++20 y -msse4.1: LO QUE EL CI USA. El log del run #64 dice que el clang++
  # del NDK compila pcsx2 con -std=gnu++20, y common/VectorIntrin.h exige SSE 4.1.
  # Usar otro standard daria un arnes que discute con el arbol real (con -std=c++17 el
  # repo no pasa: usa std::memory_order::relaxed con qualificador), o sea un rojo que
  # no es un defecto nuestro.
  local STD=-std=gnu++20
  local INC="-I$ROOT/app/src/main/cpp -I$ROOT/app/src/main/cpp/pcsx2 -I$ROOT/app/src/main/cpp/common -I$CPP -I$FMT"
  rm -rf "$box"; mkdir -p "$box"
  cp "$HERE/h10-src/hdr_tu.cpp" "$HERE/h10-src/selfcont_tu.cpp" "$box/" || { echo "h10: no se pudo traer el arnes"; RC=1; return; }
  # Las cabeceras se traen DEL REPO en cada corrida (regla anti-copia-vieja de h5..h9) y
  # la caja va PRIMERA en la ruta de includes, asi que las tres patas ven siempre el
  # arbol vivo. La pata A ademas la usa aislada (-DH10_HEADER): lo que prueba es que el
  # header funciona SOLO, sin que otro include le haya traido sus tipos antes.
  # MvuFastPath.h (0.6.35) entra en la lista por la misma razon que las otras: el CI la
  # includea desde microVU-arm64.h, y h12 la prueba en host con g++ -std=c++17. Esta pata
  # la prueba AISLADA y con el standard del CI (gnu++20), que es lo que ni h12 ni el CI
  # comprueban por si solos.
  local headers="MvuSbLife.h MvuSbIndex.h MvuSuperblock-arm64.h MvuTraceProbe-arm64.h MvuFastPath.h MvuSbEligibility.h"
  for c in $headers; do
    if [ ! -f "$CPP/$c" ]; then echo "h10: FALTA $c en el repo ($CPP)"; RC=1; return 1; fi
    cp "$CPP/$c" "$box/" || { echo "h10: no se pudo copiar $c"; RC=1; return 1; }
  done
  local n=0
  for c in $headers; do
    if ! "$CXX" $STD -fsyntax-only -Wall -Wextra -I"$box" $INC -DH10_HEADER=$c "$box/selfcont_tu.cpp" 2>"$box/sc.err"; then
      echo "h10: $c NO ES AUTOCONTENIDA (depende de que otro include aparezca antes)"
      head -20 "$box/sc.err"; RC=1; return 1
    fi
    n=$((n+1))
  done
  if ! "$CXX" $STD -fsyntax-only -Wall -Wextra -I"$box" $INC "$box/hdr_tu.cpp" 2>"$box/contract.err"; then
    echo "h10: FALLO DE CONTRATO EN CABECERAS arm64 (ordinales, leyes, mascara o firmas)"; head -30 "$box/contract.err"; RC=1; return 1
  fi
  local b s
  b=$(grep -c "static_assert" "$box/hdr_tu.cpp")
  s=$(grep -c "\*const s_fn" "$box/hdr_tu.cpp")

  # --- pata C: el TU del motor completo ---
  # El umbral es CERO diagnosticos propios, no solo RC=0: el arbol del repo (Console.h)
  # trae avisos ajenos a esta fase, y filtrar por ruta del area arm64 evita tanto el
  # falso rojo como tapar un aviso nuestro.
  local eng="$CPP/MvuSuperblock-arm64.cpp"
  if [ ! -f "$eng" ]; then echo "h10: FALTA MvuSuperblock-arm64.cpp en el repo"; RC=1; return 1; fi
  if ! "$CXX" $STD -fsyntax-only -Wall -Wextra -msse4.1 -mpopcnt $INC "$eng" 2>"$box/tu.err"; then
    echo "h10: EL TU DEL MOTOR NO COMPILA EN HOST"
    grep -E "arm64/Mvu[A-Za-z0-9-]*\.(cpp|h):" "$box/tu.err" | head -20; RC=1; return 1
  fi
  local own
  own=$(grep -cE "arm64/Mvu[A-Za-z0-9-]*\.(cpp|h):[0-9]+:[0-9]+: (error|warning)" "$box/tu.err")
  if [ "$own" != "0" ]; then
    echo "h10: el TU del motor compila pero con $own diagnosticos propios del area arm64"
    grep -E "arm64/Mvu[A-Za-z0-9-]*\.(cpp|h):[0-9]+:[0-9]+: (error|warning)" "$box/tu.err" | head -12
    RC=1; return 1
  fi

  # Recuento honesto: lo que el arnes AFIRMA. Cada static_assert es una comprobacion,
  # cada puntero de funcion es una firma atada, cada cabecera que compilo sola es una
  # autocontencion, y el TU del motor limpio es una mas.
  echo "Harness10: $((n + b + s + 1)) Comprobaciones, 0 Fallos"
}

runh11() { # arnes de LOGICA del indice PC->hueco del superbloque (0.6.33).
  #
  # MvuSbIndex.h es C++ puro y sin dependencias a proposito, igual que MvuSbLife.h,
  # para poder verificarlo en host. Que hace falta está medido, no supuesto: SbResolve
  # corre en CADA despacho de VU1, y antes de este cambio barría los 256 huecos de la
  # tabla en cada uno (el encabezado decía «escaneo O(vivos)» y era falso). Un indice
  # que se equivoca no cuesta velocidad: cuesta servir un superbloque en un PC que no
  # es el suyo, o sea ejecutar código que nadie validó. Por eso el arnes no mira
  # «funciona rápido» sino EQUIVALENCIA contra el barrido lineal, casilla por casilla,
  # despues de cada una de las 20000 operaciones del diferencial.
  #
  # Que NO prueba este arnes, y hay que decirlo para que nadie lea de más en su verde:
  # que el MOTOR llame a Bind/Unbind en los momentos correctos. Eso vive en
  # MvuSuperblock-arm64.cpp y lo cubre la autoverificación de ese archivo (cada
  # kSbIndexAuditPeriod despachos SbResolve hace además el barrido y compara; si
  # difieren el indice se auto-destituye el resto de la sesión) y la auditoría
  # casilla-por-casilla de DumpReport. En host no hay dispatcher.
  #
  # El arnes además se auto-vale: si la secuencia no pasó por los cinco caminos
  # (armar, reasignar, liberar, rechazar, idempotente) el verde se declara inválido y
  # ruida. Diez mil comprobaciones que nunca tocaron el camino raro no son evidencia.
  local box=$WORK/h11
  if [ -z "$CXX" ]; then
    echo "h11: SIN COMPILAR - no hay g++ de host (CXX='$CXX'); la logica del indice PC->hueco NO se verifico en esta corrida"
    RC=1
    return
  fi
  rm -rf "$box"; mkdir -p "$box"
  cp "$HERE/h11-src/sb_index_test.cpp" "$box/" || { echo "h11: no se pudo traer el arnes"; RC=1; return; }
  # Regla anti-copia-vieja: el indice se trae DEL REPO en cada corrida.
  if [ ! -f "$CPP/MvuSbIndex.h" ]; then echo "h11: FALTA MvuSbIndex.h en el repo ($CPP)"; RC=1; return; fi
  cp "$CPP/MvuSbIndex.h" "$box/" || { echo "h11: no se pudo copiar MvuSbIndex.h"; RC=1; return; }
  # -std=c++17 basta y es deliberado: el indice no usa nada de C++20, asi que si algun
  # dia necesita un standard mas alto se ve aqui en vez de descubrirlo en el CI.
  if ! "$CXX" -std=c++17 -Wall -Wextra -I"$box" -o "$box/h11" "$box/sb_index_test.cpp" 2>"$box/compile.err"; then
    echo "h11: FALLO DE COMPILACION"; head -40 "$box/compile.err"; RC=1; return
  fi
  OUT=$("$box/h11" 2>/dev/null | grep -E "Comprobaciones|FALLO")
  echo "$OUT"
  echo "$OUT" | grep -qE ", [1-9][0-9]* Fallos" && RC=1
  echo "$OUT" | grep -q "Comprobaciones" || { echo "h11: el arnes no imprime recuento"; RC=1; }
}

runh12() { # arnes de LOGICA del fast path VU1 (memo de despacho, 0.6.35).
  #
  # MvuFastPath.h es C++ puro y sin dependencias, igual que MvuSbIndex.h, para poder
  # verificarlo en host. Que hace falta esta medido, no supuesto: search() de la rama
  # rapida recorre quickLookup LINEALMENTE en cada despacho de VU1, y un memo que se
  # equivoca no cuesta velocidad — cuesta saltar a la hostEntry de un bloque que nadie
  # pidio, o sea ejecutar microcodigo que no es el del barrido. Por eso el arnes no mira
  # «funciona rapido» sino EQUIVALENCIA bidireccional contra un modelo lineal fiel del
  # search() real (mismo dedup de add(), misma mascara ~0x3002, mismo orden de insercion):
  # ningun acierto puede ser falso, y con el contexto vivo el memo TIENE que acertar
  # (si no, es solo costo y el verde estaria tapando una optimizacion muerta).
  #
  # Que NO prueba este arnes, y hay que decirlo para que nadie lea de mas en su verde:
  # (a) que el MOTOR llame al memo en el momento correcto — eso vive en
  #     microVU-arm64.cpp, que host NO puede analizar (tira de <arm_neon.h>); lo fija el
  #     texto mutacion_h12.sh fase 2 y lo compila el CI, (b) que el llamador pase la
  #     posicion que el barrido realmente recorrio, (c) cuanto se usa en un juego real:
  #     esa es justo la linea de telemetria que el telefono vuelca al cerrar el VM.
  #
  # El arnes se auto-vale: si la secuencia no paso por los caminos (servir, barrido
  # corto, miss, mascara volteada, micro reescrito, reset, gestor reasignado, ventana
  # movida) el verde se declara invalido y ruida. Y mutacion_h12.sh, aparte, rompe 26
  # cosas del encabezado una por una y exige 26 rojos con nombre: sin eso, «0 fallos»
  # es solo un numero.
  local box=$WORK/h12
  if [ -z "$CXX" ]; then
    echo "h12: SIN COMPILAR - no hay g++ de host (CXX='$CXX'); la logica del memo de despacho NO se verifico en esta corrida"
    RC=1
    return
  fi
  rm -rf "$box"; mkdir -p "$box"
  cp "$HERE/h12-src/fastpath_test.cpp" "$box/" || { echo "h12: no se pudo traer el arnes"; RC=1; return; }
  # Regla anti-copia-vieja: el encabezado se trae DEL REPO en cada corrida.
  if [ ! -f "$CPP/MvuFastPath.h" ]; then echo "h12: FALTA MvuFastPath.h en el repo ($CPP)"; RC=1; return; fi
  cp "$CPP/MvuFastPath.h" "$box/" || { echo "h12: no se pudo copiar MvuFastPath.h"; RC=1; return; }
  # -std=c++17 y -Wall -Wextra: el estandar del arnes es el que el encabezado promete
  # (inline variables, if constexpr en el TU es C++20 pero el header no lo usa), asi que
  # un header que de pronto necesite mas se ve aqui y no en el CI.
  if ! "$CXX" -std=c++17 -Wall -Wextra -I"$box" -o "$box/h12" "$box/fastpath_test.cpp" 2>"$box/compile.err"; then
    echo "h12: FALLO DE COMPILACION"; head -40 "$box/compile.err"; RC=1; return
  fi
  if [ -s "$box/compile.err" ]; then
    echo "h12: avisos del compilador (con -Wall -Wextra no se dejan pasar)"
    head -20 "$box/compile.err"; RC=1
  fi
  OUT=$("$box/h12" 2>/dev/null | grep -E "Comprobaciones|FALLO|testigos")
  echo "$OUT"
  echo "$OUT" | grep -qE ", [1-9][0-9]* Fallos" && RC=1
  echo "$OUT" | grep -q "Comprobaciones" || { echo "h12: el arnes no imprime recuento"; RC=1; }
  # Anti-verde-vacio: los testigos tienen que salir no-cero. Un diferencial que nunca
  # audito un acierto servido no probo la invariante (i), y uno sin rechazos legitimos
  # no probo la invalidacion. El propio arnes lo Comprueba, pero si la linea de testigos
  # desaparece del stdout la comprobacion del arnes no se entera: se exige aqui.
  echo "$OUT" | grep -q "testigos:" || { echo "h12: falta la linea de testigos"; RC=1; }
}

runh13() { # arnes de CUENTA del present sub-nativo (CenitPresentStats.h, 0.6.36).
  #
  # Por que existe: el log real de un telefono (God of War, 0.6.34) prueba que hubo
  # cuadros por debajo de 1x —el regidor sostiene 0.5x y el nucleo avisa «below
  # native»— y NO puede probar si la reconstruccion EASU los cubrio, los dejo
  # estirados o fallo al intentarlos. Nada los contaba. Este arnes no mira la imagen
  # (eso solo lo dice el telefono): mira que la cuenta sea EXHAUSTIVA, que cada
  # cuadro mova UNA sola casilla y que la suma de las cuatro cierre contra los
  # cuadros contados, porque esa linea va a ser la unica evidencia que se lea despues.
  #
  # Incluye dos controles NEGATIVOS escritos a proposito: se rompe la cuenta por
  # fuera del clasificador (un cuadro sin casilla, una casilla inflada) y se exige
  # que Closes() diga NO. Un invariante que solo se comprueba sobre codigo bueno es
  # un adorno, y debilitar la comparacion a <= pasaria verde sin ellos.
  #
  # Lo que NO prueba: que el motor llame a Tally en el momento correcto (eso vive en
  # GSRenderer.cpp, que host no compila: tira de Vulkan/ImGui/GSState) — eso lo fija
  # el TEXTO de mutacion_h13.sh fase 2, once anclas, con su limitacion escrita.
  local box=$WORK/h13
  if [ -z "$CXX" ]; then
    echo "h13: SIN COMPILAR - no hay g++ de host (CXX='$CXX'); la cuenta del present sub-nativo NO se verifico en esta corrida"
    RC=1
    return
  fi
  rm -rf "$box"; mkdir -p "$box"
  cp "$HERE/h13-src/present_test.cpp" "$box/" || { echo "h13: no se pudo traer el arnes"; RC=1; return; }
  # Regla anti-copia-vieja (heredada de h9/h12): el header se trae DEL REPO en cada
  # corrida. Un verde contra una copia editada a mano aprueba la version equivocada.
  if [ ! -f "$GCEN/CenitPresentStats.h" ]; then echo "h13: FALTA CenitPresentStats.h en el repo ($GCEN)"; RC=1; return; fi
  cp "$GCEN/CenitPresentStats.h" "$box/" || { echo "h13: no se pudo copiar CenitPresentStats.h"; RC=1; return; }
  if ! "$CXX" -std=c++17 -Wall -Wextra -I"$box" -o "$box/h13" "$box/present_test.cpp" 2>"$box/compile.err"; then
    echo "h13: FALLO DE COMPILACION"; head -40 "$box/compile.err"; RC=1; return
  fi
  if [ -s "$box/compile.err" ]; then
    echo "h13: avisos del compilador (con -Wall -Wextra no se dejan pasar)"
    head -20 "$box/compile.err"; RC=1
  fi
  OUT=$("$box/h13" 2>/dev/null | grep -E "Comprobaciones|FALLO|testigos")
  echo "$OUT"
  echo "$OUT" | grep -qE ", [1-9][0-9]* Fallos" && RC=1
  echo "$OUT" | grep -q "Comprobaciones" || { echo "h13: el arnes no imprime recuento"; RC=1; }
  echo "$OUT" | grep -q "testigos:" || { echo "h13: falta la linea de testigos"; RC=1; }
}

runh14() { # arnes de LOGICA de la decision de elegibilidad del superbloque (0.6.37).
  #
  # Por que existe, en una linea: el log de God of War de 0.6.36 decia
  # «construidos=0» y cuatro cubos (struct/cold/budget/identity) que cada uno
  # mezclaban de 2 a 4 causas distintas. Ningun lector podia decir POR QUE no se
  # armaba nada — ni siquiera si el motor estaba mirando. Este arnes prueba la ley
  # nueva (MvuSbEligibility.h, C++ puro) y, sobre todo, la PARIDAD con la cadena
  # literal de 0.6.36, casilla por casilla del espacio de entradas: el mismo
  # rechazo tiene que caer en el mismo cubo viejo (si no, una partida de hoy no es
  # comparable con la de anteayer) y donde el viejo NO movia nada el nuevo SI tiene
  # que tener casilla (eso es la ganancia, y se cuenta como testigo).
  #
  # Que NO prueba, dicho para que nadie lea de mas en su verde:
  #   - que el motor llame a SbConsiderArm desde el camino del stub (eso vive en
  #     microVU-arm64.cpp, que host no analiza: <arm_neon.h> + vixl), ni que los
  #     datos que pasa sean los vivos. Son anclas de mutacion_h14.sh fase 2.
  #   - que un telefono construya superbloques. Solo lo dice el log.
  local box=$WORK/h14
  if [ -z "$CXX" ]; then
    echo "h14: SIN COMPILAR - no hay g++ de host (CXX='$CXX'); la decision de elegibilidad del superbloque NO se verifico en esta corrida"
    RC=1
    return
  fi
  rm -rf "$box"; mkdir -p "$box"
  cp "$HERE/h14-src/elig_test.cpp" "$box/" || { echo "h14: no se pudo traer el arnes"; RC=1; return; }
  # Regla anti-copia-vieja (heredada de h9/h12/h13): el header se trae DEL REPO en
  # cada corrida. Un verde contra una copia editada a mano aprueba la version
  # equivocada — y aqui eso es peor, porque el header ES la decision.
  if [ ! -f "$CPP/MvuSbEligibility.h" ]; then echo "h14: FALTA MvuSbEligibility.h en el repo ($CPP)"; RC=1; return; fi
  cp "$CPP/MvuSbEligibility.h" "$box/" || { echo "h14: no se pudo copiar MvuSbEligibility.h"; RC=1; return; }
  if ! "$CXX" -std=c++17 -Wall -Wextra -O1 -I"$box" -o "$box/h14" "$box/elig_test.cpp" 2>"$box/compile.err"; then
    echo "h14: FALLO DE COMPILACION"; head -40 "$box/compile.err"; RC=1; return
  fi
  if [ -s "$box/compile.err" ]; then
    echo "h14: avisos del compilador (con -Wall -Wextra no se dejan pasar)"
    head -20 "$box/compile.err"; RC=1
  fi
  OUT=$("$box/h14" 2>/dev/null | grep -E "Comprobaciones|FALLO|testigos")
  echo "$OUT"
  echo "$OUT" | grep -qE ", [1-9][0-9]* Fallos" && RC=1
  echo "$OUT" | grep -q "Comprobaciones" || { echo "h14: el arnes no imprime recuento"; RC=1; }
  # Los tres testigos tienen que salir: sin «huecos» el diferencial no encontro el
  # defecto que dice cerrar (y eso significa que el modelo viejo esta mal copiado),
  # y sin «el-viejo-llegaba-al-final» el espacio no cubria el caso de exito.
  echo "$OUT" | grep -q "testigos:" || { echo "h14: falta la linea de testigos"; RC=1; }
  echo "$OUT" | grep -qE "hueco\)=[1-9]" || { echo "h14: el diferencial no encontro huecos de 0.6.36 (modelo viejo sospechoso)"; RC=1; }
  echo "$OUT" | grep -qE "el-viejo-llegaba-al-final=[1-9]" || { echo "h14: ningun caso llegaba al final de la cadena vieja"; RC=1; }
}

ONLY=${ARNES:-all}
case "$ONLY" in
  all|h5|h6|h7|h8|h9|h10|h11|h12|h13|h14) ;;
  # Un nombre mal escrito tiene que ruidar: si no, "corrí los arneses" se convierte
  # en "no corrí ninguno y volvi con RC=0", que es exactamente el verde vacio que
  # este script existe para evitar.
  *) echo "baseline.sh: ARNES='$ONLY' no existe (valores: all h5 h6 h7 h8 h9 h10 h11 h12 h13 h14)"; exit 2 ;;
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
# h10 (0.6.32): arnes de SINTAXIS de las cabeceras arm64. No mira logica — mira que
# el area arm64 sea C++ valido por si sola y que las firmas del contrato no se hallan
# movido. Es barato (segundos) y es justo lo que el CI de arm64 tardaba 25 minutos en
# decirnos, asi que va en la linea base y no al final del ritual.
[ "$ONLY" = all ] || [ "$ONLY" = h10 ] && runh10
# h11 (0.6.33): la LOGICA del indice PC->hueco. h9 mira la maquina de estados y h10
# la sintaxis del area; ninguno de los dos mira la RESOLUCION, que es el camino por el
# que pasa todo VU1. El indice es un acelerador, y un acelerador que se equivoca no
# pierde velocidad — sirve codigo que nadie valido. Por eso la equivalencia con el
# barrido se fuerza en host, casilla por casilla, antes de encenderlo en el telefono.
[ "$ONLY" = all ] || [ "$ONLY" = h11 ] && runh11
# h12 (0.6.35): la LOGICA del memo de despacho de VU1. h11 mira el indice del
# superbloque — que no esta activo en esta fase — y h10 la sintaxis del area; ninguno
# mira RESOLUCION DE DESPACHO, que es el camino por el que pasa cada VU1 de cada frame.
# El memo es un acelerador, y un acelerador que se equivoca no pierde velocidad: ejecuta
# un bloque que nadie pidio. Por eso la equivalencia con el barrido se fuerza en host,
# con las dos direcciones (nada falso servido, nada vivo negado), antes de cerrar la
# version.
[ "$ONLY" = all ] || [ "$ONLY" = h12 ] && runh12
# h13 (0.6.36): la cuenta del present sub-nativo. Entra a la base por el mismo
# argumento con el que entro h12: el numero que el telefono va a imprimir es la unica
# evidencia que se lee despues, y un contador sin arnes es una opinion con formato de
# dato.
[ "$ONLY" = all ] || [ "$ONLY" = h13 ] && runh13
# h14 (0.6.37): la LEY de elegibilidad del superbloque y su PARIDAD con la cadena
# de 0.6.36. Entra por el mismo argumento que h12/h13 y por uno propio: la
# telemetria que este cambio añade es la unica forma que va a existir de explicar
# un «construidos=0» futuro, y un contador por causas sin arnes es justo lo que
# tuvimos — cuatro contadores sin arnes.
[ "$ONLY" = all ] || [ "$ONLY" = h14 ] && runh14

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
