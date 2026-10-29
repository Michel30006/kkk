"""
Mensaje entre DOS cuentas distintas (no un bucle de la misma cuenta).

    python test_mensaje_2cuentas.py <cuenta_origen> <cuenta_destino> [texto]

Las cuentas se identifican por su `username` (telefono con codigo de pais,
sin '+'): "5358799457", "5358548838"...
"""
from __future__ import annotations

import sys
import time
import xml.etree.ElementTree as ET

from todus_api import Environment, TodusApi
from xmpp import todus as T
from xmpp.client import TodusXMPPClient, parse_message

DOMAIN = "im.todus.cu"


def _local(tag) -> str:
    if not isinstance(tag, str):
        return ""
    return tag.rsplit("}", 1)[-1]


def token_de(usuario: str) -> str:
    api = TodusApi(Environment.PROD, timeout=30)
    return api.login(usuario, "")


def main() -> int:
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    origen, destino = sys.argv[1], sys.argv[2]
    texto = sys.argv[3] if len(sys.argv) > 3 else "hola desde la otra cuenta"

    tok_a = token_de(origen)
    tok_b = token_de(destino)
    print(f"[1] tokens: {origen} ({len(tok_a)}) y {destino} ({len(tok_b)})")

    a = TodusXMPPClient(DOMAIN, origen, tok_a, resource="todus-a", timeout=30)
    jid_a = a.connect()
    b = TodusXMPPClient(DOMAIN, destino, tok_b, resource="todus-b", timeout=30)
    jid_b = b.connect()
    print(f"[2] {jid_a}\n[3] {jid_b}")

    for c in (a, b):
        c.conn.send("<presence><show>chat</show><priority>1</priority></presence>")
    time.sleep(2)

    jid_destino = T.user_jid(destino)
    ids = a.send_text(jid_destino, texto)
    print(f"[4] {origen} -> {jid_destino}: id={ids} texto={texto!r}")

    print("[5] escuchando 30s...")
    limite = time.time() + 30
    recibidos = []
    while time.time() < limite:
        for etiqueta, c in (("A", a), ("B", b)):
            for stanza in c.conn.stream():
                tipo = _local(stanza.tag)
                if tipo == "message":
                    msg = parse_message(stanza)
                    print(f"    [{etiqueta}] mensaje de {msg.from_jid} "
                          f"(tipo={msg.msg_type}, id={msg.stanza_id})")
                    print(f"        texto={msg.text!r}")
                    if msg.extensions:
                        print(f"        extensiones={msg.extensions}")
                    if etiqueta == "B":
                        recibidos.append(msg)
                        mid = stanza.get("id", "")
                        if mid:
                            b.send_tdack(msg_id=mid, origin=stanza.get("from", ""))
                            print("        (tdack enviado)")
                else:
                    print(f"    [{etiqueta}] {tipo}: {stanza.attrib}")
        time.sleep(0.2)

    a.close()
    b.close()
    ok = bool(recibidos) and recibidos[0].text == texto
    print(f"[6] {destino} recibio {len(recibidos)} mensaje(s) -> "
          f"{'OK' if ok else 'FALLO'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
