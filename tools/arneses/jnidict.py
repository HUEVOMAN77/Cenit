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
  6-9. Cenit 0.6.32 (Fase 2) — la MAQUINA DE ESTADOS VU-SB y sus cuatro cruces
     de archivo (diccionario de nombres, honestidad del HUD, mascara del
     digest de salida campo por campo, y la linea del HUD). Ver bloque abajo.

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

# ======================================================================
# --- 6..9. Maquina de estados VU-SB (Cenit 0.6.32, Fase 2) ---
# El ciclo de vida lo ejecuta Harness9 sobre MvuSbLife.h. Lo que Harness9 NO
# puede ver son los cuatro cruces de archivo: que el diccionario de nombres
# cubra la enum sin huecos; que el HUD no pueda prometer velocidad en
# VALIDATE/SHADOW (prohibicion literal del prompt); que la MASCARA que el
# digest de salida DECLARA en microVU-arm64.cpp sea exactamente la que la
# maquina EXIGE y que cada region declarada este realmente hashkeada campo por
# campo (si alguien quita un campo sin bajar el bit, la evidencia se formaria
# sobre un estado a medias); y que la linea del HUD tenga tantos
# especificadores como argumentos.
LIFE = R / "cpp/pcsx2/arm64/MvuSbLife.h"
SBCPP = R / "cpp/pcsx2/arm64/MvuSuperblock-arm64.cpp"
MVU = R / "cpp/pcsx2/arm64/microVU-arm64.cpp"
life = strip_comments(read(LIFE))
sbcpp = strip_comments(read(SBCPP))
mvu = strip_comments(read(MVU))

def blk(src, pattern, what, fname):
    m = re.search(pattern, src, flags=re.S)
    if not m:
        bad("no se encontro %s en %s" % (what, fname))
        return None
    return m.group(1)

# --- 6. diccionario de estados: la enum y su nombre, sin huecos ---
names = {}
enum = blk(life, r"enum class State : uint32_t\s*\{(.*?)\n\t\};",
           "la enum State", "MvuSbLife.h")
if enum:
    jstate = {n: int(v) for n, v in re.findall(r"(\w+)\s*=\s*(\d+)", enum)}
    names = dict(re.findall(r'case State::(\w+): return "([A-Z_?]+)";', life))
    if len(names) != len(jstate):
        bad("StateName cubre %d estados y la enum State tiene %d"
            % (len(names), len(jstate)))
    for n in sorted(jstate, key=lambda k: jstate[k]):
        if n not in names:
            bad("estado %s (ordinal %d) sin nombre en StateName" % (n, jstate[n]))
        elif names[n] == "STATE_??":
            bad("estado %s tiene el nombre de relleno STATE_??" % n)
    kcnt = re.search(r"static constexpr uint32_t kStateCount = (\d+);", life)
    if not kcnt:
        bad("falta kStateCount en MvuSbLife.h")
    elif int(kcnt.group(1)) != len(jstate):
        bad("kStateCount=%s pero la enum State tiene %d casillas"
            % (kcnt.group(1), len(jstate)))
    # el ORDEN es contrato: estos numeros viajan al HUD y a logs de telefonos
    want_order = {0: "Unseen", 1: "Building", 2: "Validating", 3: "Shadowing",
                  4: "Validated", 5: "Trusted", 6: "Dead", 7: "Invalidated"}
    if jstate != {v: k for k, v in want_order.items()}:
        bad("ordinales de la enum State reordenados: %s (el contrato dice %s)"
            % (jstate, want_order))

# --- 7. honestidad del HUD ---
labels = []
hud = blk(life, r"const char\* HudLabel\(\) const\s*\{(.*?)\n\t\t\}",
          "HudLabel()", "MvuSbLife.h")
if hud:
    labels = re.findall(r'return "(VU-SB: [A-Z/]+)";', hud)
    if not labels:
        bad("HudLabel no devuelve ninguna etiqueta VU-SB")
    for lab in labels:
        # el prompt lo prohibe en VALIDATE_ONLY y en SHADOW: aqui esta codificado
        for ban in ("ACELERADO", "ACELERADA", "TURBO", "ACELERA"):
            if ban in lab:
                bad("la etiqueta %r promete velocidad" % lab)
        if ("VALIDATE" in lab or "SHADOW" in lab) and "TRUSTED" in lab and \
                "SPOT" not in lab:
            bad("la etiqueta %r mezcla un estado sin medir con Trusted" % lab)
    need = {"VU-SB: VALIDATE", "VU-SB: SHADOW", "VU-SB: VALIDATED",
            "VU-SB: TRUSTED", "VU-SB: DEAD", "VU-SB: INVALIDATED"}
    missing = need - set(labels)
    if missing:
        bad("faltan etiquetas de estado en HudLabel: %s" % sorted(missing))
    # la vigilancia se dice JUNTO al estado, no en lugar de el
    if "VU-SB: TRUSTED/SPOT" not in labels:
        bad("falta la etiqueta compuesta VU-SB: TRUSTED/SPOT: un Trusted "
            "vigilado no puede pintarse como VALIDATE virgen")
    # Sin candidatos la linea NO puede decir OFF (el motor esta encendido; OFF se
    # leeria "no lo active"). Y si el breaker lo paro, eso tambien se dice.
    if "VU-SB: ON/SIN CANDIDATO" not in sbcpp:
        bad("HudSummary no distingue 'motor encendido sin candidatos' de 'OFF'")
    if "VU-SB: PARADO" not in sbcpp:
        bad("HudSummary no dice cuando el breaker del proceso pare el motor")
    ovsrc = read(OV)
    if "mVUSuperblock::HudSummary" not in ovsrc:
        bad("ImGuiOverlays no pinta la linea del motor con HudSummary (si pinta "
            "recuentos del proceso, un match global puede parecer evidencia)")
    if "IsEnabled() || mVUSuperblock::WasKilled()" not in ovsrc:
        bad("la linea VU-SB del HUD no se pinta tambien con el motor auto-parado: "
            "effective=false la ocultaria y el usuario no veria POR QUE se fue solo")

