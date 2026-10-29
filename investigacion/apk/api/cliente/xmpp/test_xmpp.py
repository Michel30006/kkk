"""
Pruebas del cliente XMPP de ToDus (offline: no contacta ningún servidor).

Cubren:
  * el troceado de texto según MAX_MSG_TEXT_SIZE (8192);
  * el formato exacto de las extensiones (atributos y namespaces);
  * el parser de stream XMPP (que no es XML bien formado);
  * el troceado de mensajes largos con `msg:fragment:n`;
  * el formato de los IQ con `ELEMENT` + `NAMESPACE`.

Ejecutar: python3 test_xmpp.py
"""
from __future__ import annotations

import os
import sys
import xml.etree.ElementTree as ET

# permite ejecutar el fichero directamente: añade `cliente/` al sys.path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from xmpp import todus as T
from xmpp.client import parse_message
from xmpp.stream import StreamParser, parse_attrs


def test_jids():
    assert T.user_jid("abc") == "abc@im.todus.cu"
    assert T.channel_jid("canal") == "canal@ch.im.todus.cu"
    assert T.group_jid("grupo") == "grupo@muclight.im.todus.cu"
    assert T.STATUS_RECEPTOR == "status@status.im.todus.cu"


def test_limites():
    assert T.MAX_MSG_TEXT_SIZE == 8192
    assert T.MAX_MESSAGE_LENGTH == 65536


def test_troceado_simple():
    assert T.split_text("hola") == ["hola"]
    assert T.split_text("a" * 9000) == ["a" * 8192, "a" * 808]


def test_troceado_por_bytes_utf8():
    texto = "á" * 5000          # 2 bytes por carácter en UTF-8
    trozos = T.split_text(texto)
    assert len(trozos) > 1
    for t in trozos:
        assert len(t.encode("utf-8")) <= 8192
    assert "".join(trozos) == texto


def test_extension_imagen_atributos():
    xml = T.ext_image(
        id="1", message_id="m1", url="https://s3.todus.cu/x", name="f.png",
        size=1234, hash="abc", width=100, height=200, thumbnail="https://t/x",
    )
    el = ET.fromstring(xml)
    assert el.tag == "{image:n}image"
    assert el.get("i") == "1"
    assert el.get("mi") == "m1"
    assert el.get("url") == "https://s3.todus.cu/x"
    assert el.get("n") == "f.png"
    assert el.get("s") == "1234"
    assert el.get("h") == "abc"
    assert el.get("w") == "100"
    assert el.get("he") == "200"      # ojo: "he", no "height"
    assert el.get("tnail") == "https://t/x"   # ojo: "tnail", no "thumbnail"


def test_extension_mencion():
    el = ET.fromstring(T.ext_mention(user_id="u1", offset=3, length=5))
    assert el.tag == "{mention:n}mention"
    assert (el.get("o"), el.get("l"), el.get("ui")) == ("3", "5", "u1")


def test_extension_chunk():
    el = ET.fromstring(T.ext_chunk(msg_id="abc", index=2, total=5))
    assert el.tag == "{msg:fragment:n}chunk"
    assert (el.get("msgId"), el.get("index"), el.get("total")) == ("abc", "2", "5")


def test_extension_call():
    el = ET.fromstring(T.ext_call(id="i", message_id="m", state="ringing", call_id="c"))
    assert el.tag == "{tcall:n}tcall"
    assert el.get("st") == "ringing" and el.get("cid") == "c"


def test_tdack():
    el = ET.fromstring(T.tdack(msg_id="m1", origin="io"))
    assert el.tag == "tdack"
    assert el.get("mi") == "m1" and el.get("o") == "io"


def test_marker():
    el = ET.fromstring(T.marker(message_id="m1", room="r1", marker="received"))
    assert el.tag == "{status}status"
    assert el.get("mi") == "m1" and el.get("room") == "r1"


