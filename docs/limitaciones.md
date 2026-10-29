# Limitaciones y riesgos confirmados

Este documento separa lo **probado en esta entrega** de lo que sólo está
implementado o depende de terceros. No se declara operativa ninguna
integración que no se haya ejecutado.

## 1. ToDus

### 1.1 Qué se ha comprobado (2026-10-28)

| Comprobación | Resultado |
|---|---|
| DNS y TLS de `im.todus.cu:5222` | Conexión establecida, **TLS 1.3** (`TLS_AES_256_GCM_SHA384`), certificado `CN=todus.cu`, verificación correcta. |
| `GET https://api.apklis.cu/v1/application/338/` | **200**, JSON con la ficha de la aplicación. |
| `GET https://todus.cu/catalog/version` | **401** `{"err":"missing-authorization-header"}`. |
| `GET https://auth.todus.cu/v2/auth/token` | **404** `404 page not found` (el endpoint es `POST`; un `GET` no es una prueba válida de login). |
| `GET https://client.todus.cu/todus/release/version` | **403** `AccessDenied` (bucket S3). |
| `GET https://stickers.todus.cu/v1/packs` | **404** `NoSuchKey`. |
| Inicio de sesión con una cuenta real | **No realizado** en esta entrega. |

Conclusión: la red y el servidor XMPP son alcanzables, pero **no se ha
validado el envío real de un vídeo por ToDus**. El transporte `mock` es el que
se usa en la prueba integral.

El 2026-10-29 se ejecutó esa prueba integral con los números de prueba
`58799457` y `58548838`: se normalizaron a `5358799457@im.todus.cu` y
`5358548838@im.todus.cu`, el recorrido completo terminó con SHA-256 idéntico
y estado `cleaned`, y ese mismo día se repitió el handshake TLS con
`im.todus.cu:5222` (TLS 1.3, `CN=todus.cu`). Todo ello con transporte `mock`:
no hubo sesión XMPP ni entrega real. Ver
[prueba-integral.md](prueba-integral.md).

### 1.2 Deriva de la API observada

El material de investigación incluido en `investigacion/` documenta una
verificación con cuentas reales los días 2026-10-21 y 2026-10-24 (sesión
XMPP, texto en ambos sentidos, troceado de mensajes > 8192 B y `tdack`). El
2026-10-28 las sondas HTTP muestran respuestas distintas (401/403/404). Esto
indica que la superficie HTTP de ToDus cambia sin aviso y que cualquier
integración depende de un servicio no documentado oficialmente.

Por eso:

- `TODUS_AUTH_URL`, `TODUS_DOMAIN`, `TODUS_PORT` son configurables y no se
  codifican como verdades eternas;
- el cliente XMPP sólo se activa con `TRANSPORT=todus` y credenciales;
- **no** se presenta la integración como terminada ni se garantiza que la
  versión actual de la aplicación ToDus siga hablando el mismo protocolo.

### 1.3 Límites del transporte

| Límite | Valor | Origen |
|---|---|---|
| Tamaño de stanza | **8192 bytes UTF-8** | Investigación de la APK y pruebas previas recogidas en `investigacion/`. |
| Confirmación de entrega | `<tdack mi="…"/>` | Protocolo reconstruido; el `Transport` la espera con `ACK_TIMEOUT_MS`. |
| Tipo de mensaje | texto, `type='c'` | Reconstruido de la APK. |
| Cuenta | número con prefijo de país (`53…`) + JWT | Reconstruido; no hay API pública documentada. |

`tdack` confirma que el **servidor** aceptó el mensaje, no que el teléfono
haya reconstruido el fichero. Por eso el trabajo no pasa a `completed` hasta
que el receptor envía el SHA-256 y el tamaño por `POST /jobs/{id}/confirm`.

### 1.4 Consecuencias prácticas

- La entrega real por ToDus queda **pendiente de validación con cuentas
  autorizadas**. Es el bloqueo principal del proyecto.
- El ritmo de envío (`SEND_DELAY_MS`) y los reintentos son conservadores para
  no provocar bloqueos; no se ha medido el caudal real de ToDus.
- Un vídeo de 25 MiB produce ~5370 tramas: a 120 ms por trama son ~11 minutos
  sólo de envío, sin contar reconexiones. Es un límite de diseño del
  transporte, no del codificador.

## 2. Render (plan gratuito)

Límites usados para el diseño (verificados el 2026-10-26 y anotados en
`render.yaml`):

