#!/usr/bin/env python3
"""
Vuelca las cadenas constantes por método de un archivo smali.

Útil para leer configuración embebida (URLs, cabeceras, content-types)
cuando R8 ha ofuscado los nombres.

Uso: volcar_smali.py <archivo.smali> [regex_filtro_metodo]
"""
import re
import sys

path = sys.argv[1]
filtro = sys.argv[2] if len(sys.argv) > 2 else None

txt = open(path, errors="replace").read()
methods = re.split(r"^\.method ", txt, flags=re.M)[1:]
for m in methods:
    name = m.split("(")[0].split()[-1]
    if filtro and not re.search(filtro, name):
        continue
    strs = re.findall(r'const-string(?:/[a-z0-9]+)? [vp][0-9]+, "([^"]*)"', m)
    ints = re.findall(r"const(?:/[a-z0-9]+)? [vp][0-9]+, (0x[0-9a-fA-F]+|[0-9]+)\b", m)
    if not strs and not ints:
        continue
    print(f"\n### {name}")
    if strs:
        print("   strings:")
        for s in strs:
            print(f"     {s!r}")
    if ints:
        print(f"   ints: {ints}")
