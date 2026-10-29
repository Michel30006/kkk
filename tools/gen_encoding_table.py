#!/usr/bin/env python3
"""Genera la tabla de codificación v1 (256 símbolos) y sus artefactos.

Herramienta de DESARROLLO. No forma parte del sistema en producción: el
resultado es una tabla estática, literal y versionada que se compila en C++.

Criterios (ver docs/tabla-codificacion.md):
  * exactamente 256 símbolos únicos;
  * un símbolo por cada valor de byte 0x00..0xFF;
  * nada de espacios, controles ni separadores (el transporte XMPP los
    normaliza o los recorta);
  * nada de caracteres especiales de XML (< > & " ') para no necesitar
    escapes ni permitir inyección de markup;
  * nada del separador de trama '|';
  * NFC(c) == c y NFKC(c) == c, para sobrevivir a una normalización Unicode
    del transporte o de la base de datos;
  * preferencia por UTF-8 de 1 byte (ASCII) y, después, de 2 bytes, para no
    inflar el tamaño transmitido.

Uso:
    python3 tools/gen_encoding_table.py
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import sys
import unicodedata

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXCLUDED_XML = set('<>"&\'|')

# ASCII imprimible sin espacio (0x21..0x7E), menos los especiales.
BLOCK_ASCII = [chr(c) for c in range(0x21, 0x7F) if chr(c) not in EXCLUDED_XML]
# Latin-1: solo letras acentuadas (0x00C0..0x00FF). Se excluyen × y ÷.
BLOCK_LATIN1 = [
    chr(c) for c in range(0x00C0, 0x0100) if c not in (0x00D7, 0x00F7)
]
# Latin Extended-A: letras latinas precompuestas.
BLOCK_LATIN_EXT = [chr(c) for c in range(0x0100, 0x0180)]


def is_safe(ch: str) -> bool:
    if ch in EXCLUDED_XML or ch.isspace():
        return False
    if unicodedata.category(ch)[0] not in ("L", "N", "P", "S"):
        return False
    if unicodedata.combining(ch):
        return False
    if unicodedata.normalize("NFC", ch) != ch:
        return False
    if unicodedata.normalize("NFKC", ch) != ch:
        return False
    return True


def utf8_hex(ch: str) -> str:
    return ch.encode("utf-8").hex().upper()


def cxx_escape(ch: str) -> str:
    raw = ch.encode("utf-8")
    if len(raw) == 1:
        if ch == "\\":
            return '"\\\\"'
        return '"' + ch + '"'
    return '"' + "".join(f"\\x{b:02X}" for b in raw) + '"'


def main() -> int:
    table: list[str] = []
    for block in (BLOCK_ASCII, BLOCK_LATIN1, BLOCK_LATIN_EXT):
        for ch in block:
            if len(table) == 256:
                break
            if is_safe(ch) and ch not in table:
                table.append(ch)
    if len(table) != 256:
        print(f"ERROR: solo {len(table)} símbolos válidos", file=sys.stderr)
        return 1
    assert len(set(table)) == 256
    assert all(len(ch.encode("utf-8")) <= 2 for ch in table)

    one_byte = sum(1 for ch in table if len(ch.encode("utf-8")) == 1)
    two_byte = 256 - one_byte
    avg = sum(len(ch.encode("utf-8")) for ch in table) / 256

    out_src = ROOT / "shared" / "src" / "encoding_table.inc"
    out_res = ROOT / "shared" / "resources" / "table_v1.txt"
    out_doc = ROOT / "docs" / "tabla-codificacion.md"
    out_src.parent.mkdir(parents=True, exist_ok=True)
    out_res.parent.mkdir(parents=True, exist_ok=True)
    out_doc.parent.mkdir(parents=True, exist_ok=True)

    literal = out_res.read_text(encoding="utf-8") if out_res.exists() else ""
    raw = "".join(table).encode("utf-8")
    digest = hashlib.sha256(raw).hexdigest()
    out_res.write_bytes(raw)

    lines: list[str] = []
    lines.append("// Generado por tools/gen_encoding_table.py — NO EDITAR A MANO.")
    lines.append("// Tabla de codificación v1: 256 símbolos, uno por valor de byte.")
    lines.append(f"// SHA-256 de los 256 símbolos concatenados (UTF-8): {digest}")
    lines.append("// Distribución: %d símbolos de 1 byte, %d de 2 bytes, media %.4f B/símbolo."
                 % (one_byte, two_byte, avg))
    lines.append("")
    lines.append(f'static constexpr std::string_view kTableSha256 = "{digest}";')
    lines.append("")
    lines.append("static constexpr std::array<char32_t, 256> kTableCodepoints = {")
    for i in range(0, 256, 8):
        chunk = ", ".join(f"0x{ord(c):04X}" for c in table[i:i + 8])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    lines.append("static constexpr std::array<std::string_view, 256> kTableUtf8 = {")
    for i in range(0, 256, 8):
        chunk = ", ".join(cxx_escape(c) for c in table[i:i + 8])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    out_src.write_text("\n".join(lines) + "\n", encoding="utf-8")

    with out_doc.open("w", encoding="utf-8") as fh:
        fh.write("# Tabla de codificación v1\n\n")
        fh.write("Generada por `tools/gen_encoding_table.py`. **No editar a mano.**\n\n")
        fh.write(f"- Versión: **1**\n- Símbolos: **256** (únicos)\n")
        fh.write(f"- SHA-256 de la tabla concatenada: `{digest}`\n")
        fh.write(f"- Símbolos de 1 byte UTF-8: **{one_byte}**; de 2 bytes: **{two_byte}**\n")
        fh.write(f"- Coste medio: **{avg:.4f} bytes UTF-8 por byte de vídeo**\n")
        fh.write(f"- Fichero plano: `shared/resources/table_v1.txt`\n\n")
        fh.write("## Criterios de selección\n\n")
        fh.write("- Se excluyen los especiales XML `<`, `>`, `&`, `\"`, `'` y el separador `|`.\n")
        fh.write("- Se excluyen espacios, controles, separadores y marcas combinantes.\n")
        fh.write("- Todo símbolo cumple `NFC(c) == c` y `NFKC(c) == c` (estable ante normalización).\n")
        fh.write("- Se prefiere ASCII de 1 byte y luego Latin-1 / Latin Extended-A de 2 bytes.\n\n")
        fh.write("## Tabla\n\n| Byte | CP | Símbolo | UTF-8 |\n|---:|---|---|---|\n")
        for i, ch in enumerate(table):
            shown = ch
            fh.write(f"| {i:3d} | U+{ord(ch):04X} | `{shown}` | `{utf8_hex(ch)}` |\n")

    manifest = {
        "version": 1,
        "symbols": 256,
        "sha256": digest,
        "one_byte": one_byte,
        "two_byte": two_byte,
        "avg_bytes_per_symbol": round(avg, 4),
        "codepoints": [ord(c) for c in table],
    }
    (ROOT / "shared" / "resources" / "table_v1.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(f"OK: 256 símbolos, sha256={digest}, 1B={one_byte}, 2B={two_byte}, media={avg:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
