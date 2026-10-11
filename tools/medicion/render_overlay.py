#!/usr/bin/env python3
"""
tools/medicion/render_overlay.py — verificador visual de los mandos en pantalla.

Existe porque un estilo XML de esquinas no se puede "leer y ya": cornerFamily y
cornerSize los interpreta MaterialShapeDrawable en el teléfono, y hasta 0.6.34
nadie había dibujado nunca cómo quedan. Este script reproduce la MISMA regla
geométrica que aplica Android (esquina cortada = chaflán; esquina redonda = arco
de radio cornerSize) leyendo los valores DIRECTAMENTE DEL XML del repo. Si alguien
cambia un cornerSize en styles_buttons.xml, este render cambia con él: no es una
maqueta pintada a mano, es una proyección del recurso real.

Qué PRUEBA:
  - la geometría: dónde cae cada chaflán, que las alas L/R sean espejo, que el
    pétalo de la cruceta sea achaflanado y no un círculo;
  - la transparencia de verdad, midiendo píxeles: el interior de un botón con
    relleno alfa-0 tiene que dejar pasar el fondo, y si no lo deja, eso es un
    FALLO (no un comentario);
  - los alfas declarados, contra las bandas de la paleta: relleno nulo, contorno
    y glifos en la misma banda de reposo, presionado claramente más fuerte;
  - que todo @color que referencian los tres layouts existe en el árbol de recursos.

Qué NO prueba:
  el ripple, la elevación, el pressed real de Material, la fuente del sistema ni
  el pipeline de composición de Android. Eso sólo se ve en el teléfono, y decir
  lo contrario sería vender una maqueta como validación.

Uso:  python3 tools/medicion/render_overlay.py [salida.png]   (RC=1 si algo falla)
"""
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RES = os.path.join(ROOT, "app", "src", "main", "res")
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/overlay_preview.png"
PX = 2.75  # densidad del teléfono de referencia (440 dpi)


