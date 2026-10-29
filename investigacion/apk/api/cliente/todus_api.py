"""
Cliente de la API de ToDus reconstruido a partir de la ingeniería inversa de
la APK `cu.todus.android` v3.0.2 (2.1.2 / 30102).

Todo lo que este cliente implementa está **verificado contra el artefacto**:

* Rutas y verbos HTTP: extraídos del smali de las interfaces Retrofit
  (`investigacion/apk/api/superficie-http.md`).
* Cuerpos protobuf: esquema reconstruido desde el código generado
  (`investigacion/apk/api/ApiProto.reconstruido.proto`).
* URLs base: `cu.todus.android.constants.Service.provideXUrl`
  (investigacion/apk/informe.md §5).
* Autenticación: `POST v2/auth/token` -> `Authorization: Bearer <tokenString>`,
  renovación vía `Authenticator` de OkHttp.

La API real es la del servidor de producción de ToDus (auth.todus.cu,
client.todus.cu, ...). Úsalo solo contra sistemas que tengas autorización
para operar.
"""
from __future__ import annotations

import dataclasses
import enum
import os
from typing import Any, Mapping, Sequence

import requests

try:  # como paquete (`from cliente import TodusApi`)
    from . import ApiProto_pb2 as pb
except ImportError:  # como módulo suelto (scripts, pruebas)
    import ApiProto_pb2 as pb

__all__ = [
    "TodusApi",
    "TodusApiError",
    "Environment",
    "SERVICE_URLS",
    "ApiService",
]


class TodusApiError(RuntimeError):
    """Error devuelto por la API de ToDus o por el transporte HTTP."""


class Environment(str, enum.Enum):
    """Entornos declarados en `Service$ApplicationEnvironment`."""

    PROD = "PROD"
    PROD_2 = "PROD_2"
    F5 = "F5"
    LOCAL = "LOCAL"


# `cu.todus.android.constants.Service.provideXUrl` — URL base por servicio.
SERVICE_URLS: Mapping[str, Mapping[str, str]] = {
    "auth": {
        "PROD": "https://auth.todus.cu/",
        "PROD_2": "https://auth.todus.cu/",
        "F5": "https://auth.todus.cu/",
        "LOCAL": "https://auth-test.todus.cu/",
    },
    "client": {
        "PROD": "https://client.todus.cu/",
        "PROD_2": "https://client.todus.cu/",
        "F5": "https://client.todus.cu/",
        "LOCAL": "https://client-test.todus.cu/",
    },
    "catalogue": {
        "PROD": "https://todus.cu/catalog/",
        "PROD_2": "https://todus.cu/catalog/",
        "F5": "https://todus.cu/catalog/",
        "LOCAL": "http://10.37.41.159:8086/catalog/",
    },
    "chat": {
        "PROD": "https://chat.todus.cu/",
        "PROD_2": "https://chat.todus.cu/",
        "F5": "https://chat.todus.cu/",
        "LOCAL": "https://chat-test.todus.cu/",
    },
    "s3": {
        "PROD": "https://s3.todus.cu/",
        "PROD_2": "https://s3.todus.cu/",
        "F5": "https://s3.todus.cu/",
        "LOCAL": "https://s3-test.todus.cu/",
    },
    "stickers": {
        "PROD": "https://stickers.todus.cu/",
        "PROD_2": "https://stickers.todus.cu/",
        "F5": "https://stickers.todus.cu/",
        "LOCAL": "https://stickers-test.todus.cu/",
    },
    # `LinkedDeviceService` y `CertifiedInfoService` no tienen host propio en
    # la APK: comparten el host de `ClientService` (*inferido* a partir de que
    # ambas son servicios REST de la app con rutas relativas cortas).
    "linkedDevice": {
        "PROD": "https://client.todus.cu/",
        "PROD_2": "https://client.todus.cu/",
        "F5": "https://client.todus.cu/",
        "LOCAL": "https://client-test.todus.cu/",
    },
    "certified": {
        "PROD": "https://client.todus.cu/",
        "PROD_2": "https://client.todus.cu/",
        "F5": "https://client.todus.cu/",
        "LOCAL": "https://client-test.todus.cu/",
    },
}

# User-Agent observado en `TokenAuthenticator$Companion.headerWithAuth`.
USER_AGENT = "ToDus 2.1.2 "

# `Content-Type` observado en `ProtoRequestBodyConverter`.
CONTENT_TYPE_PROTOBUF = "application/x-protobuf"

