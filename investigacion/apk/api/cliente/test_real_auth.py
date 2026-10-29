"""
Prueba de la API de ToDus CON sesion iniciada (contra produccion).

Uso (no deja credenciales en el repo ni en el historial):

    set TODUS_USER=mi_usuario
    set TODUS_PASS=mi_clave
    python test_real_auth.py

o bien:

    python test_real_auth.py mi_usuario mi_clave

Ejecuta un login real (`POST v2/auth/token`) y luego las operaciones de solo
lectura que la app movil hace con el token: releases, dispositivos vinculados,
catalogo y stickers. No registra ni reserva nombres de usuario.
"""
import os
import sys
import traceback

from todus_api import Environment, TodusApi, TodusApiError

# (nombre, callable(api) -> texto)
PRUEBAS = [
    ("GET client.todus.cu/todus/release/rel/latest",
     lambda api: api.last_release_from_todus()),
    ("GET client.todus.cu/todus/release/bet/latest",
     lambda api: api.last_beta_from_todus()),
    ("GET https://api.apklis.cu/v1/application/338/",
     lambda api: api.last_release()),
    ("GET client.todus.cu/list  (dispositivos vinculados)",
     lambda api: api.linked_devices()),
    ("GET todus.cu/catalog/version",
     lambda api: api.catalogue_version()),
    ("GET todus.cu/catalog/shop/5/0  (mi tienda)",
     lambda api: api.my_shop(5, 0)),
    ("GET stickers.todus.cu/v1/stickers/packs/5/0",
     lambda api: api.sticker_packs(5, 0)),
]


def credenciales() -> tuple[str, str]:
    """
    Usuario y contraseña.

    Solo hace falta el usuario: `v2/auth/token` no valida la contraseña
    (hallazgo §11.6 del informe), pero se acepta igualmente para poder
    probar una cuenta con contraseña real.
    """
    if len(sys.argv) >= 2:
        return sys.argv[1], (sys.argv[2] if len(sys.argv) >= 3 else "")
    user = os.environ.get("TODUS_USER")
    pwd = os.environ.get("TODUS_PASS", "")
    if not user:
        sys.exit("Faltan credenciales: usa argumentos o TODUS_USER/TODUS_PASS")
    return user, pwd


def recorte(texto: str, n: int = 280) -> str:
    texto = (texto or "").strip()
    return texto if len(texto) <= n else texto[:n] + " ...[truncado]"


def main() -> int:
    user, pwd = credenciales()
    api = TodusApi(Environment.PROD, timeout=30)

    try:
        token = api.login(user, pwd)
    except TodusApiError as exc:
        print(f"[FALLO] login: {exc}")
        return 2
    print(f"[OK]    login: token de {len(token)} caracteres, "
          f"prefijo {token[:6]}...")
    print(f"        autenticado={api.is_authenticated}\n")

    fallos = 0
    for nombre, fn in PRUEBAS:
        try:
            salida = fn(api)
        except TodusApiError as exc:
            fallos += 1
            print(f"[FALLO] {nombre}\n        {exc}")
        except Exception:
            fallos += 1
            print(f"[ERROR] {nombre}")
            traceback.print_exc()
        else:
            print(f"[OK]    {nombre}\n        {recorte(salida)!r}")

    print(f"\n{len(PRUEBAS) - fallos}/{len(PRUEBAS)} endpoints respondieron")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
