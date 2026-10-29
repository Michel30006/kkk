"""
Capa de protocolo ToDus sobre XMPP.

Todo lo que hay aquí está extraído de la APK `cu.todus.android` v3.0.2:

* JID y servicios      -> `cu.todus.android.constants.Service$Config`
* extensiones          -> `cu.todus.android.xmpp.extension.*` (atributos exactos)
* IQ                   -> `cu.todus.android.xmpp.iq.*` (ELEMENT + NAMESPACE)
* límites y troceado   -> `MAX_MSG_TEXT_SIZE` (8192) y `MAX_MESSAGE_LENGTH` (65536)
* acuse de stream      -> `org.jivesoftware.smack.toDusSM` (`<tdack mi o/>`)
"""
from __future__ import annotations

import uuid
from dataclasses import dataclass, field
from typing import Iterable, Sequence

# --- dominios y servicios (Service$Config) --------------------------------
DOMAIN = "im.todus.cu"
CHANNEL_SERVICE = "ch.im.todus.cu"
MUC_SERVICE = "muclight.im.todus.cu"
STATUS_RECEPTOR = "status@status.im.todus.cu"

# --- límites (Service$Config) ---------------------------------------------
MAX_MESSAGE_LENGTH = 0x10000   # 65536
MAX_MSG_TEXT_SIZE = 0x2000      # 8192

# --- extensiones propias: namespace ----------------------------------------
NS = {
    "audio": "audio:n",
    "button": "button:n",
    "call": "tcall:n",
    "chunk": "msg:fragment:n",
    "contact": "contact:n",
    "deleted": "deleted:n",
    "deletedowner": "deletedowner:n",
    "edited": "edited:n",
    "event": "event:n",
    "file": "file:n",
    "forwarded": "resend:n",
    "gif": "gif:n",
    "image": "image:n",
    "linkInfo": "linkInfo:n",
    "location": "location:n",
    "mention": "mention:n",
    "pubdeleted": "pubsdeleted:n",
    "pubedited": "pubedited:n",
    "reaction": "reaction:n",
    "refresht": "refresht:n",
    "statusexistence": "statusexistence:n",
    "sticker": "sticker:n",
    "streamvideo": "streamvideo:n",
    "video": "video:n",
    "voice": "voice:n",
    "status": "status",
    # estandar: marcas de chat (XEP-0333)
    "chatmarker": "urn:xmpp:chat-markers:0",
}


