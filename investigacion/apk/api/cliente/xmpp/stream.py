"""
Núcleo XMPP mínimo (solo biblioteca estándar) para el cliente de ToDus.

Implementa lo justo y necesario del protocolo XMPP que usa la app:
  * stream XML no bien formado (el parser de stanzas de XMPP no es un
    documento XML: hay que trocear los hijos de primer nivel);
  * SASL PLAIN (el mecanismo que declara la app);
  * bind (SASL2 y heredado) + sesión heredada;
  * envío y recepción de stanzas crudas.

No es una biblioteca XMPP general: implementa el subconjunto verificado en la
APK `cu.todus.android` v3.0.2 (ver `investigacion/apk/xmpp/protocolo.md`).
"""
from __future__ import annotations

import base64
import re
import socket
import ssl
import xml.etree.ElementTree as ET
from typing import Iterator

# --- nombres de espacio ---------------------------------------------------
NS_STREAM = "http://etherx.jabber.org/streams"
NS_CLIENT = "jabber:client"
NS_SASL = "urn:ietf:params:xml:ns:xmpp-sasl"
NS_SASL2 = "urn:xmpp:sasl:2"
NS_BIND = "urn:ietf:params:xml:ns:xmpp-bind"
NS_BIND2 = "urn:xmpp:bind:0"
NS_SESSION = "urn:ietf:params:xml:ns:xmpp-session"
NS_TLS = "urn:ietf:params:xml:ns:xmpp-tls"

# XEP-0333 (chat markers): el app los usa para marcar los mensajes como
# leidos (`<displayed/>` / `<received/>`).
NS_CHAT_MARKERS = "urn:xmpp:chat-markers:0"

# El servidor de ToDus (`im.todus.cu`) responde con *namespace abreviados*:
# en la cabecera de su stream declara `xmlns:stream='x1'` y `xmlns='jc'`, y en
# `<stream:features>` usa `xmlns='x2'` para SASL. Los tokens observados se
# traducen a su URI completa antes de seguir procesando la respuesta.
NS_ABBREV = {
    "jc": NS_CLIENT,
    "x1": NS_STREAM,
    "x2": NS_SASL,
    "x4": NS_BIND,
    "x5": NS_SESSION,
    "x8": "urn:xmpp:chat-markers:0",
}

# Elementos abreviados que el mismo servidor emite dentro del namespace SASL
# (`<es>` = `<mechanisms>`, `<e>` = `<mechanism>`) y dentro de los namespaces
# de bind/sesion (`<b1>` = `<bind>`, `<s1>` = `<session>`, `<or>` = `<optional>`).
ELEM_ABBREV = {
    "es": "mechanisms",
    "e": "mechanism",
    "b1": "bind",
    "s1": "session",
    "or": "optional",
    # XEP-0333 (chat markers)
    "k": "markable",
    "rd": "received",
    "dd": "displayed",
}

# Atributos abreviados de las *stanzas* que emite el servidor:
# `<iq t='result' i='bind_1'>` en vez de `type`/`id`.
# Solo se renombran en elementos de nivel superior (message/iq/presence): las
# extensiones de ToDus usan atributos como `i` o `s` con otro significado.
ATTR_ABBREV_STANZA = {
    "i": "id",
    "t": "type",
    "f": "from",
    "o": "to",
}

# Elementos de stanza abreviados: `<m .../>` = `<message>`, `<p .../>` =
# `<presence>`. Se aplican solo al elemento raiz de una stanza.
STANZA_ABBREV = {
    "m": "message",
    "p": "presence",
}

# El cuerpo del mensaje tambien llega abreviado: `<b>texto</b>` = `<body>`.
BODY_ABBREV = {"b": "body"}

STANZAS = ("message", "iq", "presence", "error")

_HEADER_RE = re.compile(r"<stream:stream\b[^>]*>", re.I | re.S)
_ATTR_RE = re.compile(r"""([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)')""")
_XMLNS_RE = re.compile(r"""xmlns(?::([\w.-]+))?\s*=\s*(?:"([^"]*)"|'([^']*)')""")


class XMPPError(RuntimeError):
    """Error de protocolo XMPP."""


def _unescape(text: str) -> str:
    return (
        text.replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", '"')
        .replace("&apos;", "'")
        .replace("&amp;", "&")
    )


