#!/usr/bin/env python3
"""
Verificador de diccionarios que cruzan lenguajes (Cenit).

El pattern que este script cierra: un numero decide una accion y lo traduce una tabla
en OTRO archivo y en OTRO Lenguaje. CI compila los dos por separado y NINGUNO de los
dos se entera si la tabla del otro se desalinо o si el TEXTO de una casilla cambia de
nombre. Con los estados de carga ya paso (son contrato escrito en
docs/CONTRATO_CENIT_LOAD_SAMPLE.md); con los del sintonizador de 0.6.31 se colо un
desajuste real: el HUD decia "AUTO_noRS" y el log y Ajustes decian "AUTO_SAFE_sinRS"
para el mismo modo 4. Un usuario que cruce HUD contra log obtendria dos modos que no
existen, y eso es exactamente presentar como evidencia algo que no lo es.

Comprueba, sin compilar nada:
  1. CpuVUTuner.STATE_NAMES[]  ==  ImGuiOverlays.cpp kTunerStates[]  (texto y orden)
  2. CpuVUTuner.modeName() por ordinal  ==  kTunerModes[]  (texto y orden)
  3. arrays.xml tuner_mode_entries: una entrada por MODE_* y en el ORDEN del ordinal
     (aqui la posicion ES el valor: el spinner escribe su indice).
  4. GameLoadClassifier.ordinalForHud()/codeForHud() vs los contratos escritos en
     CenitTelemetry.java y en ImGuiOverlays.cpp (los 7 estados y las 4 acciones).
  5. Los guardas de rango del C++ coinciden con el tamano real de sus tablas.

Sale 1 con la lista de desajustes. Es el arnes que no necesita telefono.
"""
import re, sys, pathlib

R = pathlib.Path("/app/cenit/app/src/main")
J = R / "java/com/izzy2lost/psx2"
OV = R / "cpp/pcsx2/ImGui/ImGuiOverlays.cpp"

def read(p): return p.read_text(encoding="utf-8", errors="replace")

def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)

def arr(body):
    return [x.strip().strip('"') for x in body.split(",") if x.strip()]

problems = []
def bad(msg):
    problems.append(msg)

tuner = read(J / "CpuVUTuner.java")
cls = read(J / "GameLoadClassifier.java")
tele = read(J / "CenitTelemetry.java")
ov = strip_comments(read(OV))

# --- 1. estados del sintonizador ---
jstates = arr(re.search(r"STATE_NAMES\s*=\s*\{(.*?)\}", tuner, flags=re.S).group(1))
cstates = arr(re.search(r"kTunerStates\[\]\s*=\s*\{(.*?)\}", ov, flags=re.S).group(1))
if jstates != cstates:
    bad("estados del tuner: Java=%s C++=%s" % (jstates, cstates))
jcount = int(re.search(r"STATE_COUNT = (\d+);", tuner).group(1))
if jcount != len(jstates):
    bad("STATE_COUNT=%d pero STATE_NAMES tiene %d" % (jcount, len(jstates)))
# el guarda del C++ tiene que ser el tamano real de su tabla
guard = re.search(r"state < (\d+)\) \? kTunerStates", ov)
if not guard:
    bad("no se encontro el guarda de kTunerStates en el HUD")
elif int(guard.group(1)) != len(cstates):
    bad("guarda kTunerStates: compara con %s pero la tabla tiene %d"
        % (guard.group(1), len(cstates)))

# --- 2. modos del sintonizador (texto POR ORDINAL, no por orden de aparicion) ---
consts = {n: int(v) for n, v in re.findall(r"static final int (MODE_\w+) = (\d+);", tuner)}
mode_count = consts.get("MODE_COUNT")
body = re.search(r"static String modeName\(int m\) \{(.*?)\n    \}", tuner, flags=re.S).group(1)
byidx = {}
for case, ret in re.findall(r'case MODE_(\w+):\s*return "([^"]+)";', body):
    byidx[consts["MODE_" + case]] = ret
default = re.search(r'default:\s*return "([^"]+)";', body).group(1)
jmodes = [byidx.get(i, default) for i in range(mode_count)]
cmodes = arr(re.search(r"kTunerModes\[\]\s*=\s*\{(.*?)\}", ov, flags=re.S).group(1))
if jmodes != cmodes:
    bad("modos del tuner: Java=%s C++=%s" % (jmodes, cmodes))
