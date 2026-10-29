"""
Prueba REAL de la stanza de video entre DOS cuentas, SIN subir video.

Envia un `<message>` con la extension `<video xmlns='video:n' .../>` apuntando
a una URL falsa. No se sube ni se descarga ningun archivo: solo viaja la
metadata (~1 KB), que en toDus no consume datos de video.

    python test_video_2cuentas.py <origen> <destino>

`origen`/`destino` son el `username` (53 + telefono, sin '+').
"""
from __future__ import annotations

import sys
import time
import xml.etree.ElementTree as ET

from todus_api import Environment, TodusApi
from xmpp import todus as T
from xmpp.client import TodusXMPPClient, parse_message

DOMAIN = "im.todus.cu"
URL_FALSA = "https://s3.todus.cu/demo/video-falso-NO-EXISTE.mp4"
THUMB_FALSA = "https://s3.todus.cu/demo/thumb-falso-NO-EXISTE.jpg"


def _local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1] if isinstance(tag, str) else ""


def _token(usuario: str) -> str:
    return TodusApi(Environment.PROD, timeout=30).login(usuario, "")


def main() -> int:
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    origen, destino = sys.argv[1], sys.argv[2]

    print(f"[1] token origen {origen}...")
    tok_origen = _token(origen)
    print(f"    OK ({len(tok_origen)} chars)")
    print(f"[1] token destino {destino}...")
    tok_destino = _token(destino)
    print(f"    OK ({len(tok_destino)} chars)")

    receptor = TodusXMPPClient(
        DOMAIN, destino, tok_destino, resource="todus-dest", timeout=30
    )
    jid_receptor = receptor.connect()
    print(f"[2] destino conectado: {jid_receptor}")

    emisor = TodusXMPPClient(
        DOMAIN, origen, tok_origen, resource="todus-orig", timeout=30
    )
    jid_emisor = emisor.connect()
    print(f"[3] origen conectado:  {jid_emisor}")

    for cliente in (receptor, emisor):
        cliente.conn.send("<presence><show>chat</show><priority>1</priority></presence>")
    time.sleep(2)
    print("[4] presencia enviada por ambos")

    texto_ok = False
    video_ok = False
    limite = time.time() + 35

    mid_texto = emisor.send_text(
        T.user_jid(destino), "control-video-test (ignorar)"
    )
    print(f"[5] texto de control enviado: id={mid_texto}")

    ext = T.ext_video(
        id="vid-test-1", message_id="vid-test-1", url=URL_FALSA,
        name="video-falso.mp4", size=123456, duration=10, width=1280,
        height=720, hash="deadbeef", thumbnail=THUMB_FALSA,
    )
    mid_video = emisor.conn.send_message(
        T.user_jid(destino), "", extensions=ext, msg_type="chat"
    )
    print(f"[6] stanza de video enviada: id={mid_video}")
    print(f"    {ext}")

    print("[7] escuchando en destino (35 s)...")
    while time.time() < limite and not (texto_ok and video_ok):
        for stanza in receptor.conn.stream():
            if _local(stanza.tag) != "message":
                continue
            msg = parse_message(stanza)
            print(f"    <- {msg.from_jid} | texto={msg.text!r} | ext={list(msg.extensions)}")
            if msg.text.startswith("control-video-test"):
                texto_ok = True
                print("       >>> TEXTO de control recibido")
            if "video" in msg.extensions:
                video_ok = True
                print(f"       >>> VIDEO recibido: {msg.extensions['video']}")
            sid = stanza.get("id", "")
            remitente = stanza.get("from", "")
            if sid and remitente:
                try:
                    receptor.send_tdack(msg_id=sid, origin=remitente)
                except Exception as exc:
                    print(f"       (tdack fallo: {exc})")
        time.sleep(0.1)

    receptor.close()
    emisor.close()

    print("\n===== RESULTADO =====")
    print(f"texto de control : {'OK' if texto_ok else 'NO'}")
    print(f"stanza de video  : {'OK (metadata recibida)' if video_ok else 'NO'}")
    return 0 if (texto_ok and video_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