# ---------------------------------------------------------------- recursos reales
def load_colors():
    out = {}
    for f in ("cenit_overlay.xml",):
        for c in ET.parse(os.path.join(RES, "values", f)).getroot().findall("color"):
            h = c.text.strip().lstrip("#")
            if len(h) == 6:
                h = "FF" + h
            v = int(h, 16)
            out[c.get("name")] = ((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255)
    return out


def load_styles():
    items, parents = {}, {}
    vdir = os.path.join(RES, "values")
    for f in sorted(os.listdir(vdir)):
        if not f.endswith(".xml"):
            continue
        for s in ET.parse(os.path.join(vdir, f)).getroot().findall("style"):
            n = s.get("name")
            items[n] = {i.get("name"): (i.text or "").strip() for i in s.findall("item")}
            parents[n] = s.get("parent")

    def resolved(name, seen=()):
        if name in seen or name not in items:
            return {}
        d = {}
        if parents.get(name):
            d.update(resolved(parents[name], seen + (name,)))
        d.update(items[name])
        return d

    return {n: resolved(n) for n in items}


def rest_color(C, S, style, key="strokeColor"):
    """Color de REPOSO de un atributo que apunta a un selector @color/... ."""
    ref = S[style].get(key)
    if not ref:
        raise SystemExit(f"{style} no declara {key}")
    name = ref.replace("@color/", "")
    sel = os.path.join(RES, "color", name + ".xml")
    if os.path.isfile(sel):
        # el item sin estado, que es el ultimo del selector, es el reposo
        last = re.findall(r'android:color="@color/([a-z_0-9]+)"', open(sel).read())[-1]
        return last, C[last]
    return name, C[name]


def dp(v):
    m = re.match(r"([0-9.]+)(dp|sp)", str(v).strip())
    return float(m.group(1)) * PX if m else 0.0


# ------------------------------------------------------------------ geometría M3
def corners_of(S, style):
    """(family, size_px) x4 en orden TL,TR,BR,BL, con la regla de Material:
    shapeAppearanceOverlay manda; si no está, sale el cornerRadius parejo.

    Leer las esquinas del propio estilo sin resolver el overlay — que es lo que
    hacía la primera versión de este script — daba `rounded 0` en los cuatro casos
    y dibujaba rectángulos: un verde que no mira, de lo que este archivo existe
    para huir."""
    st = S[style]
    shape = st.get("shapeAppearanceOverlay")
    if shape:
        name = shape.replace("@style/", "")
        if name not in S:
            raise SystemExit(f"{style}: shapeAppearanceOverlay apunta a {name}, inexistente")
        sh = S[name]

        def one(c):
            fam = sh.get("cornerFamily" + c) or sh.get("cornerFamily") or "rounded"
            size = sh.get("cornerSize" + c) or sh.get("cornerSize") or "0dp"
            return fam.strip(), dp(size)

        return [one(c) for c in ("TopLeft", "TopRight", "BottomRight", "BottomLeft")]
    r = dp(st.get("cornerRadius", "0dp"))
    return [("rounded", r)] * 4


def outline(x0, y0, x1, y1, cs, samples=10):
    """Contorno en el orden que lo recorre MaterialShapeDrawable: TL -> TR -> BR ->
    BL, entrando por un borde y saliendo por el siguiente. Un chaflán mal ordenado
    (punto de salida antes que el de llegada) dibuja un pico cruzando el botón —
    exactamente el defecto que tuvo la primera versión de este archivo."""
    tl, tr, br, bl = [max(0.0, s) for _, s in cs]
    fam = [f for f, _ in cs]
    pts = []

    # Recorte Material: ShapeAppearanceModel.getCornerSizesForDimensions escala las
    # CUATRO esquinas por un factor único cuando dos esquinas de un mismo borde suman
    # más que el borde. Sin esto, PSX2.OverlayCircle (cornerRadius 50dp = 137px) sobre
    # una caja de 111px dibujaba arcos de radio 137 centrados FUERA de la caja: el
    # verificador mostraba estrellas enredadas en lugar de un círculo, y eso se leía
    # como un defecto del diseño cuando era un defecto de esta réplica.
    w, h = x1 - x0, y1 - y0
    scale = 1.0
    for a, b, edge in ((tl, tr, w), (bl, br, w), (tl, bl, h), (tr, br, h)):
        if a + b > edge and a + b > 0:
            scale = min(scale, edge / (a + b))
    tl, tr, br, bl = [c * scale for c in (tl, tr, br, bl)]

    def arc(cx, cy, r, a0, a1):
        for i in range(samples + 1):
            a = a0 + (a1 - a0) * i / samples
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))

    # TL: llega por el borde izquierdo, sale por el superior
    if fam[0] == "cut":
        pts += [(x0, y0 + tl), (x0 + tl, y0)]
    elif tl > 0:
        arc(x0 + tl, y0 + tl, tl, math.pi, 1.5 * math.pi)
    else:
        pts.append((x0, y0))
    # TR: llega por el superior, sale por el derecho
    if fam[1] == "cut":
        pts += [(x1 - tr, y0), (x1, y0 + tr)]
    elif tr > 0:
        arc(x1 - tr, y0 + tr, tr, 1.5 * math.pi, 2 * math.pi)
    else:
        pts.append((x1, y0))
    # BR: llega por el derecho, sale por el inferior
    if fam[2] == "cut":
        pts += [(x1, y1 - br), (x1 - br, y1)]
    elif br > 0:
        arc(x1 - br, y1 - br, br, 0, 0.5 * math.pi)
    else:
        pts.append((x1, y1))
    # BL: llega por el inferior, sale por el izquierdo
    if fam[3] == "cut":
        pts += [(x0 + bl, y1), (x0, y1 - bl)]
    elif bl > 0:
        arc(x0 + bl, y1 - bl, bl, 0.5 * math.pi, math.pi)
    else:
        pts.append((x0, y1))
    return pts


def draw_button(layer, S, C, style, box, label=None, label_color=None, only=None):
    """Dibuja relleno y contorno en una capa RGBA aparte, para que el alfa se
    componga de verdad. Dibujar el polígono alfa-0 directo sobre el fondo ya
    pintado no deja pasar el fondo: en la primera versión de este script el
    «relleno invisible» salió negro sólido, y eso era un bug del verificador
    haciéndose pasar por el diseño.

    `only="fill"` dibuja SOLO el relleno: es lo que usa la prueba de transparencia,
    porque en el render completo el centro del botón tiene el texto (SELECT/START)
    y medir ahí compara tipografía contra fondo, no relleno contra fondo. Ese confound
    existió: la primera versión de la prueba falló por la palabra «SELECT»."""
    x0, y0, x1, y1 = box
    st = S[style]
    pts = outline(x0, y0, x1, y1, corners_of(S, style))
    fill = C["overlay_fill"]
    d = ImageDraw.Draw(layer, "RGBA")
    d.polygon(pts, fill=fill)
    if only == "fill":
        return pts
    _, ring = rest_color(C, S, style)
    w = max(1, int(round(dp(st.get("strokeWidth", "1.5dp")))))
    closed = pts + pts[:1]
    for i in range(len(closed) - 1):
        d.line([closed[i], closed[i + 1]], fill=ring, width=w)
    if label:
        bb = d.textbbox((0, 0), label)
        tw, th = bb[2] - bb[0], bb[3] - bb[1]
        d.text(((x0 + x1) / 2 - tw / 2, (y0 + y1) / 2 - th / 2 - 1), label,
               fill=label_color or C["overlay_text"])
    return pts


