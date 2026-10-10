#!/usr/bin/env python3
"""
Verificador de contratos JNI (Cenit).

CI compila el C++ y el Java por separado: si un `native` de NativeApp.java no tiene
su funcion Java_...() correspondiente en el C++, NO hay error de compilacion ni de
linking (los simbolos JNI se resuelven por nombre en runtime) y el fallo llega al
telefono como UnsatisfiedLinkError. Este script cierra ese hueco sin telefono.

Compara:
  1) cada `static native <tipo> nombre(args);` de NativeApp.java con
     `Java_com_izzy2lost_psx2_NativeApp_nombre(...)` en los .cpp, y
  2) el NUMERO de argumentos (el nombre solo no basta: una JNI con un argumento de
     menos compila y se estrella al invocar).

Salida: 0 = todo casado. 1 = hay huecos (los imprime).
"""
import re, sys, pathlib

ROOT = pathlib.Path("/app/cenit/app/src/main")
JAVA = ROOT / "java/com/izzy2lost/psx2/NativeApp.java"
CPPS = list((ROOT / "cpp").rglob("*.cpp"))

def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src

def split_args(s):
    s = s.strip()
    if not s:
        return []
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "<([{":
            depth += 1
        elif ch in ">)]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out

jsrc = strip_comments(JAVA.read_text(encoding="utf-8"))
# static native RET nombre(ARGS);  (RET puede llevar [] o <>)
jnatives = {}
for m in re.finditer(r"\bnative\s+(?:static\s+)?[\w\[\]<>.,\s]+?\s(\w+)\s*\(([^;{]*)\)\s*;", jsrc):
    name, args = m.group(1), split_args(m.group(2))
    jnatives[name] = len(args)

# tambien los no-static (instance natives): la regex de arriba ya los toma por 'native'
cpp_args = {}
for f in CPPS:
    src = strip_comments(f.read_text(encoding="utf-8", errors="replace"))
    for m in re.finditer(r"Java_com_izzy2lost_psx2_NativeApp_(\w+)\s*\(([^)]*)\)", src):
        name, args = m.group(1), split_args(m.group(2))
        # el primer arg siempre es JNIEnv*, el segundo jclass/jobject: no cuentan
        n = max(0, len(args) - 2)
        cpp_args[name] = n
        if f.name != "native-lib.cpp":
            cpp_args[name + "@" + f.name] = n

missing = sorted(k for k in jnatives if k not in cpp_args)
extra = sorted(k for k in cpp_args if k in jnatives)
mismatch = sorted((k, jnatives[k], cpp_args[k]) for k in jnatives
                  if k in cpp_args and jnatives[k] != cpp_args[k])

print("Java natives: %d | simbolos C++: %d" % (len(jnatives), len(
    [k for k in cpp_args if '@' not in k])))
for k in missing:
    print("FALTA EN C++   : %s (%d args en Java)" % (k, jnatives[k]))
for k, a, b in mismatch:
    print("ARGUMENTOS     : %s Java=%d C++=%d" % (k, a, b))
if not extra:
    pass

rc = 1 if (missing or mismatch) else 0
print("RESULTADO: %s" % ("OK: cada native tiene su JNI con el mismo numero de args"
                         if rc == 0 else "HAY HUECOS (ver arriba)"))
sys.exit(rc)
