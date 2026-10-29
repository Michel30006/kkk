"""
Prueba contra los endpoints REALES de la API de ToDus.

Solo usa operaciones de lectura que la app movil hace sin sesion iniciada
(ningun endpoint de registro/reserva/login se toca aqui). Los endpoints que
exigen token (catalogo, releases, dispositivos) fallan aqui a proposito:
para esos esta `test_real_auth.py`.

    python test_real.py
"""
import sys
import traceback

from todus_api import Environment, TodusApi, TodusApiError

# (nombre, callable)
PRUEBAS = [
    ("GET api.apklis.cu/v1/application/338/  (lastRelease)",
     lambda api: api.last_release()),
    ("GET client.todus.cu/todus/release/rel/latest",
     lambda api: api.last_release_from_todus()),
    ("GET client.todus.cu/todus/release/bet/latest",
     lambda api: api.last_beta_from_todus()),
    ("GET todus.cu/catalog/version",
     lambda api: api.catalogue_version()),
    ("GET stickers.todus.cu/v1/stickers/packs/5/0",
     lambda api: api.sticker_packs(5, 0)),
]


def main() -> int:
    api = TodusApi(Environment.PROD, timeout=30)
    fallos = 0
    for nombre, fn in PRUEBAS:
        try:
            salida = fn(api)
        except TodusApiError as exc:
            fallos += 1
            print(f"[FALLO] {nombre}\n        {exc}")
        except Exception:  # pragma: no cover
            fallos += 1
            print(f"[ERROR] {nombre}")
            traceback.print_exc()
        else:
            texto = (salida or "").strip()
            if len(texto) > 300:
                texto = texto[:300] + " ...[truncado]"
            print(f"[OK]    {nombre}\n        {texto!r}")
    print()
    print(f"{len(PRUEBAS) - fallos}/{len(PRUEBAS)} endpoints respondieron")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