# --------------------------------------------------------------------- composición
# Cada botón con el estilo y el id que realmente tiene el layout-land, y con su
# tamaño en dp traducido a px. Las posiciones son de la maqueta (no del Constraint
# real): lo que se verifica es la FORMA y el ALFA de cada estilo, no el layout.
BUTTONS = [
    # (estilo, caja px, etiqueta)
    ("PSX2.OverlayPill.WingCyanLeft",    (14, 14, 190, 107), "L2"),
    ("PSX2.OverlayPill.WingMagentaRight", (212, 14, 388, 107), "L1"),
    ("PSX2.OverlayPill.WingMagentaLeft", (692, 14, 868, 107), "R1"),
    ("PSX2.OverlayPill.WingCyanRight",   (890, 14, 1066, 107), "R2"),
    ("PSX2.OverlayCircle.Petal",         (24, 190, 134, 300), None),
    ("PSX2.OverlayCircle.Petal",         (24, 312, 134, 422), None),
    ("PSX2.OverlayCircle.Petal",         (136, 190, 246, 300), None),
    ("PSX2.OverlayCircle.Petal",         (136, 312, 246, 422), None),
    ("PSX2.OverlayPill.Cyan",            (416, 520, 542, 576), "SELECT"),
    ("PSX2.OverlayPill.Magenta",         (562, 520, 688, 576), "START"),
    ("PSX2.OverlayCircle.FaceCyan",      (884, 300, 995, 411), None),
    ("PSX2.OverlayCircle.FaceMagenta",   (767, 417, 878, 528), None),
    ("PSX2.OverlayCircle.FaceMagenta",   (1001, 417, 1112, 528), None),
    ("PSX2.OverlayCircle.FaceCyan",      (884, 534, 995, 645), None),
]


def menu_background(w, h):
    """Fondo tipo menú brillante: si el mando no es transparente, acá se nota.

    Devuelve una imagen TOTALMENTE OPACA, y eso no es un detalle: la primera
    versión dibujaba las elipses con `fill=(..., 120)` directo sobre el gradiente,
    y PIL no mezcla — PISA el píxel con ese alfa. El "fondo" quedaba con alpha 120
    en las zonas naranjas, alpha_composite componía dos veces contra el negro y la
    prueba de coherencia fallaba por el mock, no por el diseño. Se compone cada
    capa con un alfa explícito sobre una base opaca."""
    base = Image.new("RGBA", (w, h), (18, 12, 10, 255))
    d = ImageDraw.Draw(base, "RGBA")
    for y in range(h):
        t = y / h
        d.line([(0, y), (w, y)], fill=(int(70 + 120 * (1 - t)), int(28 + 40 * t), 14, 255))
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow, "RGBA")
    for cx, cy, r in [(300, 210, 190), (770, 340, 240)]:
        gd.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(210, 110, 26, 120))
    out = Image.alpha_composite(base, glow)
    assert out.getpixel((300, 210))[3] == 255, "el fondo del mock no es opaco"
    return out


def draw_stick(layer, C, cx, cy, size_dp):
    """Lo que pinta JoystickView.onDraw: base, aro, dos arcos de acento, anillo
    interior y perla. Los radios salen del mismo coeficiente (0.36 / 0.78 / 0.46)
    que está en el .java; si alguien cambia uno sin el otro, este dibujo se
    desalinearia del del teléfono, y por eso los numeros estan aca escritos."""
    d = ImageDraw.Draw(layer, "RGBA")
    radius = size_dp * PX / 2 * 0.36 / 0.5
    base = C["overlay_stick_base"]
    ring = C["overlay_stick_ring"]
    outer = C["overlay_stick_ring_outer"]
    d.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=base)
    d.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], outline=ring,
              width=max(1, int(round(dp("2.5dp")))))
    a = radius + dp("2dp")
    for start in (110, 290):
        d.arc([cx - a, cy - a, cx + a, cy + a], start, start + 55, fill=outer,
              width=max(1, int(round(dp("3.5dp")))))
    ir = radius * 0.78
    d.ellipse([cx - ir, cy - ir, cx + ir, cy + ir], outline=ring, width=1)
    kr = radius * 0.46
    d.ellipse([cx - kr, cy - kr, cx + kr, cy + kr], fill=C["overlay_stick_knob_lo"])
    d.ellipse([cx - kr, cy - kr, cx + kr, cy + kr], outline=C["overlay_stick_ring_active"],
              width=max(1, int(round(dp("1dp")))))


