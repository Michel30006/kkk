# Capa XMPP de ToDus — protocolo

Todo lo que sigue sale del artefacto (`cu.todus.android` v3.0.2). Nada se
ejecutó: es análisis estático.

## Topología de servicios XMPP

Constantes de `cu.todus.android.constants.Service$Config` (*observado*):

| Constante | Valor | Significado |
|---|---|---|
| `SERVER_DOMAIN` | `im.todus.cu` | dominio XMPP de usuarios (`<toDusId>@im.todus.cu`) |
| `SERVER_NAME_CHANNELS` | `ch` | prefijo de los canales |
| `CHANNEL_SERVICE_NAME` | `ch.im.todus.cu` | servicio de canales (dominio `@ch`) |
| `SERVER_NAME_MUC` | `muclight` | prefijo de los grupos |
| `MUC_LIGHT_SERVICE_NAME` | `muclight.im.todus.cu` | MUC ligera (grupos) |
| `STATUS_RECEPTOR` | `status@status.im.todus.cu` | servicio de estados/presencia |
| `MAX_MESSAGE_LENGTH` | `0x10000` = **65 536** | longitud máxima de un mensaje |
| `MAX_MSG_TEXT_SIZE` | `0x2000` = **8 192** | tamaño máximo del texto por trozo → origen del troceado |
| `COUNTER_VISIBLE_THRESHOLD` | `0xc8` = 200 | umbral de contador visible |
| `REACTION_NAMESPACE` | `reaction` | |
| `STATUS_NAMESPACE` | `status` | |
| `STICKERS_OFFICIAL` | `stickers.todus.cu` | servidor de stickers |

*Inferido:* el troceado de mensajes existe porque el límite de texto (8 192)
es mucho menor que el de mensaje completo (65 536): el cliente trocea el texto
y lo reensambla con `msg:fragment:n` + la tabla `message_chunks`.

## Cifrado local (preferencias)

La app incluye un módulo de cifrado propio, `cu.z17.preferences.encription`
(el nombre lleva la errata original "encription"), con esta pila
(*observado* en las constantes de la clase):

| Parámetro | Valor |
|---|---|
| Derivación de clave | **PBKDF2WithHmacSHA1** |
| Cifrado | **AES/CBC/PKCS5Padding** |
| Clave | `SecretKeySpec`, con sal, iteraciones y longitud configurables |
| Vector de inicialización | `IvParameterSpec` |
| Codificación de salida | **Base64** (propia, `Base64.encodeToString`/`decode`) |
| Charset | UTF-8; PRNG: `SHA1PRNG` |

API: `Encryption.getDefault(...)`, `encrypt`/`decrypt` (+ variantes `Async` y
`OrNull`), con `Encryption.Builder` (algoritmo, digest, iteraciones, sal, IV,
longitud de clave, modo Base64, charset, SecureRandom).

*Alcance:* es la protección de **datos locales de preferencias**, no el
cifrado de los mensajes. No hay criptografía propia en la ruta de envío de
mensajes: no se encontró `Cipher` en la capa XMPP, y **Tink no lo usa el
código de la app** (las 69 clases `com.google.crypto.tink` vienen por
dependencia transitiva y no se referencian desde `cu.todus.*`). El
cifrado extremo a extremo, si existe, no está implementado en este cliente:
queda *pendiente* (y hay que distinguirlo del cifrado de las **llamadas**, que
sí está en el cliente: DTLS-SRTP + `frame_crypto_transformer`).

## Formato del mensaje

- Mapper: `cu.todus.android.xmpp.mapper.MapperMessageToStanza` (entidad
  `Message` → stanza XMPP).
- Tipos de contenido reconocidos (*observado* en las clases `*Content`):
  `AudioContent`, `ButtonContent`, `CallContent`, `ContactContent`,
  `EventContent`, `FileContent`, `ForwardedContent`, `GifContent`,
  `ImageContent`, `LinkInfoContent`, `LocationContent`, `ReactionContent`,
  `ReplyMsgContent`, `StickerContent`, `VideoContent`, `VoiceContent`, más
  `MentionName` (mención de usuario).
