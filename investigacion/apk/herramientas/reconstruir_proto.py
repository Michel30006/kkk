#!/usr/bin/env python3
"""
Reconstruye el esquema .proto de los mensajes protobuf de la APK ToDus.

La app usa protobuf-lite (GeneratedMessageLite), que NO embebe un
FileDescriptorProto. La fuente autoritativa de la numeración son las
constantes generadas por protoc:

    .field public static final USERNAME_FIELD_NUMBER:I = 0x1

y el tipo de cada campo se toma del getter generado
(`getUsername()Ljava/lang/String;`), con la firma genérica real tomada
de la anotación `Ldalvik/annotation/Signature;`.

Uso: reconstruir_proto.py <apktool-out> <prefijo_clase> [salida.proto]
Ej.: reconstruir_proto.py apktool-out "cu/todus/proto/ApiProto"
"""
import re
import sys
import pathlib

SCALARS = {
    "Ljava/lang/String;": "string",
    "Lcom/google/protobuf/ByteString;": "bytes",
    "Z": "bool",
    "I": "int32",
    "J": "int64",
    "F": "float",
    "D": "double",
    "B": "int32",
    "S": "int32",
    "C": "int32",
}

NUM_CONST = re.compile(r"^\.field public static final (\w+)_FIELD_NUMBER:I = (0x[0-9a-fA-F]+|\d+)", re.M)
PRIV_FIELD = re.compile(r"^\.field private (?:volatile )?([\w$]+_):(L[\w/$;]+);", re.M)
METHOD = re.compile(
    r"^\.method public (?:final )?([\w$]+)\(([^)]*)\)(L[\w/$;]+);\n"
    r"((?:.|\n)*?)^\.end method", re.M)
GETTER_SIG = re.compile(
    r"^\.method public (?:final )?(get[A-Z]\w*)\([^)]*\)(L[\w/$;]+);\n"
    r"((?:.|\n)*?)^\.end method", re.M)
GETTER_SIG2 = re.compile(
    r"^\.method public (?:final )?(get[A-Z]\w*)\(([^)]*)\)(L[\w/$;]+);\n"
    r"((?:.|\n)*?)^\.end method", re.M)
SIGNATURE = re.compile(r"\.annotation system Ldalvik/annotation/Signature;.*?value = \{(.*?)\}", re.S)


def norm(s):
    return s.replace("_", "").lower()


def type_of(java_type, generic):
    """Convierte el tipo java (y su firma genérica) en tipo proto."""
    if generic:
        g = generic.strip()
        if g.startswith("()"):
            g = g[2:]
        m = re.match(r"Ljava/util/List<(.+)>;?$", g)
        if m:
            return "repeated " + type_of(m.group(1), None)
        m = re.match(r"Ljava/util/Map<(.+?),(.+?)>;?$", g)
        if m:
            k = type_of(m.group(1), None)
            v = type_of(m.group(2), None)
            return f"map<{k}, {v}>"
        m = re.match(r"(L[\w/$]+);$", g)
        if m:
            java_type = m.group(1) + ";"
    java_type = java_type.strip()
    if not java_type.endswith(";") and java_type.startswith("L"):
        java_type += ";"
    if java_type in SCALARS:
        return SCALARS[java_type]
    if java_type.startswith("Ljava/util/List"):
        return "repeated <unknown>"
    if java_type.startswith("Ljava/util/Map"):
        return "map<string, string>"
    if java_type.startswith("L"):
        return java_type.strip("L;").split("/")[-1].replace("$", ".")
    return java_type


def parse_message(smali: pathlib.Path):
    txt = smali.read_text(errors="replace")
    cm = re.search(r"^\.class .*?(L[\w/$]+;)", txt, re.M)
    if not cm:
        return None
    full = cm.group(1)[1:-1]
    name = full.split("/")[-1].replace("$", ".")
    if name.endswith(".OrBuilder") or name.endswith(".Builder"):
        return None

    priv = dict(PRIV_FIELD.findall(txt))
    if not priv:
        return None

    # números de campo
    numbers = {}
    for cname, cnum in NUM_CONST.findall(txt):
        numbers[norm(cname)] = int(cnum, 0)

    # getters -> (tipo java, firma genérica)
    getters = {}
    for gname, gtype, body in GETTER_SIG.findall(txt):
        if gname.endswith("Bytes") or gname in ("getDefaultInstance", "getDescriptor",
                                                "getSerializedSize", "getUnknownFields",
                                                "getParser", "getDefaultInstanceForType"):
            continue
        sig = SIGNATURE.search(body)
        generic = None
        if sig:
            parts = re.findall(r'"([^"]*)"', sig.group(1))
            joined = "".join(parts)
            generic = joined if "Ljava/util/" in joined else None
        getters[gname] = (gtype, generic)

    fields = []
    # getters con índice -> campo repetido; su tipo de retorno es el elemento
    indexed = {}
    for gname, gargs, gtype, gbody in GETTER_SIG2.findall(txt):
        if gargs.strip() == "I":
            indexed[gname] = gtype
    for jname, jtype in priv.items():
        pname = jname[:-1]
        num = numbers.get(norm(pname))
        gname = "get" + pname[0].upper() + pname[1:]
        g = getters.get(gname)
        ptype = type_of(g[0], g[1]) if g else type_of(jtype, None)
        # campos repetidos (protobuf-lite usa Internal$*List)
        if jtype.startswith("Lcom/google/protobuf/Internal$"):
            base = jtype.split("$")[-1].rstrip(";")
            simple = {
                "ProtobufList": None, "LazyStringList": "string", "IntList": "int32",
                "LongList": "int64", "DoubleList": "double", "FloatList": "float",
                "BooleanList": "bool", "ByteStringList": "bytes",
            }.get(base)
            if simple is None:
                elem = indexed.get(gname)
                simple = type_of(elem, None) if elem else "<message>"
            ptype = f"repeated {simple}"
        elif gname in indexed:
            ptype = f"repeated {type_of(indexed[gname], None)}"
        fields.append((pname, num, ptype))
    fields.sort(key=lambda f: (f[1] is None, f[1] if f[1] is not None else 0))
    return full.replace("/", "."), name, fields


