#!/usr/bin/env python3
"""Extrae las URLs base por servicio y entorno desde Service.smali (ToDus)."""
import re
import sys

path = sys.argv[1]
txt = open(path, errors="replace").read()

# Divide por declaraciones de método
methods = re.split(r"^\.method ", txt, flags=re.M)[1:]
for m in methods:
    name = m.split("(")[0].split()[-1]
    if not (name.startswith("provide") or name == "isToDusUrl"):
        continue
    strs = re.findall(r'const-string(?:/[a-z0-9]+)? [vp][0-9]+, "([^"]*)"', m)
    ints = re.findall(r"const(?:/[a-z0-9]+)? [vp][0-9]+, (0x[0-9a-fA-F]+|[0-9]+)\b", m)
    print(f"{name}:")
    if strs:
        print(f"   strings = {strs}")
    if ints:
        print(f"   ints    = {ints}")