def main():
    C = load_colors()
    S = load_styles()
    W, H = 1180, 700
    fails = []

    # --- render con y sin overlay, sobre el MISMO fondo, para medir transparencia
    bg = menu_background(W, H)
    ov = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for style, box, label in BUTTONS:
        draw_button(ov, S, C, style, box, label=label)
    draw_stick(ov, C, 150, 545, 132)
    face = Image.alpha_composite(bg, ov).convert("RGB")
    face.save(OUT)

    # --- prueba 1: transparencia MEDIDA, no declarada.
    # Capa aparte con SOLO el relleno (sin contorno ni texto): medido sobre el
    # render completo lo que se compararía es la palabra «SELECT» contra el fondo,
    # no el relleno — ese confound existió y hizo fallar la primera versión.
    #
    # Dos comprobaciones, y las dos pueden fallar con cualquier valor de alfa:
    #   (a) cota absoluta: la deriva del centro de cada botón tiene que quedar por
    #       debajo del 8 % de la escala. Con alfa 0 da 0, y con alfa 30 % sobre el
    #       naranja brillante del fondo da ~14 % y revienta a propósito.
    #   (b) coherencia: la deriva medida tiene que coincidir con la que PREDICE el
    #       alfa declarado. Si alguien pinta con un color distinto del del recurso,
    #       o el polígono no se dibujó donde dice la caja, esto lo ve.
    # Una prueba que solo puede fallar cuando el valor ya es 0 es decorativa: por
    # eso se compara contra la predicción y no contra cero.
    fill_only = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for style, box, label in BUTTONS:
        draw_button(fill_only, S, C, style, box, only="fill")
    comp = Image.alpha_composite(bg, fill_only).convert("RGB")
    bare = bg.convert("RGB")
    a_fill = C["overlay_fill"][3] / 255.0
    F = C["overlay_fill"][:3]
    worst_pct, worst_pred = 0.0, 0.0
    for style, box, label in BUTTONS:
        cx, cy = (box[0] + box[2]) // 2, (box[1] + box[3]) // 2
        B = bare.getpixel((cx, cy))
        p_with = comp.getpixel((cx, cy))
        drift = sum(abs(x - y) for x, y in zip(p_with, B))
        pred = sum(abs(a_fill * (f - b)) for f, b in zip(F, B))
        pct = drift / 765.0
        worst_pct, worst_pred = max(worst_pct, pct), max(worst_pred, pred)
        if pct > 0.08:
            fails.append(f"{style}: el centro del boton cambia {pct*100:.1f}% del fondo "
                         f"(cota 8%) — el mando tapa el juego")
        if pred > 3 and abs(drift - pred) > 3:
            fails.append(f"{style}: deriva medida {drift} != predicha {pred:.1f} por el "
                         f"alfa declarado — el render no esta usando el color del recurso")
    print(f"  relleno alfa={a_fill*100:.1f}%  deriva maxima medida "
          f"{worst_pct*100:.2f}% del fondo (predicha {worst_pred:.1f}/765)")

    # --- prueba 2: alfas declarados dentro de las bandas del pedido
    def alpha(name):
        return C[name][3] / 255.0

    # La paleta fija como OBJETIVO 45–55 % y la cota dura del arnés es 40–60 %: el
    # margen existe para que un redondeo de dp/alfa no caiga el build, no para dejar
    # pasar un mando que tapa. Lo que sí importaba: con el techo en 75 % —que es como
    # estaba escrito este renglón en el primer verde— un anillo al 71 % pasaba verde,
    # y esa misma laxidad fue la que tapó el defecto de los glifos (prueba 8). Una
    # comprobación más floja que la ley que dice verificar es decorativa.
    for n in ("overlay_ring", "overlay_ring_cyan", "overlay_ring_magenta"):
        if not (0.40 <= alpha(n) <= 0.60):
            fails.append(f"{n} alpha={alpha(n)*100:.0f}% fuera de [40%,60%] (ley 2)")
    for n in ("overlay_ring_cyan", "overlay_ring_magenta"):
        d = alpha(n + "_pressed") - alpha(n)
        if d < 0.25:
            fails.append(f"el presionado de {n} no se distingue del reposo (delta {d*100:.0f}%)")
    if alpha("overlay_stick_base") > 0.55:
        fails.append(f"la base del stick tapa (alpha {alpha('overlay_stick_base')*100:.0f}%)")

    # --- prueba 3: ningún estilo del overlay declara shape Y cornerRadius a la vez
    for name, st in S.items():
        if name.startswith("PSX2.Overlay") and "shapeAppearanceOverlay" in st \
                and "cornerRadius" in st:
            fails.append(f"{name}: declara shape Y cornerRadius (el radio pisa el corte)")

    # --- prueba 4: las alas son espejo y cortadas hacia afuera
    def fams(style):
        return [f for f, _ in corners_of(S, style)]

    def sizes(style):
        return [int(s) for _, s in corners_of(S, style)]

    for lft, rgt in (("PSX2.OverlayPill.WingCyanLeft", "PSX2.OverlayPill.WingCyanRight"),
                     ("PSX2.OverlayPill.WingMagentaLeft", "PSX2.OverlayPill.WingMagentaRight")):
        if fams(lft)[:2] != ["cut", "rounded"] or fams(lft)[3] != "cut":
            fails.append(f"{lft}: no esta cortada por la izquierda (TL/TR/BL={fams(lft)[:3]})")
        if fams(rgt)[:2] != ["rounded", "cut"] or fams(rgt)[2] != "cut":
            fails.append(f"{rgt}: no esta cortada por la derecha")
        mirror = [fams(rgt)[1], fams(rgt)[0], fams(rgt)[3], fams(rgt)[2]]
        if fams(lft) != mirror or sizes(lft) != [sizes(rgt)[1], sizes(rgt)[0],
                                                 sizes(rgt)[3], sizes(rgt)[2]]:
            fails.append(f"{lft}/{rgt}: no son espejo exacto")

    # --- prueba 5: la cruceta es achaflanada, no un circulo
    if any(f != "cut" for f in fams("PSX2.OverlayCircle.Petal")):
        fails.append("el petalo de la cruceta no esta cortado en sus cuatro esquinas")

    # --- prueba 6: los faciales usan dos anillos, y cada uno es del color que dice
    rings = {s: rest_color(C, S, s)[0] for s in
             ("PSX2.OverlayCircle.FaceCyan", "PSX2.OverlayCircle.FaceMagenta")}
    if len(set(rings.values())) != 2:
        fails.append(f"los faciales no tienen dos anillos distintos: {rings}")
    for s, want in (("PSX2.OverlayCircle.FaceCyan", "overlay_ring_cyan"),
                    ("PSX2.OverlayCircle.FaceMagenta", "overlay_ring_magenta")):
        if rings[s] != want:
            fails.append(f"{s} apunta a {rings[s]}, deberia {want}")

    # --- prueba 7: todo estilo PSX2.Overlay* que aparece en los tres layouts existe
    # (18 nodos por layout: los 14 retocados en 0.6.34 mas L3/R3/pausa/avance rapido)
    for f in ("layout", "layout-land", "layout-port"):
        p = os.path.join(RES, f, "activity_main.xml")
        if not os.path.isfile(p):
            continue
        used = set(re.findall(r'style="@style/(PSX2\.Overlay[A-Za-z.]*)"', open(p).read()))
        for u in sorted(used):
            if u not in S:
                fails.append(f"{f}/activity_main.xml usa {u}, que no esta definido")
        print(f"  {f}: {len(used)} estilos del overlay usados, todos definidos")

    # --- prueba 8: los GLIFOS (delta, circulo, equis, cuadrado, cruceta) tambien
    # entran en el presupuesto de transparencia. Esta prueba no estaba en el primer
    # verde y sin ella el diseño mentia: los iconos viven en res/color/overlay_face_*.xml
    # y overlay_dpad_tint.xml — archivos que NINGUN estilo referencia, por eso la
    # prueba 2 no los veia — y estaban al 71 % de alfa, por encima de la banda que
    # este mismo script exige para los anillos. Un contorno al 55 % con una leyenda
    # al 71 % es un mando que se pinta dos veces: la leyenda tapa.
    def selector_alfas(name):
        """(alpha_reposo, alpha_presionado) de un selector de res/color/, aceptando
        tanto literales (#AARRGGBB, como los glifos) como referencias @color/...
        (como overlay_text_sel, que apunta a la paleta). Separar los dos estados por
        el renglón del <item> y no por posición: el item de reposo a veces es el
        último y a veces el único sin estado en el medio."""
        sel = os.path.join(RES, "color", name + ".xml")
        if not os.path.isfile(sel):
            return None
        body = open(sel).read()
        pat = r'android:color="((?:#[0-9A-Fa-f]{8})|(?:@color/[a-z_0-9]+))"'

        def alpha_of(tok):
            if tok.startswith('@'):
                return C[tok.replace('@color/', '')][3] / 255.0
            return int(tok[1:3], 16) / 255.0

        segs = [s for s in body.split("<item ")[1:]]
        pressed = [alpha_of(m) for s in segs if 'state_pressed="true"' in s
                   for m in re.findall(pat, s)]
        rest = [alpha_of(m) for s in segs if 'state_pressed="true"' not in s
                for m in re.findall(pat, s)]
        if len(rest) != 1 or len(pressed) != 1:
            raise SystemExit(f"{name}: no se pueden separar reposo y presionado "
                             f"(reposo={rest} presionado={pressed})")
        return rest[0], pressed[0]

    glyphs = ["overlay_face_triangle", "overlay_face_circle", "overlay_face_cross",
              "overlay_face_square", "overlay_dpad_tint", "overlay_text_sel"]
    for g in glyphs:
        al = selector_alfas(g)
        if al is None:
            fails.append(f"{g}: no existe en res/color/")
            continue
        rest, prev = al
        if not (0.40 <= rest <= 0.60):
            fails.append(f"{g}: en reposo al {rest*100:.0f}%, fuera de [40%,60%] "
                         f"(misma ley 2 que los anillos)")
        if prev - rest < 0.15:
            fails.append(f"{g}: el presionado no se distingue del reposo "
                         f"(delta {(prev-rest)*100:.0f}%)")

    # y que todo iconTint del layout apunte a un color que existe. Ojo: "existe" se
    # comprueba contra TODOS los values*/ con <color>, no solo contra cenit_overlay.xml
    # — la primera versión de esta comprobación denunció @color/md_theme_tertiary, que
    # está definido desde siempre en values/colors.xml. Un verificador que acusa un
    # recurso sano es peor que no tenerlo: enseña a desconfiar del verde.
    def known_color_names():
        names = set(os.listdir(os.path.join(RES, "color")))
        names = {n[:-4] for n in names if n.endswith(".xml")}
        for d in sorted(os.listdir(RES)):
            if not d.startswith("values"):
                continue
            for fn in sorted(os.listdir(os.path.join(RES, d))):
                if not fn.endswith(".xml"):
                    continue
                for c in ET.parse(os.path.join(RES, d, fn)).getroot().findall("color"):
                    names.add(c.get("name"))
        return names

    palette = known_color_names()
    for f in ("layout", "layout-land", "layout-port"):
        p = os.path.join(RES, f, "activity_main.xml")
        if not os.path.isfile(p):
            continue
        for ref in sorted(set(re.findall(r'app:iconTint="@color/([a-z_0-9]+)"',
                                         open(p).read()))):
            if ref not in palette:
                fails.append(f"{f}/activity_main.xml usa @color/{ref}, inexistente")

    # --- informe
    print("render:", OUT)
    for name in ("overlay_fill", "overlay_ring", "overlay_ring_cyan", "overlay_ring_magenta",
                 "overlay_ring_cyan_pressed", "overlay_stick_base", "overlay_stick_ring",
                 "overlay_stick_ring_outer"):
        r, g, b, a = C[name]
        print(f"  {name:28s} #{r:02X}{g:02X}{b:02X} alpha={a/255*100:5.1f}%")
    for st in ("PSX2.OverlayPill.WingCyanLeft", "PSX2.OverlayPill.WingCyanRight",
               "PSX2.OverlayCircle.Petal", "PSX2.OverlayPill", "PSX2.OverlayCircle"):
        print(f"  {st:34s} "
              f"{list(zip(fams(st), sizes(st)))} "
              f"cornerRadius={S[st].get('cornerRadius', '-')}")
    print()
    if fails:
        for f in fails:
            print("FALLO:", f)
        print(f"render_overlay: {len(fails)} fallos")
        sys.exit(1)
    print("render_overlay: OK — transparencia medida por pixeles, alas espejo, petalos "
          "cortados, dos anillos de cara y estilos del layout resueltos sobre el XML real")


if __name__ == "__main__":
    main()
