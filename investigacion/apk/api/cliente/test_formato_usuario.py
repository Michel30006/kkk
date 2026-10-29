"""
Comprueba que formato de `username` acepta el servidor de ToDus.

Solo hace consultas de login con contrasena falsa: el servidor responde
"sql: no rows in result set" si el usuario NO existe, y otro error si el
usuario EXISTE (por ejemplo, contrasena incorrecta). asi podemos deducir el
formato del identificador sin conocer la contrasena.

    python test_formato_usuario.py 58799457
"""
import sys

from todus_api import Environment, TodusApi, TodusApiError

FORMATOS = [
    "{n}",
    "53{n}",
    "+53{n}",
    "0053{n}",
    "{n}@todus.cu",
    "{n}@im.todus.cu",
]


def main() -> int:
    numero = sys.argv[1] if len(sys.argv) > 1 else None
    if not numero:
        sys.exit("uso: python test_formato_usuario.py <numero>")
    api = TodusApi(Environment.PROD, timeout=30)
    for fmt in FORMATOS:
        usuario = fmt.format(n=numero)
        try:
            api.login(usuario, "clave-inventada")
        except TodusApiError as exc:
            texto = str(exc)
            if "no rows in result set" in texto:
                estado = "NO EXISTE"
            else:
                estado = f"EXISTE -> {texto[-160:]}"
        except Exception as exc:
            estado = f"error {type(exc).__name__}: {exc}"
        else:
            estado = "EXISTE -> ¡login con clave falsa aceptada!"
        print(f"{usuario:<28} {estado}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
