"""
Mensajeria de extremo a extremo contra el servidor real de ToDus.

Envia un texto y se queda escuchando los mensajes entrantes.

    python test_mensaje.py <53+telefono> [destinatario] [segundos]

Si no se pasa destinatario, envia a la propia cuenta (JID propio). El
destinatario puede ser:
  * un JID completo  -> "5358799457@im.todus.cu"
  * un toDusId/alias -> "michel30"  (se convierte con user_jid())

Para la prueba completa: mientras este script escucha, mandale un mensaje a
la cuenta desde la app del telefono (o pide a alguien que lo haga) y se
imprimira aqui.

    python test_mensaje.py 5358799457 60
"""
from __future__ import annotations

import sys
import time
import xml.etree.ElementTree as ET

from todus_api import Environment, TodusApi
from xmpp import todus as T
from xmpp.client import TodusXMPPClient

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
    destino = sys.argv[2] if len(sys.argv) > 2 else usuario
    segundos = int(sys.argv[3]) if len(sys.argv) > 3 else 30

    api = TodusApi(Environment.PROD, timeout=30)
    token = api.login(usuario, "")
    print(f"[1] token obtenido ({len(token)} chars)")

    cliente = TodusXMPPClient(DOMAIN, usuario, token, timeout=30)
    jid = cliente.connect()
    print(f"[2] sesion XMPP: {jid}")

    # destinatario: JID completo o alias
    if "@" in destino:
        jid_destino = destino
    else:
        jid_destino = T.user_jid(destino)
    print(f"[3] destinatario: {jid_destino}")

    texto = sys.argv[4] if len(sys.argv) > 4 else "hola desde el cliente reconstruido"
    ids = cliente.send_text(jid_destino, texto)
    print(f"[4] texto enviado: id={ids} cuerpo={texto!r}")

    print(f"[5] escuchando {segundos}s (manda un mensaje a esta cuenta desde la app):")
    limite = time.time() + segundos
    recibidos = 0
    while time.time() < limite:
        for stanza in cliente.conn.stream():
            tipo = _local(stanza.tag)
            if tipo == "message":
                recibidos += 1
                print(f"    --- mensaje #{recibidos} ---")
                print(xml_legible(stanza))
                # acuse de recibo propio de ToDus
                mid = stanza.get("id", "")
                origen = stanza.get("from", "")
                if mid and origen:
                    try:
                        cliente.send_tdack(msg_id=mid, origin=origen)
                        print(f"    (tdack enviado para {mid})")
                    except Exception as exc:
                        print(f"    (tdack fallo: {type(exc).__name__}: {exc})")
            elif tipo == "iq":
                print(f"    --- iq ---")
                print(xml_legible(stanza))
            else:
                print(f"    --- {tipo} ---")
                print(xml_legible(stanza))
        time.sleep(0.2)

    cliente.close()
    print(f"[6] fin: {recibidos} mensajes entrantes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