def test_iq_namespace():
    xml = T.iq_roster_list(20, 40)
    el = ET.fromstring(xml)
    assert el.tag == "{todus:roster:list:2}query"
    ns = "{todus:roster:list:2}"
    assert el.find(ns + "limit").text == "20"
    assert el.find(ns + "offset").text == "40"


def test_iq_s3_put_url():
    el = ET.fromstring(
        T.iq_s3_put_url(du="d", url="u", size=10, room="r", persistent=True,
                        server="s3", type_="file")
    )
    assert el.tag == "{todus:purl}query"
    ns = "{todus:purl}"
    assert el.find(ns + "du").text == "d"
    assert el.find(ns + "persistent").text == "true"
    assert el.find(ns + "type").text == "file"


def test_parser_stream():
    """El stream XMPP no es XML bien formado: hay que trocearlo."""
    p = StreamParser()
    data = (
        "<?xml version='1.0'?><stream:stream from='im.todus.cu' id='abc' "
        "xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' "
        "version='1.0'>"
        "<stream:features><mechanism xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>PLAIN</mechanism>"
        "</stream:features>"
        "<message type='chat' from='a@im.todus.cu' id='1'><body>hola</body></message>"
    )
    stanzas = p.feed(data)
    tags = [s.tag for s in stanzas]
    assert any(t.endswith("features") for t in tags), tags
    assert any(t.endswith("message") for t in tags), tags
    msg = next(s for s in stanzas if s.tag.endswith("message"))
    assert msg.find("{jabber:client}body").text == "hola"


def test_parser_stream_por_trozos():
    """La lectura puede llegar troceada: el parser debe acumular."""
    p = StreamParser()
    partes = [
        "<?xml version='1.0'?><stream:stream xmlns='jabber:client' "
        "xmlns:stream='http://etherx.jabber.org/streams'>",
        "<message type='chat' from='a@im.todus.cu' id='1'><bo",
        "dy>hola</body>",
        "</message>",
    ]
    obtenidas = []
    for parte in partes:
        obtenidas.extend(p.feed(parte))
    assert len(obtenidas) == 1
    assert obtenidas[0].find("{jabber:client}body").text == "hola"


def test_parse_message_normalizado():
    xml = (
        "<message xmlns='jabber:client' type='chat' from='a@im.todus.cu' id='9'>"
        "<body>hola</body>"
        "<image xmlns='image:n' i='1' mi='9' url='u' n='f.png' s='10'/>"
        "<reaction xmlns='reaction:n' i='2' mi='9' rc='👍'/>"
        "</message>"
    )
    msg = parse_message(ET.fromstring(xml))
    assert msg.from_jid == "a@im.todus.cu"
    assert msg.text == "hola"
    assert msg.extensions["image"]["url"] == "u"
    assert msg.extensions["reaction"]["rc"] == "👍"


def test_attrs_parse():
    attrs = parse_attrs("<x a='1' b=\"2\" c='3'>")
    assert attrs == {"a": "1", "b": "2", "c": "3"}


class ConexionFalsa:
    """Sustituye a XMPPConnection para comprobar lo que se enviaría."""

    def __init__(self):
        self.enviados = []

    def send(self, xml):
        self.enviados.append(xml)

    def send_message(self, to, body=None, *, extensions="", msg_type="chat",
                     stanza_id=None):
        self.enviados.append((to, body, extensions, msg_type))
        return stanza_id or "id-1"


def test_envio_de_texto_largo_se_trocea():
    from xmpp.client import TodusXMPPClient

    cliente = TodusXMPPClient()
    cliente.conn = ConexionFalsa()
    cliente.send_text("a@im.todus.cu", "x" * 9000)

    enviados = cliente.conn.enviados
    assert len(enviados) == 2, enviados
    for indice, (to, body, ext, _tipo) in enumerate(enviados):
        assert to == "a@im.todus.cu"
        el = ET.fromstring(ext)
        assert el.tag == "{msg:fragment:n}chunk"
        assert el.get("index") == str(indice)
        assert el.get("total") == "2"
        assert el.get("msgId")
    assert "".join(b for _, b, _, _ in enviados) == "x" * 9000