- Serialización: **`org.json.JSONObject`** (52 referencias) y **Gson con
  `TypeToken`** (28) → el `payload` de la tabla `messages` es JSON.
- La tabla `messages` distingue `content` (texto plano) y `payload`
  (serializado, nulable): el texto va en el body y el payload lleva los
  metadatos/adjuntos.
- Al recibir: `handleNotNullBody` / `handleNullBody` separan mensaje con y sin
  cuerpo; `handlePayload` procesa el payload; `handleMarker` los marcadores de
  lectura; `handleAffiliationChange`, `handlePrivileges`,
  `handleRoomConfigs`, `handleRoomExistence` y `handlePubEdited/PubDeleted`
  atienden cambios de configuración y privilegios de sala.

## Video

`ExtensionVideo` (`video:n`) viaja dentro de un `<message type='chat'>` y
**solo lleva metadatos + una URL**, no el archivo:

```xml
<video xmlns='video:n' i='<id>' mi='<messageId>' url='<url S3>'
       n='<nombre>' s='<bytes>' d='<segundos>' w='<ancho>' he='<alto>'
       h='<hash>' tnail='<url miniatura>'/>
```

- La URL se obtiene con el IQ `todus:purl` (`PutUrlIQ`: `du`, `url`, `size`,
  `room`, `persistent`, `server`, `type`) y se descarga con `todus:gurl`
  (`GetUrlIQ`: `du`, `url`).
- **Verificado en vivo (2026-10-24)**: el envío `5358548838` → `5358799457`
  de una stanza con URL falsa llega con todos los atributos intactos. **No**
  se ha probado el PUT/GET real a S3, que es lo que falta para cerrar el
  flujo completo.

## Transporte y sesión

| Elemento | Valor | Evidencia |
|---|---|---|
| Puerto XMPP por defecto | **5222** (`0x1466`) | `XMPPSessionConfiguration$Builder` |
| Puertos declarados en `Service` | 1756 (`0x6dc`), 5443 (`0x1543`) | `Service.provideIMPort` |
| Host IM (prod / test) | `im.todus.cu` / `im-test.todus.cu` | `Service.provideIMUrl` |
| SASL | **PLAIN** | literal `"PLAIN"` en el paquete `xmpp` |
| Credenciales | usuario + contraseña | `XMPPCredentials` (Kotlin data class) |
| Configuración de sesión | `xmppDomain`, `xmppHost`, `xmppPort`, `debugMode`, `replyTimeOut`, `sendPresence` | `XMPPSessionConfiguration` |
| Biblioteca | Smack (`smack-core`, `smack-im`, `smack-tcp`, `smack-extensions`) + `org.jxmpp.jid.Jid` | 243 clases `org.jivesoftware.smack` |
| Reconexión | `Smack.ReconnectionManager` | 22 referencias |

## Extensiones propias (25)

Namespace propio con el patrón **`<elemento>:n`** — tabla completa en
[`extensiones.md`](extensiones.md).

| Extensión | Namespace | Atributos observados |
|---|---|---|
| `tcall` | `tcall:n` | `i`, `mi`, `st`, `cid` (id, message-id, state, call-id) |
| `chunk` | `msg:fragment:n` | troceado de mensajes largos |
| `mention` | `mention:n` | mención de usuario |
| `reaction` | `reaction:n` | reacción a un mensaje |
| `statusExistence` | `statusexistence:n` | estado de existencia (online/offline) |
| `file` / `image` / `video` / `audio` / `voice` / `gif` | `<tipo>:n` | metadatos de adjuntos |
| `sticker` | `sticker:n` | stickers |
| `location` | `location:n` | ubicación |
| `linkInfo` | `linkInfo:n` | vista previa de enlaces (`POST v3/preview`) |
| `edited` / `pubedited` | `edited:n` / `pubedited:n` | edición de mensajes |
| `deleted` / `pubdeleted` / `deletedowner` | `deleted:n` / `pubsdeleted:n` / `deletedowner:n` | borrado |
| `resend` | `resend:n` | reenvío |
| `event` | `event:n` | eventos |
| `contact` | `contact:n` | contactos |
| `button` | `button:n` | botones de mensaje |
| `streamvideo` | `streamvideo:n` | vídeo en streaming (SFU) |
| `refreshToDusId` | `refresht:n` | refresco del toDusId |

