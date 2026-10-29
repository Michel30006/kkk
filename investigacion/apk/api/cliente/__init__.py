"""
Cliente de la API de ToDus, reconstruido desde la ingeniería inversa de la APK
`cu.todus.android` v3.0.2 (ToDus 2.1.2 / 30102).

    from cliente import TodusApi, Environment
    from cliente.xmpp.client import TodusXMPPClient
"""
from .todus_api import (  # noqa: F401
    ApiService,
    Environment,
    OPERATIONS,
    SERVICE_URLS,
    TodusApi,
    TodusApiError,
)

__all__ = [
    "TodusApi",
    "TodusApiError",
    "Environment",
    "ApiService",
    "OPERATIONS",
    "SERVICE_URLS",
]