| Restricción | Consecuencia |
|---|---|
| **0,1 CPU** y **512 MB de RAM** por servicio web | La codificación y el envío son secuenciales (`MAX_CONCURRENT_JOBS=1`). 25 MiB es el máximo asumible; el pico medido de B fue ~77 MB y el de A ~20 MB en local. |
| Suspensión tras **15 min sin tráfico entrante** | Un trabajo largo puede quedar a medias. El ping mutuo cada 10 min lo mitiga, **no lo garantiza**; además el ping es tráfico saliente y puede no contar como actividad. |
| **Disco efímero** | No se usa para compartir datos entre A y B. El `outbox` del transporte `mock` es sólo para pruebas locales. |
| PostgreSQL gratuito de **1 GB**, caduca a los **30 días** | Los payloads codificados ocupan ~1,66 veces el vídeo. Un vídeo de 25 MiB deja ~41 MiB de texto; caben pocos trabajos simultáneos y la base caduca. |
| Sin red privada en servicios gratuitos | A y B se comunican por URLs públicas y comparten la base pública de Render. |
| Un solo proceso por servicio | No hay workers paralelos; la concurrencia se controla en la base con leases. |

No se asume que Render ofrezca almacenamiento de objetos, trabajos en segundo
plano ni disponibilidad continua. El sistema está diseñado para reanudar
trabajos desde PostgreSQL tras un reinicio, pero **no** para mantener una
transferencia indefinidamente si el servicio se duerme.

## 3. Almacenamiento y tamaño

- Los datos codificados se guardan en `job_data_parts` (bloques de
  `ENCODE_PART_BYTES`, 64 KiB por defecto) y se borran al confirmar.
- La tabla de codificación v1 cuesta **1,6562 bytes UTF-8 por byte de vídeo**
  (88 símbolos de 1 byte y 168 de 2 bytes). Un vídeo de 25 MiB ocupa
  ~41,4 MiB de texto antes de las cabeceras de trama.
- El límite por defecto es 25 MiB (`MAX_VIDEO_BYTES`). Subirlo en Render
  gratuito no es realista por CPU, RAM y cuota de base de datos.
- No se usa un almacenamiento de objetos porque el plan gratuito elegido no
  lo garantiza; la base de datos conserva metadatos y bloques temporales.

## 4. Cliente

- El cliente móvil se sustituye por el **componente receptor acordado**:
  `tds_receiver`, una CLI C++ que implementa el mismo protocolo.
- En modo `--frames` lee un directorio; en modo XMPP escucha mensajes. No hay
  interfaz gráfica ni integración con la galería del teléfono.
- El receptor **no crea trabajos**: el alta se hace con `POST /jobs` (por
  ejemplo con `curl`) y al CLI se le pasan el `job_id` y el `client_token` ya
  emitidos. Tampoco muestra progreso en tiempo real: informa al terminar.
- La verificación es tamaño + SHA-256. No hay firma digital ni cifrado
  extremo a extremo propio: la confidencialidad depende de TLS.
- Cualquiera que posea el `client_token` puede consultar el trabajo y
  confirmarlo; el token se devuelve una sola vez al crearlo y se guarda
  hasheado en la base.

## 5. Operación y seguridad

- `INTERNAL_TOKEN` es un secreto compartido único; no hay rotación ni
  identidades por servicio.
- No hay endpoint de cancelación ni de borrado manual; la expiración la
  realiza el hilo de mantenimiento.
- No hay limitación de peticiones (rate limiting) en la API.
- La validación de URL bloquea direcciones privadas y esquemas no permitidos,
  pero sigue dependiendo de la resolución DNS y de la política configurada;
  `ALLOW_PRIVATE_NETWORKS=1` sólo debe usarse en pruebas locales.
- Los logs evitan secretos y números de teléfono completos, pero el
  destinatario se guarda en la base porque es necesario para entregar.
- Los benchmarks se han medido en un contenedor de 2 vCPU; no representan
  Render gratuito (0,1 CPU) ni un móvil real.
- La compilación con libpqxx 8 emite advertencias de deprecación por el uso de
  `exec_params`; la API sigue funcionando en la versión fijada, pero conviene
  migrar a `exec(..., pqxx::params{...})` en una revisión futura.

## 6. Qué faltaría para cerrar el proyecto al 100 %

1. Cuenta o cuentas de ToDus autorizadas y una prueba de envío/recepción real
   con un vídeo pequeño, comparando SHA-256.
2. Confirmar el límite de stanza vigente y el caudal real (mensajes/s) con esa
   cuenta.
3. Verificar el comportamiento tras suspensión de Render con una prueba
   programada de varias horas.
4. Decidir si se necesita almacenamiento de objetos para vídeos mayores y
   asumir su coste.
5. Rotación del token interno y autenticación por servicio si el sistema deja
   de ser una evaluación.
