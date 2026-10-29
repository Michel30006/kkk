"""
Sesion completa de mensajeria contra el servidor real de ToDus.

Obtiene el token con la API REST y lo usa como credencial SASL PLAIN en
`im.todus.cu`, hace bind y lista la roster (contactos).

    python test_real_xmpp.py <usuario>          # usuario = 53 + telefono
"""
import sys

from todus_api import Environment, TodusApi
from xmpp.client import TodusXMPPClient

DOMAIN = "im.todus.cu"


def main() -> int:
    usuario = sys.argv[1]
    api = TodusApi(Environment.PROD, timeout=30)
    token = api.login(usuario, "sin-importa")
    print(f"[OK]    token de {len(token)} caracteres")

    cliente = TodusXMPPClient(DOMAIN, usuario, token, timeout=30)
    jid = cliente.connect()
    print(f"[OK]    sesion XMPP iniciada: JID = {jid}")

    try:
        roster = cliente.roster(limit=10)
        items = [c for c in roster if c.tag.rsplit("}", 1)[-1] == "item"]
        print(f"[OK]    roster: {len(items)} contactos")
        for c in items[:10]:
            print(f"          {c.attrib}")
    except Exception as exc:
        print(f"[AVISO] roster: {type(exc).__name__}: {exc}")

    cliente.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