mguard = re.search(r"mode < (\d+)\) \? kTunerModes", ov)
if not mguard:
    bad("no se encontro el guarda de kTunerModes en el HUD")
elif int(mguard.group(1)) != len(cmodes):
    bad("guarda kTunerModes: compara con %s pero la tabla tiene %d"
        % (mguard.group(1), len(cmodes)))

# --- 3. arrays.xml: una entrada por modo, en el orden del ordinal ---
arrays = read(R / "res/values/arrays.xml")
blk = re.search(r'name="tuner_mode_entries">(.*?)</string-array>', arrays, flags=re.S)
if not blk:
    bad("arrays.xml no tiene tuner_mode_entries")
else:
    items = [re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", i).replace("&quot;", '"')).strip()
             for i in re.findall(r"<item>(.*?)</item>", blk.group(1), flags=re.S)]
    if len(items) != mode_count:
        bad("tuner_mode_entries tiene %d entradas y hay %d modos" % (len(items), mode_count))
    # el spinner escribe SU INDICE: el ordinal de cada etiqueta tiene que ser el indice
    order = {v: n for n, v in consts.items() if n != "MODE_COUNT"}
    names_in_order = [order[i] for i in range(mode_count)]
    expect = {0: "Desactivado", 1: "Manual", 2: "Autom"}
    for i, pref in expect.items():
        if i < len(items) and not items[i].startswith(pref):
            bad("tuner_mode_entries[%d] empieza con %r y deberia con %r (la posicion ES "
                "el valor del modo)" % (i, items[i][:24], pref))
    if names_in_order != ["MODE_OFF", "MODE_MANUAL", "MODE_AUTO_SAFE",
                          "MODE_AUTO_MTVU_ONLY", "MODE_AUTO_NO_RATE_SKIP"]:
        bad("ordinales MODE_* reordenados: %s" % names_in_order)

# --- 4. estados/acciones de CARGA (contrato de 0.6.30) ---
hud_states = re.findall(r"case (\w+):\s*return\s*(\d+);",
                        re.search(r"ordinalForHud\(\) \{(.*?)\}", cls, flags=re.S).group(1))
want_states = {"UNKNOWN_LOAD": 0, "NO_BOTTLENECK": 1, "GPU_BOUND": 2, "CPU_BOUND": 3,
               "VU_BOUND": 4, "THERMAL_BOUND": 5, "INVALID_SAMPLE": 6}
got_states = {k: int(v) for k, v in hud_states}
if got_states != want_states:
    bad("ordinalForHud cambio: %s (el contrato dice %s)" % (got_states, want_states))
hud_acts = {k: int(v) for k, v in re.findall(r"case (\w+):\s*return\s*(\d+);",
              re.search(r"codeForHud\(\) \{(.*?)\}", cls, flags=re.S).group(1))}
want_acts = {"HOLD": 0, "ALLOW_RESOLUTION_DROP": 1, "BLOCK_RESOLUTION_DROP": 2,
             "WAIT_FOR_EVIDENCE": 3}
if hud_acts != want_acts:
    bad("codeForHud cambio: %s" % hud_acts)
# CenitTelemetry.STATE_* tiene que decir lo mismo que el HUD
tl = {n: int(v) for n, v in re.findall(r"STATE_(\w+) = (\d+);", tele)}
if {k: v for k, v in tl.items() if k != "COUNT"} != want_states:
    bad("CenitTelemetry.STATE_* ya no coincide con el contrato: %s" % tl)

print("estados del tuner : %d casillas Java / %d C++" % (len(jstates), len(cstates)))
print("modos del tuner   : %s" % jmodes)
print("modo por ordinal  : %s" % names_in_order)
print("estados de carga  : %s" % want_states)
print("acciones de carga : %s" % want_acts)
for p in problems:
    print("DESAJUSTE: " + p)
print("RESULTADO: %s" % ("OK: los diccionarios que cruzan lenguajes dicen lo mismo"
                         if not problems else "HAY DESAJUSTES (ver arriba)"))
sys.exit(1 if problems else 0)