def _escape(text: str) -> str:
    return (
        str(text)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def parse_attrs(fragment: str) -> dict[str, str]:
    """Atributos de una etiqueta (con o sin prefijo de espacio de nombres)."""
    out: dict[str, str] = {}
    for name, dq, sq in _ATTR_RE.findall(fragment):
        out[name] = _unescape(dq or sq)
    return out


def split_tag(fragment: str) -> tuple[str, bool]:
    """Devuelve (nombre de la etiqueta, ¿es de cierre?)."""
    body = fragment[1:].rstrip(">").strip()
    closing = body.startswith("/")
    if closing:
        body = body[1:]
    name = body.split()[0] if body.split() else ""
    return name, closing


def _local(tag) -> str:
    """Nombre local de una etiqueta (sin namespace)."""
    if not isinstance(tag, str):
        return ""
    return tag.rsplit("}", 1)[-1]


def _local_of(element) -> str:
    """Nombre local de un elemento ET."""
    return _local(getattr(element, "tag", element))


class StreamParser:
    """
    Trocea un stream XMPP en sus hijos de primer nivel.

    El contenido de un stream XMPP no es XML bien formado (varios raíces,
    prefijos declarados en la cabecera), así que no se puede usar un parser
    XML directo: aquí se acumula el XML crudo de cada elemento de nivel
    superior y luego se parsea por separado, inyectando las declaraciones
    `xmlns` de la cabecera.
    """

    def __init__(self) -> None:
        self.buffer = ""
        self.ns_decls: dict[str, str] = {}
        self.header: dict[str, str] = {}
        self.in_header = True
        self.depth = 0
        self.current: list[str] = []
        self.pending: list[ET.Element] = []

    # -- interno ----------------------------------------------------------
    def _parse_element(self, raw: str) -> ET.Element | None:
        raw = raw.strip()
        if not raw:
            return None
        # inyecta los xmlns declarados en la cabecera del stream
        if self.ns_decls:
            m = re.match(r"<([\w:.-]+)", raw)
            if m and "xmlns" not in raw[: raw.find(">")]:
                decls = " ".join(
                    f'xmlns:{p}="{u}"' if p else f'xmlns="{u}"'
                    for p, u in self.ns_decls.items()
                )
                raw = raw.replace(f"<{m.group(1)}", f"<{m.group(1)} {decls}", 1)
        try:
            el = ET.fromstring(raw)
        except ET.ParseError:
            return None
        # stanzas abreviadas: `<m/>` = `<message>`, `<p/>` = `<presence>`
        local = _local(el.tag)
        if local in STANZA_ABBREV:
            el.tag = f"{{{NS_CLIENT}}}{STANZA_ABBREV[local]}"
        self._expand_abbreviations(el)
        return el

    @staticmethod
    def _expand_abbreviations(el: ET.Element) -> None:
        """Traduce namespaces, elementos y atributos abreviados del servidor."""
        for node in el.iter():
            tag = node.tag
            if not isinstance(tag, str):
                continue
            if tag.startswith("{"):
                uri, _, local = tag[1:].partition("}")
                uri = NS_ABBREV.get(uri, uri)
                node.tag = f"{{{uri}}}{local}"
                if uri in (NS_SASL, NS_BIND, NS_SESSION) and local in ELEM_ABBREV:
                    node.tag = f"{{{uri}}}{ELEM_ABBREV[local]}"
                elif uri == NS_CHAT_MARKERS and local in ELEM_ABBREV:
                    node.tag = f"{{{uri}}}{ELEM_ABBREV[local]}"
            if _local(node.tag) in STANZAS:
                for corto, largo in ATTR_ABBREV_STANZA.items():
                    if corto in node.attrib and largo not in node.attrib:
                        node.attrib[largo] = node.attrib.pop(corto)
            # `<b>texto</b>` = `<body>` (namespace del cliente)
            if (
                _local(node.tag) in BODY_ABBREV
                and isinstance(node.tag, str)
                and node.tag.startswith(f"{{{NS_CLIENT}}}")
            ):
                node.tag = f"{{{NS_CLIENT}}}{BODY_ABBREV[_local(node.tag)]}"

    def _handle_header(self, tag: str) -> bool:
        attrs = parse_attrs(tag)
        if tag.lower().startswith("<stream:stream"):
            self.header = attrs
            self.ns_decls = {
                (p or ""): (u)
                for p, u in (
                    (m.group(1), m.group(2) if m.group(2) is not None else m.group(3))
                    for m in _XMLNS_RE.finditer(tag)
                )
            }
            self.in_header = False
            return True
        return False

    # -- API --------------------------------------------------------------
    def feed(self, data: str) -> list[ET.Element]:
        """Alimenta el parser y devuelve las stanzas completas detectadas."""
        self.buffer += data
        out: list[ET.Element] = []
        while True:
            if self.in_header:
                m = _HEADER_RE.search(self.buffer)
                if not m:
                    break
                self.buffer = self.buffer[m.end():]
                self._handle_header(m.group(0))
                continue
            # El servidor reinicia el stream (tras SASL o STARTTLS): llega una
            # cabecera nueva en mitad del flujo y hay que reiniciar el estado.
            m = _HEADER_RE.search(self.buffer)
            if m:
                self.buffer = self.buffer[m.start():]
                self.depth = 0
                self.current = []
                self.in_header = True
                continue
            idx = self.buffer.find("<")
            if idx < 0:
                # texto suelto: solo importa dentro de un elemento abierto
                if self.depth > 0:
                    self.current.append(self.buffer)
                self.buffer = ""
                break
            if idx > 0:
                texto = self.buffer[:idx]
                if self.depth > 0:
                    self.current.append(texto)
                self.buffer = self.buffer[idx:]
            # el buffer empieza por '<'
            if self.buffer.startswith("<?") or self.buffer.startswith("<!"):
                end = self.buffer.find(">")
                if end < 0:
                    break
                self.buffer = self.buffer[end + 1:]
                continue
            end = self.buffer.find(">")
            if end < 0:
                break
            tag = self.buffer[: end + 1]
            self.buffer = self.buffer[end + 1:]
            name, closing = split_tag(tag)
            if self.depth == 0:
                # nuevo elemento de nivel superior
                self.current = [tag]
                if tag.rstrip().endswith("/>") or closing:
                    raw = "".join(self.current)
                    self.current = []
                    el = self._parse_element(raw)
                    if el is not None:
                        out.append(el)
                    continue
                self.depth = 1
                continue
            # dentro de un elemento
            self.current.append(tag)
            if closing:
                self.depth -= 1
            elif not tag.rstrip().endswith("/>"):
                # un elemento auto-cerrado (`<x/>`) no anida nada
                self.depth += 1
            if self.depth == 0:
                raw = "".join(self.current)
                self.current = []
                el = self._parse_element(raw)
                if el is not None:
                    out.append(el)
        return out


class XMPPConnection:
    """Conexión XMPP con SASL PLAIN, sin dependencias externas."""

    def __init__(
        self,
        host: str,
        port: int = 5222,
        *,
        domain: str | None = None,
        tls_mode: str = "direct",  # "direct" | "starttls" | "none"
        timeout: float = 30.0,
        verify_tls: bool = True,
    ) -> None:
        self.host = host
        self.port = port
        self.domain = domain or host
        self.tls_mode = tls_mode
        self.timeout = timeout
        self.verify_tls = verify_tls
        self.sock: socket.socket | None = None
        self.parser = StreamParser()
        self.jid: str | None = None
        self.bound = False
        self._next_id = 0
        self._xml_decl_sent = False

    # -- conexión ---------------------------------------------------------
    def connect(self) -> None:
        raw = socket.create_connection((self.host, self.port), timeout=self.timeout)
        if self.tls_mode == "direct":
            raw = self._wrap_tls(raw)
        self.sock = raw

    def _wrap_tls(self, raw: socket.socket) -> socket.socket:
        ctx = ssl.create_default_context()
        if not self.verify_tls:
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
        return ctx.wrap_socket(raw, server_hostname=self.host)

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    # -- E/S --------------------------------------------------------------
    def send(self, xml: str) -> None:
        if self.sock is None:
            raise XMPPError("no hay conexión")
        self.sock.sendall(xml.encode("utf-8"))

    def _read(self, n: int = 4096) -> str:
        assert self.sock is not None
        data = self.sock.recv(n)
        if not data:
            raise XMPPError("el servidor cerró la conexión")
        return data.decode("utf-8", "replace")

    def read_stanzas(self, timeout: float | None = None) -> list[ET.Element]:
        """Lee hasta que haya al menos una stanza completa o venza el tiempo."""
        assert self.sock is not None
        if timeout is not None:
            self.sock.settimeout(timeout)
        stanzas: list[ET.Element] = []
        while True:
            stanzas.extend(self.parser.feed(self._read()))
            if stanzas:
                return stanzas
            if timeout is not None:
                try:
                    self.sock.settimeout(timeout)
                except OSError:
                    pass

    def stream(self) -> list[ET.Element]:
        """Devuelve las stanzas pendientes sin bloquear (no bloqueante)."""
        assert self.sock is not None
        self.sock.settimeout(0.2)
        try:
            data = self.sock.recv(65536)
            if not data:
                raise XMPPError("el servidor cerró la conexión")
            return self.parser.feed(data.decode("utf-8", "replace"))
        except socket.timeout:
            return []
        except BlockingIOError:
            return []

    # --.handshake --------------------------------------------------------
    def open_stream(self) -> list[ET.Element]:
        """
        Abre (o reabre) el stream XML.

        La declaración `<?xml version='1.0'?>` solo puede ir al principio de la
        entidad: al reiniciar el stream (tras SASL o STARTTLS) el servidor la
        rechaza con `not-well-formed`, asi que solo se envia la primera vez.
        """
        cabecera = (
            "<?xml version='1.0'?>" if not self._xml_decl_sent else ""
        ) + (
            "<stream:stream to='%s' xmlns='%s' "
            "xmlns:stream='%s' version='1.0'>"
            % (_escape(self.domain), NS_CLIENT, NS_STREAM)
        )
        self._xml_decl_sent = True
        self.send(cabecera)
        stanzas = self._wait_for(lambda s: s.tag.endswith("features"))
        return stanzas

    def _wait_for(self, predicate, timeout: float | None = None) -> list[ET.Element]:
        collected: list[ET.Element] = []
        deadline_steps = 0
        while True:
            for st in self.read_stanzas(timeout=timeout):
                collected.append(st)
                if predicate(st):
                    return collected
            deadline_steps += 1
            if deadline_steps > 50:
                raise XMPPError("tiempo agotado esperando la respuesta del servidor")

    def starttls(self) -> None:
        self.send(f'<starttls xmlns="{NS_TLS}"/>')
        stanzas = self._wait_for(lambda s: s.tag.endswith("proceed") or s.tag.endswith("failure"))
        if not any(s.tag.endswith("proceed") for s in stanzas):
            raise XMPPError("el servidor rechazó STARTTLS")
        assert self.sock is not None
        self.sock = self._wrap_tls(self.sock)
        self.parser = StreamParser()

    def authenticate(self, username: str, password: str) -> None:
        """
        SASL PLAIN con `authcid = username` y `password` = clave o token.

        El servidor de ToDus (`im.todus.cu`) acepta el **JWT de la API como
        contraseña** (`<auth mechanism='PLAIN'>` con `authcid` = usuario y
        `passwd` = token): es la via que usa la app para la sesion de
        mensajeria. Con la contrasena del usuario responde
        `not-authorized: Invalid username or password`.

        El exito SASL llega como `<success/>` en XMPP estandar, pero este
        servidor lo abrevia a `<ok/>`.
        """
        raw = b"\x00" + username.encode() + b"\x00" + password.encode()
        payload = base64.b64encode(raw).decode()
        self.send(
            f'<auth xmlns="{NS_SASL}" mechanism="PLAIN">{payload}</auth>'
        )
        stanzas = self._wait_for(
            lambda s: _local_of(s) in ("success", "ok") or _local_of(s) == "failure"
        )
        if not any(_local_of(s) in ("success", "ok") for s in stanzas):
            raise XMPPError("fallo de autenticación SASL PLAIN")

    def bind(self, resource: str = "todus") -> str:
        """
        Bind de recurso.

        El servidor decide el modo: si anuncia el bind de SASL2
        (`urn:xmpp:bind:0`) se usa `<bind/>` directo; si anuncia el bind
        clasico (`urn:ietf:params:xml:ns:xmpp-bind`) se usa el `<iq>` heredado.
        `im.todus.cu` (ejabberd) anuncia solo el clasico: mandar el `<bind/>`
        de SASL2 ahi no produce ninguna respuesta.
        """
        features = self._features()
        if features is not None and self._ofrece_bind_sasl2(features):
            self.send(
                f'<bind xmlns="{NS_BIND2}"><resource>{_escape(resource)}</resource></bind>'
            )
            stanzas = self._wait_for(
                lambda s: _local_of(s) in ("bound", "failure")
            )
            bound = next(
                (s for s in stanzas if _local_of(s) == "bound"), None
            )
            if bound is not None:
                self.jid = bound.get("jid")
                self.bound = True
                return self.jid or ""
        # bind heredado: IQ
        self._next_id += 1
        iq_id = f"bind_{self._next_id}"
        self.send(
            f'<iq xmlns="{NS_CLIENT}" type="set" id="{iq_id}">'
            f'<bind xmlns="{NS_BIND}"><resource>{_escape(resource)}</resource></bind></iq>'
        )
        stanzas = self._wait_for(
            lambda s: _local_of(s) == "iq" and s.get("id") == iq_id
        )
        iq = next(s for s in stanzas if _local_of(s) == "iq")
        if iq.get("type") == "error":
            raise XMPPError("el servidor rechazó el bind")
        bound = iq.find(f"{{{NS_BIND}}}bind")
        if bound is None:
            bound = next(
                (e for e in iq.iter() if _local_of(e) == "bind"), None
            )
        # el JID viene como elemento hijo `<jid>...</jid>`, no como atributo
        jid_el = None
        if bound is not None:
            jid_el = bound.find(f"{{{NS_BIND}}}jid")
            if jid_el is None:
                jid_el = next(
                    (e for e in bound.iter() if _local_of(e) == "jid"), None
                )
        self.jid = (jid_el.text or "") if jid_el is not None else None
        self.bound = True
        return self.jid or ""

    def _features(self) -> ET.Element | None:
        stanzas = self.open_stream()
        return next((s for s in stanzas if s.tag.endswith("features")), None)

    @staticmethod
    def _ofrece_bind_sasl2(features: ET.Element) -> bool:
        """¿El servidor anuncia el bind de SASL2 (`urn:xmpp:bind:0`)?"""
        for c in features:
            if not isinstance(c.tag, str):
                continue
            if c.tag == f"{{{NS_BIND2}}}bind" or c.tag == f"{{{NS_BIND2}}}b1":
                return True
        return False

    @staticmethod
    def _has_mechanism(features: ET.Element, mechanism: str, ns: str) -> bool:
        """
        Busca un mecanismo SASL en `<features>`.

        Se acepta tanto `<mechanism>` (forma estandar) como la forma abreviada
        que emite `im.todus.cu` (`<e>` dentro de `<es>`), en cuyo caso el
        namespace ya viene expandido por el parser.
        """
        for m in features.iter():
            if not isinstance(m.tag, str):
                continue
            local = m.tag.rsplit("}", 1)[-1]
            if local in ("mechanism", "e") and (m.text or "").strip() == mechanism:
                return True
        return False

    def start_session(self) -> None:
        """`<session/>` heredado (XEP-0128); el servidor puede ignorarlo."""
        try:
            self.send(f'<session xmlns="{NS_SESSION}"/>')
        except XMPPError:
            pass

    # -- estrofas ---------------------------------------------------------
    def next_id(self, prefix: str = "rea") -> str:
        self._next_id += 1
        return f"{prefix}_{self._next_id}"

    def send_message(
        self,
        to: str,
        body: str | None = None,
        *,
        stanza_id: str | None = None,
        extensions: str = "",
        msg_type: str = "chat",
    ) -> str:
        """
        Envia una `<message>`.

        El texto va SIEMPRE dentro de `<body>`: enviado como contenido suelto
        de la stanza, el servidor de ToDus lo acepta (responde `tdack`) pero
        **no lo entrega** al destinatario (verificado en bucle de dos
        sesiones).
        """
        mid = stanza_id or self.next_id("msg")
        partes = [
            f'<message xmlns="{NS_CLIENT}" type="{msg_type}" to="{_escape(to)}" id="{mid}"'
        ]
        cuerpo = f"<body>{_escape(body or '')}</body>"
        if extensions:
            partes.append(f">{extensions}{cuerpo}</message>")
        else:
            partes.append(f">{cuerpo}</message>")
        self.send("".join(partes))
        return mid

    def send_iq(self, payload: str, iq_type: str = "get", iq_id: str | None = None) -> str:
        mid = iq_id or self.next_id("iq")
        self.send(f'<iq xmlns="{NS_CLIENT}" type="{iq_type}" id="{mid}">{payload}</iq>')
        return mid

    def iter_stanzas(self, timeout: float = 1.0) -> Iterator[ET.Element]:
        """Generador de stanzas recibidas."""
        while True:
            for st in self.stream():
                yield st
            if not self.bound:
                time.sleep(0.05)


import time  # noqa: E402  (usado al final del módulo)
