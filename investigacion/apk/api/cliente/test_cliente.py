"""
Pruebas del cliente de la API contra un transporte HTTP simulado.

No contacta ningún servidor: comprueba que las peticiones que genera el
cliente coinciden **exactamente** con lo observado en la APK (verbo, ruta,
cabeceras, content-type y cuerpo protobuf).

Ejecutar:  python3 -m pytest test_cliente.py -q
       o:  python3 test_cliente.py
"""
from __future__ import annotations

import sys
import types

import ApiProto_pb2 as pb
from todus_api import CONTENT_TYPE_PROTOBUF, USER_AGENT, Environment, TodusApi


class RespuestaFalsa:
    def __init__(self, status_code=200, content=b"", text=""):
        self.status_code = status_code
        self.content = content
        self.text = text or content.decode("utf-8", "replace")


class SesionFalsa:
    """Captura las peticiones y devuelve respuestas programadas."""

    def __init__(self, respuestas=None):
        self.peticiones = []
        self.respuestas = respuestas or {}

    def request(self, verb, url, data=None, params=None, headers=None, timeout=None):
        self.peticiones.append(
            {
                "verb": verb, "url": url, "data": data, "params": params,
                "headers": headers or {}, "timeout": timeout,
            }
        )
        return self.respuestas.get(url, RespuestaFalsa())


def test_login_envia_protobuf_y_guarda_bearer():
    cuerpo = pb.ProtoUserAuthTokenResponse(tokenString="TOKEN-XYZ").SerializeToString()
    sesion = SesionFalsa(
        {"https://auth.todus.cu/v2/auth/token": RespuestaFalsa(content=cuerpo)}
    )
    api = TodusApi(Environment.PROD, session=sesion)
    token = api.login("usuario", "clave")

    p = sesion.peticiones[0]
    assert p["verb"] == "POST", p
    assert p["url"] == "https://auth.todus.cu/v2/auth/token", p
    assert p["headers"]["Content-Type"] == CONTENT_TYPE_PROTOBUF, p
    assert p["headers"]["User-Agent"] == USER_AGENT, p
    # "Authorization" NO debe estar en el login (no hay token todavía)
    assert "Authorization" not in p["headers"], p

    enviado = pb.ProtoUserAuthTokenRequest()
    enviado.ParseFromString(p["data"])
    assert enviado.username == "usuario"
    assert enviado.password == "clave"
    assert enviado.version == "2.1.2"
    assert token == "TOKEN-XYZ"
    assert api.is_authenticated


def test_operacion_con_token_usa_authorization():
    """
    El token viaja CRUDO en `Authorization`.

    La APK sugiere el prefijo "Bearer ", pero el servidor real lo rechaza
    (`GET todus.cu/catalog/version` -> `{"err":"invalid token"}`) y solo
    acepta el JWT sin prefijo.
    """
    sesion = SesionFalsa()
    api = TodusApi(Environment.PROD, session=sesion)
    api._token = "T1"
    api.sticker_pack_info("pack1")

    p = sesion.peticiones[0]
    assert p["verb"] == "GET", p
    assert p["url"] == "https://stickers.todus.cu/v1/stickers/info/pack1", p
    assert p["headers"]["Authorization"] == "T1", p


def test_rutas_con_placeholders_se_sustituyen():
    sesion = SesionFalsa()
    api = TodusApi(Environment.PROD, session=sesion)
    api._token = "T1"
    api.search_products_by_tag("todus-1", "celular", 10, 20)
    assert (
        sesion.peticiones[0]["url"]
        == "https://todus.cu/catalog/search/products/tag/todus-1/celular/10/20"
    ), sesion.peticiones[0]


def test_entorno_test_usa_los_hosts_de_prueba():
    sesion = SesionFalsa()
    api = TodusApi(Environment.LOCAL, session=sesion)
    api._token = "T1"
    api.sticker_packs(5, 0)
    assert (
        sesion.peticiones[0]["url"]
        == "https://stickers-test.todus.cu/v1/stickers/packs/5/0"
    ), sesion.peticiones[0]