def main():
    root = pathlib.Path(sys.argv[1])
    prefix = sys.argv[2]
    out_file = sys.argv[3] if len(sys.argv) > 3 else None
    files = [sm for sr in sorted(root.glob("smali*")) for sm in sr.rglob("*.smali")
             if "/" + prefix.replace(".", "/") + "$" in str(sm).replace("\\", "/")]
    pkg = prefix.split("/")[-1].lower()
    lines = [
        "// Reconstrucción del esquema protobuf de la app ToDus",
        f"// Origen: {prefix} — protobuf-lite (sin FileDescriptorProto embebido)",
        "// Números: constantes <CAMPO>_FIELD_NUMBER generadas por protoc.",
        "// Tipos: getters generados + anotación Signature (genéricos reales).",
        "",
        'syntax = "proto3";',
        "",
        f"package {pkg};",
        "",
    ]
    total = 0
    sin_numero = []
    # árbol de mensajes: raíz = el nombre Java exterior (ApiProto)
    root = {"__campos__": None, "__hijos__": {}}
    for f in sorted(files):
        parsed = parse_message(f)
        if not parsed:
            continue
        full, name, fields = parsed
        path = name.split(".")
        node = root
        for part in path:
            node = node["__hijos__"].setdefault(part, {"__campos__": None, "__hijos__": {}})
        node["__campos__"] = fields
    tree = root["__hijos__"]

    # rutas conocidas de mensajes (sin el prefijo exterior)
    paths = set()

    def collect(node, prefix):
        for name, info in node.items():
            p = f"{prefix}.{name}" if prefix else name
            paths.add(p)
            collect(info["__hijos__"], p)

    root_name = next(iter(tree)) if tree else ""
    collect(tree[root_name]["__hijos__"], "")

    def qualify(ptype, scope):
        """Resuelve un tipo de mensaje a su nombre relativo válido en el scope."""
        if ptype.startswith("repeated "):
            return "repeated " + qualify(ptype[len("repeated "):], scope)
        if ptype.startswith("map<"):
            return ptype
        if not re.match(r"^[A-Z]\w*(\.\w+)*$", ptype):
            return ptype
        cand = ptype
        if cand.startswith(root_name + "."):
            cand = cand[len(root_name) + 1:]
        parts = cand.split(".")
        # busca el sufijo más corto resoluble desde el scope
        for i in range(len(parts)):
            rel = ".".join(parts[i:])
            if rel in paths and (not scope or rel.startswith(scope) or scope.startswith(rel)):
                return rel
        return cand

    def emit(node, scope="", indent=0):
        nonlocal total
        pad = "  " * indent
        for name in sorted(node):
            info = node[name]
            campos = info["__campos__"] or []
            scope_here = f"{scope}.{name}" if scope else name
            if campos:
                print(f"{pad}message {name} {{", file=out)
                for pname, num, ptype in campos:
                    ptype = qualify(ptype, scope_here)
                    if num is None:
                        sin_numero.append(f"{scope_here}.{pname}")
                        print(f"{pad}  // {ptype} {pname} = ?;", file=out)
                    else:
                        total += 1
                        print(f"{pad}  {ptype} {pname} = {num};", file=out)
                emit(info["__hijos__"], scope_here, indent + 1)
                print(f"{pad}}}", file=out)
            else:
                emit(info["__hijos__"], scope_here, indent)

    out = open(out_file, "w") if out_file else sys.stdout
    if out_file:
        out.write("\n".join(lines))
    emit(tree[root_name]["__hijos__"], scope="")
    if out_file:
        out.write("\n")
        out.close()
    text = None
    if out_file:
        print(f"escrito: {out_file} — {total} campos numerados, {len(sin_numero)} sin número")
        if sin_numero:
            print("sin número:", ", ".join(sin_numero[:30]))
    else:
        print(text)


if __name__ == "__main__":
    main()