# --- 8. mascara declarada == mascara exigida == campos hashkeados ---
want = set()
regs = blk(life, r"namespace Regions\s*\{(.*?)\n\t\};",
           "namespace Regions", "MvuSbLife.h")
if regs:
    bits = {n: int(sh) for n, sh in re.findall(r"(\w+)\s*=\s*1u\s*<<\s*(\d+)", regs)}
    req = blk(life, r"static constexpr uint32_t kRequiredRegions =\s*(.*?);",
              "kRequiredRegions", "MvuSbLife.h")
    declared = blk(mvu, r"g_exitMask\.store\((.*?)\)", "g_exitMask.store",
                   "microVU-arm64.cpp")
    if req is not None and declared is not None:
        want = set(re.findall(r"Regions::(\w+)", req))
        got = set(re.findall(r"Regions::(\w+)", declared))
        if want != set(bits):
            bad("kRequiredRegions no cubre todas las regiones declaradas: faltan %s"
                % sorted(set(bits) - want))
        if got != want:
            bad("microVU-arm64.cpp DECLARA %s y la maquina EXIGE %s: con esa "
                "diferencia Machine::PairMatched rechaza cada par y no hay "
                "evidencia posible" % (sorted(got), sorted(want)))
        body = blk(mvu, r"static u64 mVU1ExitDigest\(u32 consumedCycles\)\s*\{(.*?)\n\}",
                   "mVU1ExitDigest", "microVU-arm64.cpp")
        must = {
            "Vf": ["r.VF["], "Vi": ["r.VI["], "Acc": ["r.ACC"],
            "Qp": ["r.q", "r.p"], "Cycle": ["r.cycle"],
            "Flags": ["r.flags", "r.ebit"],
            "Pendings": ["r.pending_q", "r.pending_p"],
            "MacroClipStatus": ["r.micro_macflags", "r.micro_clipflags",
                               "r.micro_statusflags", "r.macflag",
                               "r.statusflag", "r.clipflag"],
            "NextBlockCycles": ["r.nextBlockCycles"],
            "Xgkick": ["r.xgkickaddr", "r.xgkickendpacket"],
            "ViBackup": ["r.VIBackupCycles", "r.VIOldValue", "r.VIRegNumber"],
            "LatencyRings": ["r.fmac", "r.fdiv", "r.efu", "r.ialu",
                             "r.fmacwritepos", "r.ialureadpos"],
            "DataMem": ["VU1.Mem"],
            "EpisodeCycles": ["consumedCycles"],
        }
        if body is None:
            bad("no se pudo leer el cuerpo de mVU1ExitDigest")
        else:
            for region in sorted(want):
                for tok in must.get(region, []):
                    if tok not in body:
                        bad("region %s declarada en g_exitMask pero %r ya no esta "
                            "hashkeada en mVU1ExitDigest" % (region, tok))
            extra = set(must) - want
            if extra:
                bad("el contrato de campos menciona regiones que la maquina no "
                    "exige: %s" % sorted(extra))

# --- 9. la linea del HUD: especificadores == argumentos ---
fmt = re.search(r'std::snprintf\(out, outSize,\s*"(.*?)"\s*,\s*(.*?)\);',
                sbcpp, flags=re.S)
if not fmt:
    bad("no se encontro el snprintf de HudSummary en MvuSuperblock-arm64.cpp")
else:
    spec = re.findall(r"%[-#0-9.]*[suXda]", fmt.group(1))
    argtext = fmt.group(2)
    args, depth, cur = [], 0, ""
    for ch in argtext:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            args.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        args.append(cur)
    args = [a for a in args if a.strip()]
    if len(spec) != len(args):
        bad("HudSummary: %d especificadores para %d argumentos (%s)"
            % (len(spec), len(args), fmt.group(1)))
    guard = re.search(r"if \(!out \|\| outSize < (\d+)\)", sbcpp)
    if not guard:
        bad("HudSummary sin guarda de tamano de buffer")
    elif len(spec) and int(guard.group(1)) < len(spec) * 8 + 32:
        bad("HudSummary: la guarda outSize<%s no deja espacio para %d campos"
            % (guard.group(1), len(spec)))

print("estados del tuner : %d casillas Java / %d C++" % (len(jstates), len(cstates)))
print("modos del tuner   : %s" % jmodes)
print("modo por ordinal  : %s" % names_in_order)
print("estados de carga  : %s" % want_states)
print("acciones de carga : %s" % want_acts)
print("VU-SB: %-11s : %s" % ("estados", names if names else "-"))
print("VU-SB: %-11s : %s" % ("etiquetas", sorted(labels) if labels else "-"))
print("VU-SB: %-11s : %d regiones (declaradas = exigidas = hashkeadas)"
      % ("mascara", len(want)))
for p in problems:
    print("DESAJUSTE: " + p)
print("RESULTADO: %s" % ("OK: los diccionarios que cruzan lenguajes dicen lo mismo"
                         if not problems else "HAY DESAJUSTES (ver arriba)"))
sys.exit(1 if problems else 0)
