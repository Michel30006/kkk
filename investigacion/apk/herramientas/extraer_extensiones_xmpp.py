#!/usr/bin/env python3
"""
Extrae la tabla de extensiones XMPP propias de ToDus.

Para cada clase `cu/todus/android/xmpp/extension/**` lee el literal que
devuelve `getNamespace()` y `getElementName()` en el smali.

Uso: extraer_extensiones_xmpp.py <apktool-out> [salida.md]
"""
import re
import sys
import pathlib


def literal_return(txt, method):
    m = re.search(r"^\.method [^\n]*" + re.escape(method) + r"\([^\n]*\n(.*?)^\.end method",
                  txt, re.M | re.S)
    if not m:
        return None
    s = re.search(r'const-string v\d+, "([^"]*)"', m.group(1))
    return s.group(1) if s else None


def main():
    root = pathlib.Path(sys.argv[1])
    out_file = sys.argv[2] if len(sys.argv) > 2 else None
    rows = []
    for sr in sorted(root.glob("smali*")):
        base = sr / "cu/todus/android/xmpp/extension"
        if not base.is_dir():
            continue
        for sm in sorted(base.rglob("*.smali")):
            if "$" in sm.name:
                continue
            txt = sm.read_text(errors="replace")
            ns = literal_return(txt, "getNamespace")
            el = literal_return(txt, "getElementName")
            if ns or el:
                rel = str(sm.relative_to(base)).replace("\\", "/")
                rows.append((rel[:-6], ns, el))
    lines = ["# Extensiones XMPP propias de ToDus", "",
             "Extraído de los métodos `getNamespace()` / `getElementName()` de cada",
             "extensión (`cu.todus.android.xmpp.extension.*`).", "",
             "| Clase | Namespace | Elemento |", "|---|---|---|"]
    for cls, ns, el in rows:
        lines.append(f"| `{cls}` | `{ns or '—'}` | `{el or '—'}` |")
    lines += ["", f"**Total de extensiones:** {len(rows)}", ""]
    text = "\n".join(lines)
    if out_file:
        pathlib.Path(out_file).write_text(text)
        print(f"escrito: {out_file} ({len(rows)} extensiones)")
    else:
        print(text)


if __name__ == "__main__":
    main()