def test_envio_de_texto_corto_no_trocea():
    from xmpp.client import TodusXMPPClient

    cliente = TodusXMPPClient()
    cliente.conn = ConexionFalsa()
    cliente.send_text("a@im.todus.cu", "hola")
    to, body, ext, _ = cliente.conn.enviados[0]
    assert ext == ""
    assert body == "hola"


def test_envio_de_imagen_incluye_extension():
    from xmpp.client import TodusXMPPClient

    cliente = TodusXMPPClient()
    cliente.conn = ConexionFalsa()
    cliente.send_image(
        "a@im.todus.cu", url="https://s3.todus.cu/x", name="f.png", size=10
    )
    _to, _body, ext, _tipo = cliente.conn.enviados[0]
    el = ET.fromstring(ext)
    assert el.tag == "{image:n}image"
    assert el.get("n") == "f.png" and el.get("s") == "10"


# --- compatibilidad con el servidor real (im.todus.cu, ejabberd) -----------


def test_parser_acepta_elementos_autocerrados():
    """Un hijo vacio (`<x/>`) no debe dejar el stream abierto."""
    p = StreamParser()
    p.feed("<stream:stream xmlns:stream='http://etherx.jabber.org/streams'>")
    p.feed("<message xmlns='jabber:client' type='chat'/>")
    elementos = p.feed("<iq xmlns='jabber:client' id='1' type='result'/>")
    tags = [e.tag for e in elementos]
    assert "{jabber:client}iq" in tags, tags
    assert p.depth == 0, p.depth


def test_parser_expande_namespaces_abrevidados():
    """El servidor abrevia `jc`/`x1`/`x2` y los elementos `es`/`e`."""
    p = StreamParser()
    p.feed(
        "<?xml version='1.0'?><stream:stream i='1' v='1.0' "
        "xmlns:stream='x1' f='im.todus.cu' xmlns='jc'>"
    )
    features = p.feed(
        "<stream:features><es xmlns='x2'><e>PLAIN</e><e>X-OAUTH2</e></es>"
        "</stream:features>"
    )
    assert len(features) == 1, features
    ns = "{urn:ietf:params:xml:ns:xmpp-sasl}"
    assert features[0].find(f"{ns}mechanisms") is not None
    assert [m.text for m in features[0].iter(f"{ns}mechanism")] == [
        "PLAIN", "X-OAUTH2",
    ]


def test_parser_reinicia_el_stream_tras_el_login():
    """Tras SASL el servidor manda una cabecera nueva: hay que reiniciar."""
    p = StreamParser()
    p.feed(
        "<?xml version='1.0'?><stream:stream i='1' v='1.0' "
        "xmlns:stream='x1' f='im.todus.cu' xmlns='jc'>"
    )
    p.feed(
        "<stream:features><es xmlns='x2'><e>PLAIN</e></es></stream:features>"
    )
    ok = p.feed("<ok xmlns='x2'/>")
    assert len(ok) == 1 and ok[0].tag.endswith("ok"), ok
    # segunda cabecera (nuevo stream)
    p.feed(
        "<?xml version='1.0'?><stream:stream i='2' v='1.0' "
        "xmlns:stream='x1' f='im.todus.cu' xmlns='jc'>"
    )
    features = p.feed(
        "<stream:features><b1 xmlns='x4'/><s1 xmlns='x5'><or/></s1>"
        "</stream:features>"
    )
    assert len(features) == 1, features
    assert features[0].find("{urn:ietf:params:xml:ns:xmpp-bind}bind") is not None


def test_parser_normaliza_atributos_abreviados_de_stanza():
    """`<iq t='result' i='bind_1'>` -> `type`/`id`."""
    p = StreamParser()
    p.feed("<stream:stream xmlns:stream='http://etherx.jabber.org/streams'>")
    elementos = p.feed(
        "<iq xmlns='jabber:client' t='result' i='bind_1' "
        "o='5358799457@im.todus.cu/todus' from='5358799457@im.todus.cu'/>"
    )
    iq = elementos[0]
    assert iq.get("type") == "result", iq.attrib
    assert iq.get("id") == "bind_1", iq.attrib
    assert iq.get("from") == "5358799457@im.todus.cu", iq.attrib


