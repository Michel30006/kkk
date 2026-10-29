"""
Cliente XMPP de ToDus: une la conexión XMPP con la capa de protocolo ToDus.

Uso mínimo:

    from xmpp.client import TodusXMPPClient

    c = TodusXMPPClient("im.todus.cu", "usuario", "clave")
    c.connect()
    c.send_text("destinatario", "hola")
    for msg in c.incoming():
        print(msg.from_jid, msg.text, msg.extensions)
"""
from __future__ import annotations

import time
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from typing import Iterator

from . import todus as T
from .stream import NS_CLIENT, XMPPConnection, XMPPError


@dataclass
class IncomingMessage:
    """Mensaje recibido, ya normalizado."""

    from_jid: str
    to_jid: str
    stanza_id: str
    msg_type: str
    body: str = ""
    extensions: dict[str, dict[str, str]] = field(default_factory=dict)
    raw: ET.Element | None = None

    @property
    def text(self) -> str:
        return self.body


def _localname(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _namespace(tag: str) -> str:
    return tag[1:].split("}", 1)[0] if tag.startswith("{") else ""


def parse_message(stanza: ET.Element) -> IncomingMessage:
    """Normaliza un `<message>` de ToDus."""
    msg = IncomingMessage(
        from_jid=stanza.get("from", ""),
        to_jid=stanza.get("to", ""),
        stanza_id=stanza.get("id", ""),
        msg_type=stanza.get("type", "chat"),
        raw=stanza,
    )
    body = stanza.find("{jabber:client}body")
    if body is None:
        body = stanza.find("body")
    if body is not None and body.text:
        msg.body = body.text
    for child in stanza:
        ns = _namespace(child.tag)
        if not ns:
            continue
        #Extensions de ToDus: namespace propio -> tipo
        kind = None
        for name, value in T.NS.items():
            if value == ns:
                kind = name
                break
        if kind is None:
            continue
        msg.extensions.setdefault(kind, {}).update(child.attrib)
    return msg


class TodusXMPPClient:
    """Cliente XMPP de ToDus (mensajería)."""

    def __init__(
        self,
        domain: str = T.DOMAIN,
        username: str = "",
        password: str = "",
        *,
        port: int = 5222,
        resource: str = "todus",
        tls_mode: str = "direct",
        verify_tls: bool = True,
        timeout: float = 30.0,
    ) -> None:
        self.domain = domain
        self.username = username
        self.password = password
        self.resource = resource
        self.conn = XMPPConnection(
            domain, port, domain=domain, tls_mode=tls_mode,
            timeout=timeout, verify_tls=verify_tls,
        )
        self._pending_iqs: dict[str, ET.Element] = {}
        self._last_from: str = ""

    # -- ciclo de vida ----------------------------------------------------
    def connect(self) -> str:
        """Abre el stream, negocia TLS, autentica (SASL PLAIN) y hace bind."""
        self.conn.connect()
        features = self.conn._features()
        if self.conn.tls_mode == "starttls":
            if features is not None and any(
                c.tag.endswith("starttls") for c in features
            ):
                self.conn.starttls()
                # tras STARTTLS el stream se reinicia: hay que reabrirlo
                features = self.conn._features()
            else:
                # el servidor no ofrece STARTTLS: se reintenta con TLS directo
                self.conn.close()
                self.conn.tls_mode = "direct"
                self.conn.connect()
                features = self.conn._features()
        self.conn.authenticate(self.username, self.password)
        jid = self.conn.bind(self.resource)
        self.conn.start_session()
        return jid

    def close(self) -> None:
        self.conn.close()

    def __enter__(self) -> "TodusXMPPClient":
        self.connect()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # -- recepción --------------------------------------------------------
    def incoming(self, timeout: float = 1.0) -> Iterator[IncomingMessage]:
        """Generador de mensajes entrantes."""
        while True:
            for stanza in self.conn.stream():
                if _localname(stanza.tag) == "message":
                    msg = parse_message(stanza)
                    if msg.from_jid:
                        self._last_from = msg.from_jid
                    yield msg
                elif _localname(stanza.tag) == "iq":
                    self._pending_iqs[stanza.get("id", "")] = stanza
            time.sleep(0.05)

    def wait_iq(self, iq_id: str, timeout: float = 10.0) -> ET.Element:
        """Espera la respuesta de un IQ por id."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            if iq_id in self._pending_iqs:
                return self._pending_iqs.pop(iq_id)
            for stanza in self.conn.stream():
                if _localname(stanza.tag) == "iq":
                    self._pending_iqs[stanza.get("id", "")] = stanza
                elif _localname(stanza.tag) == "message":
                    pass
            if iq_id in self._pending_iqs:
                return self._pending_iqs.pop(iq_id)
            time.sleep(0.05)
        raise XMPPError(f"no llegó la respuesta del IQ {iq_id}")

    # -- envío ------------------------------------------------------------
    def send_text(self, to: str, text: str, *, msg_type: str = "chat") -> list[str]:
        """
        Envía texto, troceándolo si supera `MAX_MSG_TEXT_SIZE` (8192 bytes).

        Cada trozo va en su propia stanza con la extensión `msg:fragment:n`
        (`msgId`, `index`, `total`), igual que la app.
        """
        trozos = T.split_text(text)
        ids: list[str] = []
        if len(trozos) == 1:
            return [self.conn.send_message(to, text, msg_type=msg_type)]
        msg_id = f"{int(time.time() * 1000)}-{len(trozos)}"
        for index, trozo in enumerate(trozos):
            ext = T.ext_chunk(msg_id=msg_id, index=index, total=len(trozos))
            ids.append(
                self.conn.send_message(
                    to, trozo, extensions=ext, msg_type=msg_type,
                    stanza_id=f"{msg_id}_{index}",
                )
            )
        return ids

    def send_image(
        self, to: str, *, url: str, name: str, size: int, message_id: str | None = None,
        hash: str | None = None, width: int | None = None, height: int | None = None,
        thumbnail: str | None = None, body: str = "",
    ) -> str:
        mid = message_id or f"img-{int(time.time() * 1000)}"
        ext = T.ext_image(
            id=mid, message_id=mid, url=url, name=name, size=size,
            hash=hash, width=width, height=height, thumbnail=thumbnail,
        )
        return self.conn.send_message(to, body, extensions=ext)

    def send_file(
        self, to: str, *, url: str, name: str, size: int, message_id: str | None = None,
        hash: str | None = None, body: str = "",
    ) -> str:
        mid = message_id or f"file-{int(time.time() * 1000)}"
        ext = T.ext_file(id=mid, message_id=mid, url=url, name=name, size=size, hash=hash)
        return self.conn.send_message(to, body, extensions=ext)

    def send_location(
        self, to: str, *, lat: float, lon: float, zoom: float | None = None,
        thumbnails: str | None = None, message_id: str | None = None, body: str = "",
    ) -> str:
        mid = message_id or f"loc-{int(time.time() * 1000)}"
        ext = T.ext_location(
            id=mid, message_id=mid, lat=lat, lon=lon, zoom=zoom, thumbnails=thumbnails
        )
        return self.conn.send_message(to, body, extensions=ext)

    def send_reaction(
        self, to: str, *, message_id: str, content: str, entity: str = "message",
        reaction_id: str | None = None, created_at: int | None = None, body: str = "",
    ) -> str:
        rid = reaction_id or f"react-{int(time.time() * 1000)}"
        ext = T.ext_reaction(
            id=rid, message_id=message_id, entity=entity, content=content,
            created_at=created_at,
        )
        return self.conn.send_message(to, body, extensions=ext)

    def send_mention(
        self, to: str, *, text: str, user_id: str, offset: int, length: int,
    ) -> str:
        """Envía un texto con una mención incrustada (`mention:n`)."""
        ext = T.ext_mention(user_id=user_id, offset=offset, length=length)
        return self.conn.send_message(to, text, extensions=ext)

    def send_marker(self, *, message_id: str, room: str, marker: str) -> str:
        """Confirma la lectura de un mensaje (namespace `status`)."""
        ext = T.marker(message_id=message_id, room=room, marker=marker)
        return self.conn.send_message(T.STATUS_RECEPTOR, "", extensions=ext)

    def send_tdack(self, *, msg_id: str, origin: str) -> str:
        """Acuse de recibo de stream propio de ToDus (`<tdack mi o/>`)."""
        self.conn.send(T.tdack(msg_id=msg_id, origin=origin))

    def send_chat_marker(
        self, *, message_id: str, state: str = "displayed", to: str | None = None
    ) -> str:
        """
        Marca de chat XEP-0333 (`<displayed/>`): el emisor ve el mensaje
        como leido (doble tick).

        `to` es el JID de quien envio el mensaje (si no se indica, se usa el
        `from` del ultimo mensaje recibido).
        """
        if to is None:
            to = self._last_from
        if not to:
            raise XMPPError("no se sabe a quien enviar la marca")
        mid = self.conn.next_id("mark")
        cuerpo = T.chat_marker(message_id, state)
        self.conn.send(
            f'<message xmlns="{NS_CLIENT}" type="chat" to="{to}" id="{mid}">'
            f"{cuerpo}</message>"
        )
        return mid

    # -- IQ ---------------------------------------------------------------
    def iq(self, payload: str, iq_type: str = "get", timeout: float = 10.0) -> ET.Element:
        iq_id = self.conn.send_iq(payload, iq_type=iq_type)
        return self.wait_iq(iq_id, timeout=timeout)

    def roster(self, limit: int = 50, offset: int = 0) -> ET.Element:
        return self.iq(T.iq_roster_list(limit, offset))

    def roster_version(self) -> ET.Element:
        return self.iq(T.iq_roster_version())

    def s3_put_url(
        self, *, du: str, url: str, size: int, room: str, persistent: bool = True,
        server: str = "s3", type_: str = "file",
    ) -> ET.Element:
        """Pide una URL prefirmada de subida al S3 de ToDus."""
        return self.iq(
            T.iq_s3_put_url(
                du=du, url=url, size=size, room=room, persistent=persistent,
                server=server, type_=type_,
            ),
            iq_type="set",
        )

    def s3_get_url(self, *, du: str, url: str) -> ET.Element:
        return self.iq(T.iq_s3_get_url(du=du, url=url), iq_type="set")

    def channel_list(self) -> ET.Element:
        return self.iq(T.iq_channel_list())

    def publish_to_channel(self, *, publ: str, link: str) -> ET.Element:
        return self.iq(T.iq_publish_to_channel(publ=publ, link=link), iq_type="set")

    def turn_credentials(self) -> ET.Element:
        """Credenciales TURN para las llamadas."""
        return self.iq(T.iq_turn_credentials())

    def offline_messages(self) -> ET.Element:
        return self.iq(T.iq_get_offline())