# Prefijo de la cabecera `Authorization`.
#
# La APK sugiere la cadena literal "Bearer " (`TokenAuthenticator$Companion`),
# pero el servidor real **rechaza** ese formato: `GET todus.cu/catalog/version`
# con `Authorization: Bearer <jwt>` responde `{"err":"invalid token"}` y con
# el JWT crudo responde `200 {"version":1}`. Se usa el token crudo.
TOKEN_PREFIX = ""


@dataclasses.dataclass(frozen=True)
class ApiService:
    """Una operación REST documentada, con su verbo y ruta exactos."""

    service: str
    method: str
    verb: str
    path: str
    body_proto: str | None = None
    response_proto: str | None = None


# Catálogo completo: 33 operaciones (ver api/superficie-http.md).
OPERATIONS: Sequence[ApiService] = (
    # --- AuthService -------------------------------------------------------
    ApiService("auth", "token", "POST", "v2/auth/token",
               "ProtoUserAuthTokenRequest", "ProtoUserAuthTokenResponse"),
    ApiService("auth", "passCheck", "POST", "v2/auth/check",
               "ProtoAuthCheckRequest", None),
    ApiService("auth", "register", "POST", "v2/auth/users.register",
               "ProtoUserRegisterRequest", "ProtoUserRegisterResponse"),
    ApiService("auth", "reserve", "POST", "v2/auth/users.reserve",
               "ProtoUserReserveRequest", None),
    ApiService("auth", "linkInfo", "POST", "v3/preview",
               "LinkInfoRequest", None),
    # --- ClientService -----------------------------------------------------
    ApiService("client", "lastRelease", "GET", "https://api.apklis.cu/v1/application/338/"),
    ApiService("client", "lastBetaFromToDus", "GET", "todus/release/bet/latest"),
    ApiService("client", "lastReleaseFromToDus", "GET", "todus/release/rel/latest"),
    ApiService("client", "downloadLink", "POST", "https://api.apklis.cu/v2/release/get_url/"),
    # --- UserService -------------------------------------------------------
    ApiService("user", "updateProfile", "POST", "v2/todus/users.me",
               "ProtoUsersMeRequest", None),
    # --- LinkedDeviceService ----------------------------------------------
    ApiService("linkedDevice", "getLinkedDevices", "GET", "list"),
    ApiService("linkedDevice", "linkDevice", "GET", "", None, None),
    ApiService("linkedDevice", "logoutDevice", "POST", "logout", "LinkedDeviceLogoutRequest"),
    # --- ReportService -----------------------------------------------------
    ApiService("report", "reportMessage", "POST", "v1/report", "ReportMessageRequest"),
    # --- StickerService ----------------------------------------------------
    ApiService("stickers", "packInfo", "GET", "v1/stickers/info/{pack_name}"),
    ApiService("stickers", "packs", "GET", "v1/stickers/packs/{limit}/{offset}"),
    # --- CertifiedInfoService ----------------------------------------------
    ApiService("certified", "getInfo", "GET", "card/info/{uid}"),
    ApiService("certified", "getInfoOrError", "GET", "info/{uid}"),
    ApiService("certified", "subscribe", "GET", "card/subscribe/{uid}"),
    ApiService("certified", "unSubscribe", "GET", "card/unsubscribe/{uid}"),
    # --- CatalogueService --------------------------------------------------
    ApiService("catalogue", "version", "GET", "version"),
    ApiService("catalogue", "myShop", "GET", "shop/{limit}/{offset}"),
    ApiService("catalogue", "shop", "GET", "{todus_id}/{limit}/{offset}"),
    ApiService("catalogue", "getProduct", "GET", "product/{todus_id}/{id}"),
    ApiService("catalogue", "addProduct", "PUT", "product", "AddProductRequest"),
    ApiService("catalogue", "updateProduct", "PATCH", "product/{id}", "UpdateProductRequest"),
    ApiService("catalogue", "deleteProduct", "DELETE", "product/{id}"),
    ApiService("catalogue", "requestProduct", "GET", "request/product/{id}"),
    ApiService("catalogue", "searchProductsByTag", "GET",
               "search/products/tag/{todus_id}/{tag}/{limit}/{offset}"),
    ApiService("catalogue", "searchTags", "GET", "search/tags/{todus_id}"),
    ApiService("catalogue", "shareProduct", "GET", "share/product/{id}"),
    ApiService("catalogue", "shareCatalog", "GET", "share/product/{id}"),
)


