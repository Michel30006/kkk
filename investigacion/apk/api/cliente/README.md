# Cliente de la API de ToDus (reconstruido)

Cliente Python de la **API de control** de ToDus, reconstruido íntegramente a
partir de la ingeniería inversa de la APK `cu.todus.android` v3.0.2.

No es una suposición: cada verbo, ruta, cabecera y cuerpo protobuf de este
cliente sale de la APK (ver [`../../informe.md`](../../informe.md) y
[`../superficie-http.md`](../superficie-http.md)).

## Estado

| Capa | Cobertura |
|---|---|
| **API REST + protobuf** | ✅ 32 endpoints implementados (las 33 operaciones de la tabla: `token` y `tokenNotLive` comparten ruta) |
| **Autenticación REST** | ✅ `v2/auth/token` → `Authorization: <token>` (JWT **crudo**, sin `Bearer `) |
| **Esquema protobuf** | ✅ 34 mensajes / 78 campos, generado desde `ApiProto.proto` |
| **Mensajería XMPP** | ✅ cliente implementado: sesión SASL PLAIN, bind, envío/recepción, troceado, marcadores, 25 extensiones, 78 IQs — **verificado en vivo contra `im.todus.cu`** |
| **Video** | ⚠️ stanza y metadata ✅ verificadas en vivo (URL falsa); **falta** subida/descarga real a S3 y `send_video()` |
| **WebRTC / llamadas** | ❌ pendiente (fase 2) |

> **Aviso (2026-10-28):** la tabla siguiente recoge lo observado los días
> 2026-10-21 y 2026-10-24. Una sonda posterior el 2026-10-28 obtuvo
> `401 missing-authorization-header` en `GET todus.cu/catalog/version`,
> `403 AccessDenied` en `client.todus.cu/todus/release/...` y `404` en
> `stickers.todus.cu/...`: la superficie HTTP ha cambiado. Las filas de XMPP
> corresponden a la verificación de octubre y **no** se han repetido en esta
> entrega. Ver `docs/limitaciones.md`.

### Verificado contra los servidores reales (2026-10-21 / 2026-10-24)

| Llamada | Respuesta |
|---|---|
| `GET api.apklis.cu/v1/application/338/` | 200, JSON de 9 126 B |
| `POST auth.todus.cu/v2/auth/token` | 200, JWT de 165 chars |
| `GET todus.cu/catalog/version` | 200 `{"version":1}` |
| `GET todus.cu/catalog/shop/5/0` | 200 `{"catalog":[]}` |
| XMPP `im.todus.cu:5222` | TLS 1.3 → SASL PLAIN → bind → roster |
| Enviar texto | ✅ el servidor acusa con `<tdack mi='…'/>` |
| Recibir texto | ✅ `type='c'`, texto íntegro, con `tdack` de vuelta |
| Troceado (>8192 B) | ✅ 2 fragmentos con `msg:fragment:n`, reensamblado idéntico |
| Mensaje entre dos cuentas | ✅ en los dos sentidos |
| Marcas de "leído" (XEP-0333) | ✅ `send_chat_marker(...)` → `<displayed/>` |
| Chat-state "escribiendo" | ⚠️ llega abreviado (`<uc1:csp/>`); falta identificar su namespace |
| Enviar/recibir stanza de video | ✅ `5358548838` → `5358799457`, metadata `video:n` intacta (URL falsa, sin gastar datos de video) |

Detalles y correcciones (cabecera sin `Bearer `, TLS directo, namespaces
abreviados del servidor, bind clásico…) en
[`../../informe.md`](../../informe.md) §11.1.

## Uso — API REST

```python
from cliente import TodusApi, Environment

api = TodusApi(Environment.PROD)
api.login("5358799457", "clave")      # POST https://auth.todus.cu/v2/auth/token
print(api.last_release())              # GET  https://api.apklis.cu/v1/application/338/
print(api.catalogue_version())         # GET  https://todus.cu/catalog/version
```

Entornos: `Environment.PROD` (producción), `Environment.LOCAL` (hosts `-test`).

> **`username` = código de país sin `+` + teléfono** (Cuba: `53` + 8 dígitos).
> Con el número suelto el servidor responde `sql: no rows in result set`.
> La contraseña **no se valida** en `v2/auth/token` (hallazgo §11 del
> informe): sirve el JWT para todo lo demás.

## Uso — mensajería XMPP

La sesión de mensajería se autentica con **el JWT de la API como contraseña**
(SASL PLAIN), no con la contraseña del usuario:

```python
from cliente import TodusApi, Environment
from cliente.xmpp.client import TodusXMPPClient
from cliente.xmpp import todus as T

api = TodusApi(Environment.PROD)
token = api.login("5358799457", "")   # la contraseña da igual: el servidor no la valida

c = TodusXMPPClient("im.todus.cu", "5358799457", token)   # TLS directo
c.connect()                             # SASL PLAIN → bind
print(c.conn.jid)                        # 5358799457@im.todus.cu/todus

# el destinatario va por `username` (teléfono con código de país), NO por el alias
c.send_text(T.user_jid("5358799457"), "hola")
c.send_image(T.user_jid("5358799457"),
             url="https://s3.todus.cu/x", name="f.png", size=1024)
c.send_marker(message_id="m1", room="r1", marker="received")

for msg in c.incoming():
    print(msg.from_jid, msg.text, msg.extensions)
```

