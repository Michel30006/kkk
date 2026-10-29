"""
Prueba de recepcion sin terceros: dos sesiones XMPP de la MISMA cuenta.

La sesion B envia un texto a la sesion A (mismo usuario, distinto recurso),
la sesion A lo recibe. Asi se cierra el circuito envio -> servidor ->
recepcion sin depender de que alguien escriba desde el telefono.

    python test_mensaje_loopback.py <53+telefono> [texto]
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
    texto = sys.argv[2] if len(sys.argv) > 2 else "hola desde la sesion B"

    api = TodusApi(Environment.PROD, timeout=30)
    token = api.login(usuario, "")
    print(f"[1] token ({len(token)} chars)")

    # sesion A: la que recibe
    a = TodusXMPPClient(DOMAIN, usuario, token, resource="todus-a", timeout=30)
    jid_a = a.connect()
    print(f"[2] sesion A: {jid_a}")

    # sesion B: la que envia
    b = TodusXMPPClient(DOMAIN, usuario, token, resource="todus-b", timeout=30)
    jid_b = b.connect()
    print(f"[3] sesion B: {jid_b}")

    # presencia: el servidor de ToDus solo acepta ciertos valores de <show>
    # ('chat' vale, 'online' se rechaza con bad-request)
    for nombre, c in (("A", a), ("B", b)):
        c.conn.send("<presence><show>chat</show><priority>1</priority></presence>")
    print("[3b] presencia enviada por A y B")
    time.sleep(2)

    # el destinatario es el JID COMPLETO de la sesion A (con recurso)
    destino = jid_a
    ids = b.send_text(destino, texto)
    print(f"[4] B envia a {destino}: id={ids} cuerpo={texto!r}")

    print("[5] A escuchando 30s...")
    limite = time.time() + 30
    recibidos = []
    while time.time() < limite:
        for stanza in a.conn.stream():
            tipo = _local(stanza.tag)
            if tipo == "message":
                msg = parse_message(stanza)          # parser de alto nivel
                print("    --- mensaje recibido ---")
                print(f"    from={msg.from_jid} to={msg.to_jid} tipo={msg.msg_type}")
                print(f"    texto={msg.text!r}")
                print(f"    extensiones={msg.extensions}")
                if msg.text == texto:
                    print("    >>> TEXTO CORRECTO: el cuerpo llego integro")
                recibidos.append(msg)
                mid = stanza.get("id", "")
                origen = stanza.get("from", "")
                if mid and origen:
                    try:
                        a.send_tdack(msg_id=mid, origin=origen)
                        print(f"    (tdack enviado para {mid})")
                    except Exception as exc:
                        print(f"    (tdack fallo: {type(exc).__name__})")
            else:
                print(f"    [{tipo}] {stanza.attrib}")
        time.sleep(0.2)

    # lo que haya llegado a la sesion B (por ejemplo el tdack)
    for stanza in b.conn.stream():
        print(f"    [B] {_local(stanza.tag)}: {stanza.attrib}")

    a.close()
    b.close()

    # si el texto iba troceado, se reensambla por msgId
    trozos: dict[str, dict] = {}
    sueltos = []
    for msg in recibidos:
        chunk = msg.extensions.get("chunk")
        if chunk:
            trozos.setdefault(chunk["msgId"], {})[int(chunk["index"])] = msg.text
        else:
            sueltos.append(msg.text)
    reensamblado = "".join(
        trozos[mid][i] for mid in sorted(trozos) for i in sorted(trozos[mid])
    )
    completa = reensamblado or "".join(sueltos)
    ok = completa == texto
    print(f"[6] fin: {len(recibidos)} mensajes recibidos por A")
    if trozos:
        print(f"    {len(trozos)} fragmento(s) reensamblados -> {len(completa)} chars")
    print(f"    texto íntegro: {'OK' if ok else 'NO COINCIDE'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
