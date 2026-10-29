#!/usr/bin/env python3
"""
Extrae la tabla de IQs propios de ToDus (plano de mensajería XMPP).

Cada clase de IQ expone dos constantes estáticas:
    ELEMENT   -> nombre local del hijo del <iq>
    NAMESPACE -> valor de xmlns (p.ej. "todus:gurl", "todus:roster:list:2")

Uso: extraer_iqs.py <apktool-out> [salida.md]
"""
import re
import sys
import pathlib

FIELD = re.compile(
    r'^\.field public static final (ELEMENT|NAMESPACE):Ljava/lang/String; = "([^"]*)"', re.M)
METHOD = re.compile(r"^\.method public (?:final )?(\w+)\(", re.M)


def main():
    root = pathlib.Path(sys.argv[1])
    out_file = sys.argv[2] if len(sys.argv) > 2 else None
    rows = []
    for sr in sorted(root.glob("smali*")):
        base = sr / "cu/todus/android/xmpp/iq"
        if not base.is_dir():
            continue
        for sm in sorted(base.rglob("*.smali")):
            if "$" in sm.name or sm.name.endswith("Provider.smali"):
                continue
            txt = sm.read_text(errors="replace")
            fields = dict(FIELD.findall(txt))
            if "ELEMENT" not in fields and "NAMESPACE" not in fields:
                continue
            metodos = sorted(set(METHOD.findall(txt)))
            rel = str(sm.relative_to(base)).replace("\\", "/")[:-6]
            rows.append((rel, fields.get("ELEMENT", ""), fields.get("NAMESPACE", ""), metodos))
    rows.sort()
    lines = ["# IQs propios de ToDus (API XMPP de control)", "",
             "Cada IQ es un `<iq>` de XMPP cuyo hijo lleva `ELEMENT` como nombre local",
             "y `NAMESPACE` como valor de `xmlns`.", "",
             "| IQ | ELEMENT | NAMESPACE |", "|---|---|---|"]
    for rel, el, ns, _ in rows:
        lines.append(f"| `{rel}` | `{el or '—'}` | `{ns or '—'}` |")
    lines += ["", f"**Total de IQs:** {len(rows)}", "",
              "## Detalle de constructores y getters", ""]
    for rel, el, ns, metodos in rows:
        interesting = [m for m in metodos if m.startswith("get") and m not in
                        ("getIQChildElementBuilder", "getType", "getTo", "getFrom",
                         "getStanzaId", "getElementName", "getNamespace")]
        lines.append(f"- **{rel}** (`{el or '—'}` / `{ns or '—'}`): "
                     + (", ".join(f"`{m}`" for m in interesting) or "—"))
    text = "\n".join(lines) + "\n"
    if out_file:
        pathlib.Path(out_file).write_text(text)
        print(f"escrito: {out_file} ({len(rows)} IQs)")
    else:
        print(text)


if __name__ == "__main__":
    main()