Namespaces estándar usados: `urn:xmpp:blocking`, `urn:xmpp:delay`,
`urn:xmpp:forward:0`, `urn:xmpp:ping`, `jabber.org/protocol/{muc,muc#admin,
muc#owner,muc#request,muc#rooms,muc#user,rsm,xdata-layout,xdata-validate,
xhtml-im,disco#info,disco#items,disco#publish,chatstates,compress,iq-register}`,
`http://etherx.jabber.org/streams`.

## Extensión propia de la biblioteca: `toDusSM`

La app **parchea un plugin de Smack propio** (`org.jivesoftware.smack.toDusSM`,
3 clases: `SMStanza`, `SMFilter`, `SMParseUtil`) que implementa un
*Stream Management* **no estándar**:

```xml
<tdack mi='<messageId>' o='<origin>'/>
```

* `SMStanza.ELEMENT = "tdack"`, atributo `msgId` (serializado como `mi`).
* `SMFilter.accept()` acepta las stanzas cuyo XML empieza por `<tdack`.
* *Inferido*: `mi` = message id y `o` = origen de la confirmación; es el
  mecanismo de acuse de recibo a nivel de stream, propio de ToDus (el SM
  estándar de XMPP usa `<r/>` y `<a h='N'/>`).

## Gestores y eventos

**Managers** (`cu.todus.android.xmpp.managers`): `BlocksManager`, `CallManager`,
`ChannelManager`, `GroupsManager`, `LastManager`, `LocationManager`,
`MessageManager`, `PresenceManager`, `PrivacyManager`, `ProfileManager`,
`RosterManager`, `StatusManager`, `UrlManager`.

**Dispatchers** (suscriben a los eventos de Smack): `OwnerDeleteMessageManager`,
`OwnerMarkersManager`, `OwnerMessageManager`, `OwnerReactionManager`,
`OwnerStatusManager`.

## Datos derivados de la capa XMPP

- **Fragmentación**: tabla Room `message_chunks(originalMsgId, chunkIndex,
  textContent)` + extensión `msg:fragment:n` → los mensajes largos se trocean
  y se reassemblan en el cliente.
- **Canales y grupos**: dos conceptos distintos en el protocolo.
  - *MUC* (XEP-0045) vía `ProtoMuc*` (protobuf): `mucId`, `version`, `aff`,
    `config`, `admins`, `roomId`.
  - *Channels* (propios de ToDus) vía `Channels*`: `channelUsername`,
    `userDomain`, `config`, `verAff` y `CheckChannelsVersion` (canales
    versionados con control de versión por usuario).
- **Archivo de mensajes (MAM, XEP-0313)**: `ProtoMamMessageRequest{ids}` /
  `ProtoMamMessageResponse{messages{id, xml, from}}` — el `xml` es el
 *elemento XMPP original serializado.
- **Estado/marcadores**: `OwnerMarkersManager` + tabla `marker(uid, msg, room,
  marker, createdAt)` → confirmación de lectura al estilo XEP-0333.
- **Perfil público**: `chat.todus.cu/card/`, `chat.todus.cu/social-profile/`,
  `chat.todus.cu/chat-profile/` (URLs observadas en el DEX).
- **Cifrado**: **no** es OMEMO (0 clases `eu.siacs.*`/`omemo`). Hay Tink
  (69 clases `com.google.crypto.tink`) presente, así que el cifrado del
  *payload* es plausible, pero la clave y el esquema concretos **no están
  localizados** → *pendiente*.
