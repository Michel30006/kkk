"""
Escucha mensajes entrantes y devuelve la marca de "leido" (XEP-0333).

    python test_escuchar.py <53+telefono> [segundos] [displayed|received|ninguna]

Ademas del `tdack` propio de ToDus, responde con `<displayed/>` (o
`<received/>`) para que el emisor vea el mensaje como leido.
"""
from __future__ import annotations

import sys
import time
import xml.etree.ElementTree as ET

from todus_api import Environment, TodusApi
from xmpp.client import TodusXMPPClient, parse_message

DOMAIN = "im.todus.cu"


def _local(tag) -> str:
    if not isinstance(tag, str):
        return ""
    return tag.rsplit("}", 1)[-1]


def xml_legible(el: ET.Element, sangria: int = 0) -> str:
    if not isinstance(el.tag, str):
        return ""
    txt = (el.text or "").strip()
    linea = "  " * sangria + f"<{el.tag}> {el.attrib}"
    if txt:
        linea += f" {txt!r}"
    for hijo in el:
        linea += "\n" + xml_legible(hijo, sangria + 1)
    return linea


def main() -> int:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    usuario = sys.argv[1]
    segundos = int(sys.argv[2]) if len(sys.argv) > 2 else 120
    marca = sys.argv[3] if len(sys.argv) > 3 else "displayed"

    api = TodusApi(Environment.PROD, timeout=30)
    token = api.login(usuario, "")
    print(f"[1] token de {usuario} ({len(token)} chars)")

    cliente = TodusXMPPClient(DOMAIN, usuario, token, timeout=30)
    jid = cliente.connect()
    print(f"[2] sesion: {jid}")

    cliente.conn.send("<presence><show>chat</show><priority>1</priority></presence>")
    print(f"[3] presencia enviada; escuchando {segundos}s")
    print(f"    marca de leitura: {marca}")
    print("    (envia un mensaje a esta cuenta desde la app)\n")

    limite = time.time() + segundos
    n = 0
    while time.time() < limite:
        for stanza in cliente.conn.stream():
            tipo = _local(stanza.tag)
            if tipo == "message":
                n += 1
                msg = parse_message(stanza)
                print(f"  ===== mensaje #{n} =====")
                print(f"  from={msg.from_jid}  to={msg.to_jid}  tipo={msg.msg_type}")
                print(f"  texto={msg.text!r}")
                if msg.extensions:
                    print(f"  extensiones={msg.extensions}")
                print(xml_legible(stanza))
                mid = stanza.get("id", "")
                origen = stanza.get("from", "")
                if mid and origen:
                    try:
                        cliente.send_tdack(msg_id=mid, origin=origen)
                        print("  (tdack enviado)")
                    except Exception as exc:
                        print(f"  (tdack fallo: {type(exc).__name__}: {exc})")
                    if marca != "ninguna":
                        try:
                            cliente.send_chat_marker(
                                message_id=mid, state=marca, to=origen
                            )
                            print(f"  (marca '{marca}' enviada para {mid})")
                        except Exception as exc:
                            print(f"  (marca fallo: {type(exc).__name__}: {exc})")
                print(flush=True)
            else:
                print(f"  [{tipo}] {stanza.attrib}")
        time.sleep(0.2)

    cliente.close()
    print(f"\n[4] fin: {n} mensajes entrantes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
