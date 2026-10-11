#!/usr/bin/env bash
# tools/medicion/mutacion_overlay.sh — prueba que render_overlay.py PUEDE fallar.
#
# Un verificador que nunca se vio fallar por una causa concreta no es un
# verificador: es un informe. Este script sabotea, de a uno y sobre una copia del
# árbol de recursos, las cosas que las ocho comprobaciones juran mirar, y exige
# que CADA saboteo salga rojo nombrando al culpable.
#
# Nació de un hecho registrado en 0.6.34: la primera versión de la comprobación de
# glifos tenía la banda escrita en [30%,75%], más laxa que la ley que decía
# verificar (45-55%), así que volver a subir un glifo al 71% salía VERDE. Ese
# defecto era del verificador, no del diseño, y solo se ve corriéndolo contra
# código saboteado a propósito.
#
# Copia SOLO app/src/main/res y la herramienta (el repo pesa 452 MB; copiar todo
# diez veces sería esperar una hora por un verde que dura segundos).
#
# RC=0 solo si caen todos y ninguna ancla queda sin encontrar.
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# --------------------------------------------------------------------- los saboteos
# nombre :: archivo :: ancla :: reemplazo   (el reemplazo DEBE violar una ley)
MUTS=(
  # ley 1: el relleno vale 0 % de alfa
  "relleno-tapa::app/src/main/res/values/cenit_overlay.xml::<color name=\"overlay_fill\">#00000000</color>::<color name=\"overlay_fill\">#4D243458</color>"
  # ley 2: banda de reposo 40-60 %
  "anillo-tapa::app/src/main/res/values/cenit_overlay.xml::<color name=\"overlay_ring_cyan\">#8C35E7FF</color>::<color name=\"overlay_ring_cyan\">#CC35E7FF</color>"
  # ley 2 (texto): la misma banda, en el archivo que NINGUN estilo referencia
  "texto-tapa::app/src/main/res/values/cenit_overlay.xml::<color name=\"overlay_text\">#8CC2CBD8</color>::<color name=\"overlay_text\">#B3C2CBD8</color>"
  # ley 2 (glifos): res/color/overlay_face_*.xml, el hueco que tapó el defecto real
  "glifo-tapa::app/src/main/res/color/overlay_face_circle.xml::#8CFF5A6E::#B4FF5A6E"
  # ley 2 (presionado): delta minimo 25 % para que el toque se sienta
  "presionado-identico::app/src/main/res/values/cenit_overlay.xml::<color name=\"overlay_ring_magenta_pressed\">#F0DA8CFF</color>::<color name=\"overlay_ring_magenta_pressed\">#90DA8CFF</color>"
  # ley 3: la base del stick no puede ser un parche opaco
  "base-stick-tapa::app/src/main/res/values/cenit_overlay.xml::<color name=\"overlay_stick_base\">#4D0B1018</color>::<color name=\"overlay_stick_base\">#B30B1018</color>"
  # geometria: el petalo de la cruceta es achaflanado, no circular
  "petalo-redondeado::app/src/main/res/values/styles_buttons.xml::<item name=\"cornerFamily\">cut</item>::<item name=\"cornerFamily\">rounded</item>"
  # geometria: las alas son espejo exacto
  "ala-no-espejo::app/src/main/res/values/styles_buttons.xml::<item name=\"cornerFamilyTopRight\">cut</item>::<item name=\"cornerFamilyTopRight\">rounded</item>"
  # el orden de Material: cornerRadius PISA el corte, y eso es asercion
  "radio-pisa-corte::app/src/main/res/values/styles_buttons.xml::<item name=\"shapeAppearanceOverlay\">@style/PSX2.Shape.Petal</item>::<item name=\"shapeAppearanceOverlay\">@style/PSX2.Shape.Petal</item>@@<item name=\"cornerRadius\">8dp</item>"
  # recursos: todo estilo que usa un layout tiene que existir
  "estilo-fantasma::app/src/main/res/layout-land/activity_main.xml::style=\"@style/PSX2.OverlayCircle.Petal\"::style=\"@style/PSX2.OverlayCircle.SinExistir\""
  # recursos: todo @color de iconTint tiene que existir. Sin tilde: la comprobación
  # busca [a-z_0-9]+, y un nombre con "á" ni siquiera entraría a la lista — sería un
  # mutante que no llega a la prueba, no un fallo detectado.
  "color-fantasma::app/src/main/res/layout-port/activity_main.xml::app:iconTint=\"@color/overlay_face_triangle\"::app:iconTint=\"@color/overlay_face_triangulo\""
)

fails=0
total=0
for m in "${MUTS[@]}"; do
  total=$((total+1))
  tree="$WORK/tree"
  rm -rf "$tree"
  mkdir -p "$tree/app/src/main" "$tree/tools/medicion"
  cp -a "$ROOT/app/src/main/res" "$tree/app/src/main/res"
  cp "$ROOT/tools/medicion/render_overlay.py" "$tree/tools/medicion/"

  # El corte por '::' se hace acá adentro y no en bash: IFS solo separa por
  # CARACTERES, así que "IFS='::'" partiría también en los ':' sueltos y el
  # resultado sería una ancla truncada que se reportaría como "no encontrada"
  # — un rojo del ejecutor, no del verificador.
  if ! MUT="$m" TREE="$tree" python3 - <<'PY'
import os, sys
name, rel, anchor, repl = os.environ["MUT"].split("::", 3)
tgt = os.path.join(os.environ["TREE"], rel)
s = open(tgt, encoding="utf-8").read()
if anchor not in s:
    sys.stderr.write(f"ancla no encontrada: {name}\n"); sys.exit(2)
open(tgt, "w", encoding="utf-8").write(s.replace(anchor, repl.replace("@@", "\n        "), 1))
PY
  then
    echo "ANCLA NO ENCONTRADA: $m"
    fails=$((fails+1)); continue
  fi

  name=${m%%::*}
  out=$(cd "$tree" && python3 tools/medicion/render_overlay.py /tmp/mut.png 2>&1); rc=$?
  if [ $rc -eq 0 ]; then
    echo "ESCAPÓ (verde con el código saboteado): $name"
    fails=$((fails+1))
  else
    echo "detectado: $name -> $(grep -m1 '^FALLO:' <<<"$out" | cut -c1-100)"
  fi
done

echo
if [ $fails -ne 0 ]; then
  echo "mutacion_overlay: $fails de $total saboteos NO se detectaron"
  exit 1
fi
echo "mutacion_overlay: OK — los $total saboteos caen, cada uno nombrando al culpable"