class TodusApi:
    """Cliente de la API de control de ToDus (plano REST + protobuf)."""

    def __init__(
        self,
        environment: Environment | str = Environment.PROD,
        *,
        session: requests.Session | None = None,
        timeout: float = 30.0,
        user_agent: str = USER_AGENT,
        client_version: str = "2.1.2",
    ) -> None:
        self.environment = Environment(environment)
        self._http = session or requests.Session()
        self._timeout = timeout
        self._user_agent = user_agent
        self._client_version = client_version
        self._token: str | None = None
        self._username: str | None = None

    # -- infraestructura ---------------------------------------------------
    def _base(self, service: str) -> str:
        try:
            return SERVICE_URLS[service][self.environment.value]
        except KeyError as exc:  # pragma: no cover - configuración inválida
            raise TodusApiError(f"sin URL base para el servicio {service!r}") from exc

    def _op(self, service: str, method: str) -> ApiService:
        for op in OPERATIONS:
            if op.service == service and op.method == method:
                return op
        raise TodusApiError(f"operación desconocida: {service}.{method}")

    def _headers(self, *, auth: bool = True) -> dict[str, str]:
        headers = {
            "User-Agent": self._user_agent,
            "Accept": "*/*",
        }
        if auth and self._token:
            headers["Authorization"] = f"{TOKEN_PREFIX}{self._token}"
        return headers

    def _call(
        self,
        service: str,
        method: str,
        *,
        path_params: Mapping[str, Any] | None = None,
        body: bytes | None = None,
        query: Mapping[str, Any] | None = None,
        absolute_url: str | None = None,
        auth: bool = True,
    ) -> requests.Response:
        op = self._op(service, method)
        if absolute_url:
            url = absolute_url
        elif op.path.startswith(("http://", "https://")):
            # La anotacion de Retrofit ya trae la URL completa (`@Url`).
            url = op.path
        else:
            url = self._base(service) + op.path
        for key, value in (path_params or {}).items():
            url = url.replace("{" + key + "}", str(value))
        headers = self._headers(auth=auth)
        if body is not None:
            headers["Content-Type"] = CONTENT_TYPE_PROTOBUF
        resp = self._http.request(
            op.verb, url, data=body, params=dict(query or {}) or None,
            headers=headers, timeout=self._timeout,
        )
        if resp.status_code >= 400:
            raise TodusApiError(
                f"{op.verb} {url} -> HTTP {resp.status_code}: {resp.text[:400]}"
            )
        return resp

    @staticmethod
    def _encode(msg) -> bytes:
        return msg.SerializeToString()

    @staticmethod
    def _decode(resp: requests.Response, msg_type):
        if msg_type is None:
            return resp.text
        out = msg_type()
        out.ParseFromString(resp.content)
        return out

    # -- autenticación -----------------------------------------------------
    def login(self, username: str, password: str) -> str:
        """`POST v2/auth/token` -> token de sesión (campo `tokenString`)."""
        req = pb.ProtoUserAuthTokenRequest(
            username=username, password=password, version=self._client_version
        )
        resp = self._call("auth", "token", body=self._encode(req))
        token = self._decode(resp, pb.ProtoUserAuthTokenResponse).tokenString
        if not token:
            raise TodusApiError("la respuesta de auth/token no trae tokenString")
        self._token = token
        self._username = username
        return token

    def check_credentials(self, username: str, password: str) -> requests.Response:
        """`POST v2/auth/check` — valida credenciales sin abrir sesión."""
        req = pb.ProtoAuthCheckRequest(username=username, password=password)
        return self._call("auth", "passCheck", body=self._encode(req), auth=False)

    def register(self, username: str, client_token: str, code: str):
        """`POST v2/auth/users.register` — alta de usuario (code = OTP)."""
        req = pb.ProtoUserRegisterRequest(
            username=username, clientToken=client_token, code=code
        )
        resp = self._call("auth", "register", body=self._encode(req), auth=False)
        return self._decode(resp, pb.ProtoUserRegisterResponse)

    def reserve(self, username: str, client_token: str) -> requests.Response:
        """`POST v2/auth/users.reserve` — reserva de nombre de usuario."""
        req = pb.ProtoUserReserveRequest(username=username, clientToken=client_token)
        return self._call("auth", "reserve", body=self._encode(req), auth=False)

    def update_profile(self, user_info: Mapping[str, str]) -> requests.Response:
        """`POST v2/todus/users.me` — actualización de perfil."""
        req = pb.ProtoUsersMeRequest(userInfo=dict(user_info))
        return self._call("user", "updateProfile", body=self._encode(req))

    # -- cliente / versiones ------------------------------------------------
    def last_release(self) -> str:
        """`GET https://api.apklis.cu/v1/application/338/` — última versión."""
        return self._call("client", "lastRelease").text

    def last_release_from_todus(self) -> str:
        """`GET todus/release/rel/latest`."""
        return self._call("client", "lastReleaseFromToDus").text

    def last_beta_from_todus(self) -> str:
        """`GET todus/release/bet/latest`."""
        return self._call("client", "lastBetaFromToDus").text

    # -- dispositivos vinculados -------------------------------------------
    def linked_devices(self) -> str:
        """`GET list` — dispositivos vinculados a la cuenta."""
        return self._call("linkedDevice", "getLinkedDevices").text

    def logout_device(self, request) -> requests.Response:
        """`POST logout` — cierra la sesión de un dispositivo vinculado."""
        return self._call("linkedDevice", "logoutDevice", body=self._encode(request))

    # -- reportes -----------------------------------------------------------
    def report_message(self, request) -> requests.Response:
        """`POST v1/report` — denuncia de un mensaje."""
        return self._call("report", "reportMessage", body=self._encode(request))

    # -- stickers -----------------------------------------------------------
    def sticker_packs(self, limit: int, offset: int) -> str:
        """`GET v1/stickers/packs/{limit}/{offset}`."""
        return self._call(
            "stickers", "packs",
            path_params={"limit": limit, "offset": offset},
        ).text

    def sticker_pack_info(self, pack_name: str) -> str:
        """`GET v1/stickers/info/{pack_name}`."""
        return self._call("stickers", "packInfo", path_params={"pack_name": pack_name}).text

    # -- catálogo -----------------------------------------------------------
    def catalogue_version(self) -> str:
        """`GET version` — versión del catálogo."""
        return self._call("catalogue", "version").text

    def my_shop(self, limit: int, offset: int) -> str:
        """`GET shop/{limit}/{offset}` — tienda del usuario."""
        return self._call(
            "catalogue", "myShop", path_params={"limit": limit, "offset": offset}
        ).text

    def shop(self, todus_id: str, limit: int, offset: int) -> str:
        """`GET {todus_id}/{limit}/{offset}` — tienda de un usuario."""
        return self._call(
            "catalogue", "shop",
            path_params={"todus_id": todus_id, "limit": limit, "offset": offset},
        ).text

    def get_product(self, todus_id: str, product_id: str) -> str:
        """`GET product/{todus_id}/{id}`."""
        return self._call(
            "catalogue", "getProduct",
            path_params={"todus_id": todus_id, "id": product_id},
        ).text

    def add_product(self, request) -> requests.Response:
        """`PUT product` — crea un producto."""
        return self._call("catalogue", "addProduct", body=self._encode(request))

    def update_product(self, product_id: str, request) -> requests.Response:
        """`PATCH product/{id}` — actualiza un producto."""
        return self._call(
            "catalogue", "updateProduct",
            path_params={"id": product_id}, body=self._encode(request),
        )

    def delete_product(self, product_id: str) -> requests.Response:
        """`DELETE product/{id}`."""
        return self._call("catalogue", "deleteProduct", path_params={"id": product_id})

    def request_product(self, product_id: str) -> str:
        """`GET request/product/{id}`."""
        return self._call(
            "catalogue", "requestProduct", path_params={"id": product_id}
        ).text

    def search_products_by_tag(
        self, todus_id: str, tag: str, limit: int, offset: int
    ) -> str:
        """`GET search/products/tag/{todus_id}/{tag}/{limit}/{offset}`."""
        return self._call(
            "catalogue", "searchProductsByTag",
            path_params={
                "todus_id": todus_id, "tag": tag, "limit": limit, "offset": offset,
            },
        ).text

    def search_tags(self, todus_id: str) -> str:
        """`GET search/tags/{todus_id}`."""
        return self._call("catalogue", "searchTags", path_params={"todus_id": todus_id}).text

    def share_product(self, product_id: str) -> str:
        """`GET share/product/{id}`."""
        return self._call(
            "catalogue", "shareProduct", path_params={"id": product_id}
        ).text

    # -- estado -------------------------------------------------------------
    @property
    def token(self) -> str | None:
        return self._token

    @property
    def is_authenticated(self) -> bool:
        return bool(self._token)