def test_extension_toDus_no_renombra_atributos():
    """`i` y `s` de las extensiones de ToDus no son `id`/`type`."""
    p = StreamParser()
    p.feed("<stream:stream xmlns:stream='http://etherx.jabber.org/streams'>")
    elementos = p.feed(
        "<message xmlns='jabber:client' type='chat'>"
        "<image xmlns='image:n' i='m1' s='1024'/>"
        "</message>"
    )
    imagen = next(e for e in elementos[0].iter() if e.tag.endswith("image"))
    assert imagen.get("i") == "m1", imagen.attrib
    assert imagen.get("s") == "1024", imagen.attrib
    assert imagen.get("id") is None, imagen.attrib


def test_parser_expande_stanzas_abreviadas():
    """`<m/>` = `<message>`, `<p/>` = `<presence>`, `<b>` = `<body>`."""
    p = StreamParser()
    p.feed("<stream:stream xmlns:stream='http://etherx.jabber.org/streams'>")
    elementos = p.feed(
        "<m xmlns='jabber:client' t='c' i='msg_2' f='b@im.todus.cu' "
        "o='a@im.todus.cu'><b>hola</b></m>"
    )
    mensaje = elementos[0]
    assert mensaje.tag == "{jabber:client}message", mensaje.tag
    assert mensaje.get("type") == "c", mensaje.attrib
    assert mensaje.get("id") == "msg_2", mensaje.attrib
    assert mensaje.get("from") == "b@im.todus.cu", mensaje.attrib
    assert mensaje.get("to") == "a@im.todus.cu", mensaje.attrib
    cuerpo = mensaje.find("{jabber:client}body")
    assert cuerpo is not None and cuerpo.text == "hola", cuerpo


def test_mensaje_recibido_se_normaliza():
    """`parse_message` extrae el texto de un `<m><b>hola</b></m>` real."""
    p = StreamParser()
    p.feed("<stream:stream xmlns:stream='http://etherx.jabber.org/streams'>")
    elementos = p.feed(
        "<m xmlns='jabber:client' t='c' i='msg_2' f='b@im.todus.cu' "
        "o='a@im.todus.cu'><b>hola desde la sesion B</b></m>"
    )
    msg = parse_message(elementos[0])
    assert msg.text == "hola desde la sesion B", msg.text
    assert msg.from_jid == "b@im.todus.cu", msg.from_jid
    assert msg.to_jid == "a@im.todus.cu", msg.to_jid
    assert msg.msg_type == "c", msg.msg_type


def test_texto_siempre_va_dentro_de_body():
    """Como contenido suelto el servidor lo acepta pero no lo entrega."""
    from xmpp.stream import XMPPConnection

    class SocketFalso:
        def __init__(self):
            self.enviado = ""

        def sendall(self, datos):
            self.enviado += datos.decode("utf-8")

    conn = XMPPConnection("ejemplo", 5222)
    conn.sock = SocketFalso()
    conn.send_message("b@im.todus.cu", "hola", stanza_id="m1")
    assert "<body>hola</body>" in conn.sock.enviado, conn.sock.enviado
    assert ">hola</message>" not in conn.sock.enviado, conn.sock.enviado


def test_marca_de_chat_xep0333():
    """`<displayed xmlns='urn:xmpp:chat-markers:0' id='…'/>` (XEP-0333)."""
    xml = T.chat_marker("msg_1", "displayed")
    el = ET.fromstring(xml)
    assert el.tag == "{urn:xmpp:chat-markers:0}displayed", el.tag
    assert el.get("id") == "msg_1", el.attrib
    otro = ET.fromstring(T.chat_marker("m2", "received"))
    assert otro.tag == "{urn:xmpp:chat-markers:0}received", otro.tag
    try:
        T.chat_marker("m3", "inventado")
    except ValueError:
        pass
    else:
        raise AssertionError("deberia rechazar un estado de marca invalido")