def test_operaciones_con_url_absoluta_no_se_concatenan():
    """
    Las rutas que ya traen la URL completa (`@Url` de Retrofit) no deben
    pegarse al host base: en la APK son `api.apklis.cu`.
    """
    sesion = SesionFalsa()
    api = TodusApi(Environment.PROD, session=sesion)
    api.last_release()
    assert (
        sesion.peticiones[0]["url"] == "https://api.apklis.cu/v1/application/338/"
    ), sesion.peticiones[0]


def test_servicios_sin_host_propio_usan_el_de_client():
    """`LinkedDeviceService` y `CertifiedInfoService` cuelgan de client."""
    sesion = SesionFalsa()
    api = TodusApi(Environment.PROD, session=sesion)
    api._token = "T1"
    api.linked_devices()
    assert sesion.peticiones[0]["url"] == "https://client.todus.cu/list", (
        sesion.peticiones[0]
    )


def test_verbos_y_rutas_de_cada_operacion():
    """Cobertura de las 33 operaciones documentadas contra la APK."""
    esperado = {
        ("auth", "token"): ("POST", "v2/auth/token"),
        ("auth", "passCheck"): ("POST", "v2/auth/check"),
        ("auth", "register"): ("POST", "v2/auth/users.register"),
        ("auth", "reserve"): ("POST", "v2/auth/users.reserve"),
        ("auth", "linkInfo"): ("POST", "v3/preview"),
        ("client", "lastRelease"): ("GET", "https://api.apklis.cu/v1/application/338/"),
        ("client", "lastBetaFromToDus"): ("GET", "todus/release/bet/latest"),
        ("client", "lastReleaseFromToDus"): ("GET", "todus/release/rel/latest"),
        ("client", "downloadLink"): ("POST", "https://api.apklis.cu/v2/release/get_url/"),
        ("user", "updateProfile"): ("POST", "v2/todus/users.me"),
        ("linkedDevice", "getLinkedDevices"): ("GET", "list"),
        ("linkedDevice", "linkDevice"): ("GET", ""),
        ("linkedDevice", "logoutDevice"): ("POST", "logout"),
        ("report", "reportMessage"): ("POST", "v1/report"),
        ("stickers", "packInfo"): ("GET", "v1/stickers/info/{pack_name}"),
        ("stickers", "packs"): ("GET", "v1/stickers/packs/{limit}/{offset}"),
        ("certified", "getInfo"): ("GET", "card/info/{uid}"),
        ("certified", "getInfoOrError"): ("GET", "info/{uid}"),
        ("certified", "subscribe"): ("GET", "card/subscribe/{uid}"),
        ("certified", "unSubscribe"): ("GET", "card/unsubscribe/{uid}"),
        ("catalogue", "version"): ("GET", "version"),
        ("catalogue", "myShop"): ("GET", "shop/{limit}/{offset}"),
        ("catalogue", "shop"): ("GET", "{todus_id}/{limit}/{offset}"),
        ("catalogue", "getProduct"): ("GET", "product/{todus_id}/{id}"),
        ("catalogue", "addProduct"): ("PUT", "product"),
        ("catalogue", "updateProduct"): ("PATCH", "product/{id}"),
        ("catalogue", "deleteProduct"): ("DELETE", "product/{id}"),
        ("catalogue", "requestProduct"): ("GET", "request/product/{id}"),
        ("catalogue", "searchProductsByTag"): (
            "GET", "search/products/tag/{todus_id}/{tag}/{limit}/{offset}",
        ),
        ("catalogue", "searchTags"): ("GET", "search/tags/{todus_id}"),
        ("catalogue", "shareProduct"): ("GET", "share/product/{id}"),
        ("catalogue", "shareCatalog"): ("GET", "share/product/{id}"),
    }
    from todus_api import OPERATIONS

    real = {(o.service, o.method): (o.verb, o.path) for o in OPERATIONS}
    assert real == esperado, (
        "las operaciones del cliente divergen de la tabla documentada:\n"
        f"solo en cliente: {set(real) - set(esperado)}\n"
        f"solo en tabla:   {set(esperado) - set(real)}"
    )


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