def esc(value) -> str:
    return (
        str(value)
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def user_jid(username: str) -> str:
    """
    JID de usuario: `<username>@im.todus.cu`.

    **El localpart es el `username`, no el `toDusId`** (el alias). Verificado
    en vivo: un mensaje a `5358799457@im.todus.cu` se entrega, y uno a
    `michel30@im.todus.cu` (el `toDusId` de esa misma cuenta) se acepta pero
    **no se entrega nunca**. El `username` es el teléfono con el código de
    país sin `+` (Cuba: `53` + 8 dígitos).
    """
    return f"{username}@{DOMAIN}"


# XEP-0333 (Chat Markers). El servidor lo abrevia como `x8` y sus elementos
# como `k` (markable), `rd` (received) y `dd` (displayed).
NS_CHAT_MARKERS = "urn:xmpp:chat-markers:0"

CHAT_MARKER_STATES = ("received", "displayed", "deleted")


def chat_marker(message_id: str, state: str = "displayed") -> str:
    """
    Marca de chat (XEP-0333): `<displayed xmlns='urn:xmpp:chat-markers:0' id='…'/>`.

    La app usa esta extension para poner el doble tick azul de "leido"; sin
    ella el emisor no ve el mensaje como leido.
    """
    if state not in CHAT_MARKER_STATES:
        raise ValueError(f"estado de marca invalido: {state!r}")
    return f'<{state} xmlns="{NS_CHAT_MARKERS}" id="{esc(message_id)}"/>'


def channel_jid(channel: str) -> str:
    """JID de canal: `<canal>@ch.im.todus.cu`."""
    return f"{channel}@{CHANNEL_SERVICE}"


def group_jid(group: str) -> str:
    """JID de grupo (MUC ligera): `<grupo>@muclight.im.todus.cu`."""
    return f"{group}@{MUC_SERVICE}"


def _attrs(pairs: Sequence[tuple[str, object]]) -> str:
    out = []
    for name, value in pairs:
        if value is None:
            continue
        out.append(f'{name}="{esc(value)}"')
    return (" " + " ".join(out)) if out else ""


# --- extensiones -----------------------------------------------------------
def ext_image(
    *, id: str, message_id: str, url: str, name: str, size: int,
    hash: str | None = None, width: int | None = None, height: int | None = None,
    thumbnail: str | None = None,
) -> str:
    """`<image xmlns='image:n' i mi url n s h w he tnail/>`."""
    return (
        f'<image xmlns="{NS["image"]}"'
        + _attrs([
            ("i", id), ("mi", message_id), ("url", url), ("n", name),
            ("s", size), ("h", hash), ("w", width), ("he", height),
            ("tnail", thumbnail),
        ])
        + "/>"
    )


def ext_file(
    *, id: str, message_id: str, url: str, name: str, size: int,
    hash: str | None = None,
) -> str:
    """`<file xmlns='file:n' i mi url n s h/>`."""
    return (
        f'<file xmlns="{NS["file"]}"'
        + _attrs([
            ("i", id), ("mi", message_id), ("url", url), ("n", name),
            ("s", size), ("h", hash),
        ])
        + "/>"
    )


def ext_audio(
    *, id: str, message_id: str, url: str, name: str, size: int,
    duration: int | None = None, hash: str | None = None,
) -> str:
    return (
        f'<audio xmlns="{NS["audio"]}"'
        + _attrs([
            ("i", id), ("mi", message_id), ("url", url), ("n", name),
            ("s", size), ("d", duration), ("h", hash),
        ])
        + "/>"
    )


def ext_voice(
    *, id: str, message_id: str, url: str, name: str, size: int,
    duration: int | None = None, hash: str | None = None,
    wave_sample: str | None = None,
) -> str:
    return (
        f'<voice xmlns="{NS["voice"]}"'
        + _attrs([
            ("i", id), ("mi", message_id), ("url", url), ("n", name),
            ("s", size), ("d", duration), ("h", hash), ("ws", wave_sample),
        ])
        + "/>"
    )


def ext_video(
    *, id: str, message_id: str, url: str, name: str, size: int,
    duration: int | None = None, width: int | None = None,
    height: int | None = None, hash: str | None = None,
    thumbnail: str | None = None,
) -> str:
    return (
        f'<video xmlns="{NS["video"]}"'
        + _attrs([
            ("i", id), ("mi", message_id), ("url", url), ("n", name),
            ("s", size), ("d", duration), ("w", width), ("he", height),
            ("h", hash), ("tnail", thumbnail),
        ])
        + "/>"
    )


def ext_sticker(
    *, id: str, message_id: str, url: str, name: str, size: int,
    folder: str | None = None, hash: str | None = None, json: str | None = None,
) -> str:
    return (
        f'<sticker xmlns="{NS["sticker"]}"'
        + _attrs([
            ("i", id), ("mi", message_id), ("url", url), ("n", name),
            ("s", size), ("f", folder), ("h", hash), ("j", json),
        ])
        + "/>"
    )


def ext_location(
    *, id: str, message_id: str, lat: float, lon: float,
    zoom: float | None = None, thumbnails: str | None = None,
) -> str:
    return (
        f'<location xmlns="{NS["location"]}"'
        + _attrs([
            ("i", id), ("mi", message_id), ("lat", lat), ("lon", lon),
            ("z", zoom), ("t", thumbnails),
        ])
        + "/>"
    )


def ext_mention(*, user_id: str, offset: int, length: int) -> str:
    """`<mention xmlns='mention:n' o l ui/>` (offset, longitud, userId)."""
    return (
        f'<mention xmlns="{NS["mention"]}"'
        + _attrs([("o", offset), ("l", length), ("ui", user_id)])
        + "/>"
    )


def ext_reaction(
    *, id: str, message_id: str, entity: str, content: str,
    created_at: int | None = None, mirror: str | None = None,
) -> str:
    """`<reaction xmlns='reaction:n' i mi mir rc ca/>`."""
    return (
        f'<reaction xmlns="{NS["reaction"]}"'
        + _attrs([
            ("i", id), ("mi", message_id), ("mir", mirror),
            ("rc", content), ("ca", created_at),
        ])
        + "/>"
    )


def ext_chunk(*, msg_id: str, index: int, total: int) -> str:
    """`<chunk xmlns='msg:fragment:n' msgId index total/>`."""
    return (
        f'<chunk xmlns="{NS["chunk"]}"'
        + _attrs([("msgId", msg_id), ("index", index), ("total", total)])
        + "/>"
    )


def ext_event(
    *, id: str, message_id: str, title: str, start: int, end: int,
    all_day: bool = False, ics: str | None = None,
) -> str:
    return (
        f'<event xmlns="{NS["event"]}"'
        + _attrs([
            ("i", id), ("mi", message_id), ("t", title), ("s", start),
            ("e", end), ("ad", all_day), ("ics", ics),
        ])
        + "/>"
    )


def ext_call(
    *, id: str, message_id: str, state: str, call_id: str | None = None,
) -> str:
    """`<tcall xmlns='tcall:n' i mi st cid/>`."""
    return (
        f'<tcall xmlns="{NS["call"]}"'
        + _attrs([("i", id), ("mi", message_id), ("st", state), ("cid", call_id)])
        + "/>"
    )


def ext_status_existence(*, user_id: str, state: str) -> str:
    """`<statusexistence xmlns='statusexistence:n' .../>`."""
    return (
        f'<statusexistence xmlns="{NS["statusexistence"]}"'
        + _attrs([("ui", user_id), ("st", state)])
        + "/>"
    )


def marker(*, message_id: str, room: str, marker: str) -> str:
    """
    Marcador de lectura (confirmación de recepción).

    El namespace es `status` (`Service$Config.STATUS_NAMESPACE`) y el receptor
    es `status@status.im.todus.cu` (`Service$Config.STATUS_RECEPTOR`).
    """
    return (
        f'<status xmlns="{NS["status"]}"'
        + _attrs([("mi", message_id), ("room", room), ("marker", marker)])
        + "/>"
    )


def tdack(*, msg_id: str, origin: str) -> str:
    """
    Acuse de recibo a nivel de stream propio de ToDus.

    Plugin `org.jivesoftware.smack.toDusSM`: `<tdack mi='...' o='...'/>`.
    """
    return f'<tdack mi="{esc(msg_id)}" o="{esc(origin)}"/>'


# --- troceado --------------------------------------------------------------
def split_text(text: str, limit: int = MAX_MSG_TEXT_SIZE) -> list[str]:
    """
    Trocea el texto en trozos de como máximo `limit` bytes UTF-8.

    La app trocea por el límite de 8192 bytes (`MAX_MSG_TEXT_SIZE`) y marca
    cada trozo con la extensión `msg:fragment:n` (msgId, index, total).
    """
    data = text.encode("utf-8")
    if len(data) <= limit:
        return [text]
    trozos: list[str] = []
    actual = bytearray()
    for ch in text:
        enc = ch.encode("utf-8")
        if len(actual) + len(enc) > limit:
            trozos.append(actual.decode("utf-8"))
            actual = bytearray()
        actual += enc
    if actual:
        trozos.append(actual.decode("utf-8"))
    return trozos


@dataclass
class OutgoingMessage:
    """Mensaje de salida con sus extensiones."""

    to: str
    body: str = ""
    extensions: list[str] = field(default_factory=list)
    msg_type: str = "chat"
    stanza_id: str = field(default_factory=lambda: uuid.uuid4().hex)
    extra: dict[str, str] = field(default_factory=dict)


def build_extensions(parts: Iterable[str]) -> str:
    return "".join(parts)


# --- IQ --------------------------------------------------------------------
def iq_query(namespace: str, element: str = "query", children: str = "") -> str:
    """`<query xmlns='todus:...'>...</query>` — hijo de un `<iq>` de ToDus."""
    return f'<{element} xmlns="{namespace}">{children}</{element}>'


# IQs documentados (ver xmpp/iqs.md)
def iq_roster_list(limit: int, offset: int) -> str:
    return iq_query("todus:roster:list:2", children=f"<limit>{limit}</limit><offset>{offset}</offset>")


def iq_roster_version() -> str:
    return iq_query("todus:roster:version")


def iq_roster_to_dus_id(username: str) -> str:
    return iq_query("todus:roster:todus:id", children=f"<username>{esc(username)}</username>")


def iq_s3_put_url(
    *, du: str, url: str, size: int, room: str, persistent: bool,
    server: str, type_: str,
) -> str:
    return iq_query(
        "todus:purl",
        children=(
            f"<du>{esc(du)}</du><url>{esc(url)}</url><size>{size}</size>"
            f"<room>{esc(room)}</room><persistent>{'true' if persistent else 'false'}</persistent>"
            f"<server>{esc(server)}</server><type>{esc(type_)}</type>"
        ),
    )


def iq_s3_get_url(*, du: str, url: str) -> str:
    return iq_query("todus:gurl", children=f"<du>{esc(du)}</du><url>{esc(url)}</url>")


def iq_publish_to_channel(*, publ: str, link: str) -> str:
    return iq_query(
        "todus:ch:publish", children=f"<publ>{esc(publ)}</publ><link>{esc(link)}</link>"
    )


def iq_channel_list() -> str:
    return iq_query("todus:ch:my_channels")


def iq_channel_list_paginated(limit: int, offset: int) -> str:
    return iq_query(
        "todus:ch:my_channels:2", children=f"<limit>{limit}</limit><offset>{offset}</offset>"
    )


def iq_channel_info_by_id(channel: str) -> str:
    return iq_query("todus:ch:info:id", children=f"<channel>{esc(channel)}</channel>")


def iq_subscribe_to_channel(channel: str) -> str:
    return iq_query("todus:ch:subscribe", children=f"<channel>{esc(channel)}</channel>")


def iq_leave_channel(channel: str) -> str:
    return iq_query("todus:ch:leave", children=f"<channel>{esc(channel)}</channel>")


def iq_create_channel(channel: str, config: str | None = None) -> str:
    children = f"<channel>{esc(channel)}</channel>"
    if config:
        children += f"<config>{esc(config)}</config>"
    return iq_query("todus:ch:create", children=children)


def iq_join_group(group: str) -> str:
    return iq_query("todus:group:join", children=f"<group>{esc(group)}</group>")


def iq_leave_group(group: str) -> str:
    return iq_query("todus:group:leave", children=f"<group>{esc(group)}</group>")


def iq_group_members(group: str) -> str:
    return iq_query("todus:group:members", children=f"<group>{esc(group)}</group>")


def iq_turn_credentials() -> str:
    """Credenciales TURN para las llamadas (`todus:turn:cred`)."""
    return iq_query("todus:turn:cred")


def iq_call_status(*, from_user: str, to_user: str, status: str, content: str) -> str:
    return iq_query(
        "todus:call:status",
        children=(
            f"<fromUser>{esc(from_user)}</fromUser><toUser>{esc(to_user)}</toUser>"
            f"<status>{esc(status)}</status><content>{esc(content)}</content>"
        ),
    )


def iq_block_user(username: str) -> str:
    return iq_query("todus:block:set", children=f"<username>{esc(username)}</username>")


def iq_unblock_user(username: str) -> str:
    return iq_query("todus:block:unset:2", children=f"<username>{esc(username)}</username>")


def iq_get_block_list() -> str:
    return iq_query("todus:block:get:2")


def iq_get_offline() -> str:
    return iq_query("todus:offline:get")


def iq_update_info() -> str:
    return iq_query("todus:version:update")