> **A quién se envía**: el localpart del JID es el `username` (teléfono con
> código de país, `53` + 8 dígitos). El `toDusId`/alias (`michel30`) **no**
> enruta: el servidor acepta el mensaje pero no lo entrega.
>
> `im.todus.cu` **no** admite STARTTLS: hay que conectar con TLS directo
> (`tls_mode="direct"`, valor por defecto). El servidor (ejabberd) usa
> namespaces, elementos y atributos abreviados; el parser de este cliente ya
> los traduce.

Detalles de protocolo respetados por el cliente (todo verificado en la APK):

- **JID**: usuarios `@im.todus.cu`, canales `@ch.im.todus.cu`, grupos
  `@muclight.im.todus.cu`, estados `status@status.im.todus.cu`.
- **Troceado**: los textos de más de 8192 bytes (`MAX_MSG_TEXT_SIZE`) se
  trocean y cada trozo lleva la extensión `msg:fragment:n` (`msgId`, `index`,
  `total`).
- **Atributos exactos** de cada extensión (p. ej. la imagen usa `i`, `mi`,
  `url`, `n`, `s`, `h`, `w`, `he` y `tnail` — no `height` ni `thumbnail`).
- **IQs**: cada una con su `ELEMENT` y su `NAMESPACE` (`todus:roster:list:2`,
  `todus:purl`, `todus:ch:publish`, …). Tabla completa en
  [`../../xmpp/iqs.md`](../../xmpp/iqs.md).
- **Acuse de stream** propio de ToDus: `<tdack mi='…' o='…'/>`.

## Ficheros

| Fichero | Contenido |
|---|---|
| `todus_api.py` | El cliente REST (`TodusApi`) + tabla `OPERATIONS` con las 33 operaciones |
| `xmpp/stream.py` | Núcleo XMPP (parser de stream, SASL PLAIN, bind) — solo stdlib |
| `xmpp/todus.py` | Capa de protocolo ToDus: JID, extensiones, troceado, IQs |
| `xmpp/client.py` | Cliente de mensajería de alto nivel |
| `ApiProto.proto` | Esquema protobuf reconstruido (paquete `cu.todus.proto`) |
| `ApiProto_pb2.py` | Módulo generado con `protoc` (regenerable) |
| `test_cliente.py` | Pruebas REST con transporte simulado |
| `xmpp/test_xmpp.py` | Pruebas XMPP offline (parser, extensiones, troceado, IQs, video) |
| `test_real.py` | Endpoints públicos reales (sin sesión) |
| `test_real_auth.py` | Endpoints autenticados reales (lee `TODUS_USER`/`TODUS_PASS`) |
| `test_real_xmpp.py` | Sesión XMPP real (bind + roster) |
| `test_mensaje.py` | Envía un texto y escucha los entrantes |
| `test_mensaje_loopback.py` | Bucle automático: dos sesiones de la misma cuenta, envío y recepción |
| `test_mensaje_2cuentas.py` | Mensaje entre dos cuentas distintas, con traza completa |
| `test_video_2cuentas.py` | Envía/recibe la stanza de video (URL falsa, sin subir archivo) |
| `test_escuchar.py` | Solo escucha; responde con `tdack` y con la marca de "leído" |
| `test_formato_usuario.py` | Deduce el formato del `username` que acepta el servidor |

## Regenerar el módulo protobuf

```bash
python3 -m grpc_tools.protoc -I . --python_out=. ApiProto.proto
```

## Pruebas

```bash
python3 test_cliente.py     # 7 pruebas REST (transporte simulado)
python3 xmpp/test_xmpp.py   # 33 pruebas XMPP (offline)
```

No contactan ningún servidor: usan un transporte simulado. Requieren
`requests` y `protobuf` instalados (`pip install requests protobuf`).

Las pruebas *reales* sí contactan producción y **no** registran ni reservan
nombres de usuario:

```bash
python3 test_real.py
python3 test_real_auth.py <53+telefono>
python3 test_real_xmpp.py <53+telefono>
python3 test_video_2cuentas.py <53+origen> <53+destino>
```

> `test_video_2cuentas.py` usa una URL de video **falsa**: solo viaja la
> stanza (metadata, ~1 KB), no se sube ni descarga video.

## Advertencia

La API real es la del servidor de producción de ToDus (`*.todus.cu`). Este
cliente existe para ingeniería inversa, interoperabilidad y estudios; úsalo solo
contra sistemas que tengas autorización para operar. Nunca automatices el
login ni el registro contra producción: `v2/auth/users.register` consume un
código de un solo uso y `v2/auth/users.reserve` reserva nombres de usuario en
el servidor real.

> `v2/auth/token` **no valida la contraseña** (hallazgo §11.6 del informe):
> con un usuario existente devuelve token válido para cualquier contraseña.
> Eso hace que el login sea automatizable de forma masiva, así que úsalo solo
> contra cuentas propias y con moderación.