def test_parser_expande_marcas_de_chat_abreviadas():
    """`x8` = chat-markers; `k`=markable, `rd`=received, `dd`=displayed."""
    p = StreamParser()
    p.feed("<stream:stream xmlns:stream='http://etherx.jabber.org/streams'>")
    elementos = p.feed(
        "<message xmlns='jabber:client' type='chat'>"
        "<k xmlns='x8'/><rd xmlns='x8' id='m1'/><dd xmlns='x8' id='m1'/>"
        "</message>"
    )
    ns = "{urn:xmpp:chat-markers:0}"
    hijos = [e.tag for e in elementos[0]]
    assert f"{ns}markable" in hijos, hijos
    assert f"{ns}received" in hijos, hijos
    assert f"{ns}displayed" in hijos, hijos


def test_extension_video_atributos():
    """`<video xmlns='video:n' .../>` con los atributos exactos de la APK."""
    xml = T.ext_video(
        id="v1", message_id="m1", url="https://s3.todus.cu/v.mp4",
        name="v.mp4", size=123456, duration=10, width=1920, height=1080,
        hash="abc", thumbnail="https://s3.todus.cu/t.jpg",
    )
    el = ET.fromstring(xml)
    assert el.tag == "{video:n}video", el.tag
    assert el.attrib == {
        "i": "v1", "mi": "m1", "url": "https://s3.todus.cu/v.mp4",
        "n": "v.mp4", "s": "123456", "d": "10", "w": "1920",
        "he": "1080", "h": "abc", "tnail": "https://s3.todus.cu/t.jpg",
    }, el.attrib


def test_envio_de_video_incluye_extension():
    from xmpp.client import TodusXMPPClient

    cliente = TodusXMPPClient()
    cliente.conn = ConexionFalsa()
    cliente.conn.send_message(
        "a@im.todus.cu", "",
        extensions=T.ext_video(
            id="v1", message_id="v1", url="https://s3.todus.cu/v.mp4",
            name="v.mp4", size=10,
        ),
    )
    _to, _body, ext, _tipo = cliente.conn.enviados[0]
    el = ET.fromstring(ext)
    assert el.tag == "{video:n}video", el.tag
    assert el.get("n") == "v.mp4" and el.get("s") == "10", el.attrib


def test_video_recibido_se_normaliza():
    """Un `<message>` entrante con `<video/>` expone `extensions['video']`."""
    xml = (
        "<message xmlns='jabber:client' type='chat' from='b@im.todus.cu' id='9'>"
        "<video xmlns='video:n' i='v1' mi='9' url='https://s3.todus.cu/v.mp4'"
        " n='v.mp4' s='123' d='10' w='1280' he='720' h='abc'"
        " tnail='https://s3.todus.cu/t.jpg'/>"
        "</message>"
    )
    msg = parse_message(ET.fromstring(xml))
    video = msg.extensions["video"]
    assert video["url"] == "https://s3.todus.cu/v.mp4", video
    assert video["he"] == "720", video
    assert video["tnail"] == "https://s3.todus.cu/t.jpg", video


def test_iq_s3_put_url_tipo_video():
    el = ET.fromstring(
        T.iq_s3_put_url(
            du="d", url="v.mp4", size=10, room="r",
            persistent=True, server="s3", type_="video",
        )
    )
    assert el.tag == "{todus:purl}query", el.tag
    assert el.find("{todus:purl}type").text == "video"


if __name__ == "__main__":
    fallos = 0
    for nombre, fn in sorted(globals().items()):
        if nombre.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"  OK  {nombre}")
            except AssertionError as exc:
                fallos += 1
                print(f"  FALLO  {nombre}: {exc}")
    print(f"\n{'todo correcto' if not fallos else str(fallos) + ' fallo(s)'}")
    sys.exit(1 if fallos else 0)
