#!/usr/bin/env python3
"""
Extractor de la superficie HTTP de la APK ToDus.

La APK ofusca las anotaciones de Retrofit con R8, por lo que el verbo HTTP y
el tipo de parámetro NO se deducen de los nombres: se leen del propio
`RequestFactory` de Retrofit empaquetado en la APK.

Evidencia (smali_classes7/mi/z$a.smali, método `e` = resolución de verbo y
método `g` = resolución de parámetro):

    Loi/b; -> DELETE      Loi/f; -> GET       Loi/g; -> HEAD
    Loi/n; -> PATCH       Loi/o; -> POST      Loi/p; -> PUT
    Loi/m; -> OPTIONS     Loi/h; -> @HTTP     Loi/k; -> @Headers

    Loi/y; -> @Url   (error "@Path parameters may not be used with @Url.")
    Loi/s; -> @Path  Loi/t; -> @Query  Loi/a; -> @Body
    Loi/x; -> @Tag   (error "@Tag type ")  Loi/q; -> @Part
    Loi/v; -> @QueryMap  Loi/u; -> @FieldMap  Loi/c; -> @Header
    Loi/i; -> @QueryName  Loi/j; -> @HeaderMap  Loi/d; -> @Field
    Loi/r; -> @PartMap

Uso: extraer_api.py <apktool-out> [salida.md]
"""
import re
import sys
import pathlib

VERB = {
    "b": "DELETE", "f": "GET", "g": "HEAD", "n": "PATCH",
    "o": "POST", "p": "PUT", "m": "OPTIONS", "h": "@HTTP",
}
PARAM_ANN = {
    "y": "@Url", "s": "@Path", "t": "@Query", "a": "@Body", "x": "@Tag",
    "q": "@Part", "r": "@PartMap", "v": "@QueryMap", "u": "@FieldMap",
    "c": "@Header", "i": "@QueryName", "j": "@HeaderMap", "d": "@Field",
}

METHOD_RE = re.compile(r"^\.method (?:public )?abstract (\S+?)\((.*?)\)(.*?)\n^\.end method", re.M | re.S)
CLASS_RE = re.compile(r"^\.class .*?(L[\w/$]+;)", re.M)
PARAM_RE = re.compile(
    r"^\s{4}\.param (p\d+)\s+# ([\w/$]+);\n((?:\s{8}\.annotation runtime Loi/([a-z]);\n"
    r"(?:\s{12}value = .*\n)?\s{8}\.end annotation\n)*)", re.M)
VERB_RE = re.compile(
    r"^\s{4}\.annotation runtime Loi/([a-z]);\n"
    r"(?:\s{8}value = \"([^\"]*)\"\n)?", re.M)
PARAM_ANN_RE = re.compile(r"Loi/([a-z]);")


def parse_service(smali: pathlib.Path):
    text = smali.read_text(errors="replace")
    cls = CLASS_RE.search(text)
    name = cls.group(1)[1:-1].replace("/", ".") if cls else smali.stem
    out = []
    for m in METHOD_RE.finditer(text):
        mname, sig, body = m.group(1), m.group(2), m.group(3)
        verb = None
        for cand in VERB_RE.finditer(body):
            key = cand.group(1)
            # solo las anotaciones de verbo HTTP (no @Headers, que lleva un array)
            if key in VERB:
                verb = cand
                break
        http, path = ("?", "?")
        if verb:
            key = verb.group(1)
            http = VERB.get(key, f"Loi/{key}")
            path = verb.group(2) or ""  # sin valor = ruta vacía (@Url dinámico)
        params = []
        for pm in PARAM_RE.finditer(body):
            ptype = pm.group(2).split("/")[-1].rstrip(";")
            ann = pm.group(4) or pm.group(5)
            kind = PARAM_ANN.get(ann, f"Loi/{ann}" if ann else "-")
            params.append((kind, ptype))
        # params sin anotación propia (p.ej. path implícito)
        if not params and sig:
            for t in re.findall(r"L([\w/$]+);", sig):
                params.append(("-", t.split("/")[-1]))
        out.append((mname, http, path, params))
    return name, out


def main():
    root = pathlib.Path(sys.argv[1])
    out_file = sys.argv[2] if len(sys.argv) > 2 else None
    services = []
    for sr in sorted(root.glob("smali*")):
        for sm in sr.rglob("*Service.smali"):
            if "/cu/todus/" not in str(sm):
                continue
            services.append(parse_service(sm))
    services.sort(key=lambda x: x[0])

    lines = ["# Superficie HTTP de ToDus (Retrofit)", "",
             "Extraído del smali de la APK v3.0.2 (`cu.todus.android`, 2026-10-20).",
             "",
             "Los verbos y tipos de parámetro **no se infieren**: se leen del",
             "`RequestFactory` de Retrofit incluido en la propia APK, que resuelve",
             "las anotaciones ofuscadas por R8 (`Lretrofit2/http/POST;` → `Loi/o;`).",
             ""]
    total = 0
    for name, methods in services:
        if not any(a != "?" for _, a, _, _ in methods):
            continue
        lines += [f"## {name}", "", "| Método | Verbo | Ruta | Parámetros |", "|---|---|---|---|"]
        for mname, http, path, params in methods:
            total += 1
            ps = ", ".join(f"{k} {t}" for k, t in params) or "—"
            lines.append(f"| `{mname}` | **{http}** | `{path}` | {ps} |")
        lines.append("")
    text = "\n".join(lines) + f"\n**Total de operaciones:** {total}\n"
    if out_file:
        pathlib.Path(out_file).write_text(text)
        print(f"escrito: {out_file} ({total} operaciones)")
    else:
        print(text)


if __name__ == "__main__":
    main()
