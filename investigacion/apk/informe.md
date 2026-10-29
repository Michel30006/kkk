# Ingeniería inversa de ToDus (Android) — Informe

**Artefacto:** `cu.todus.android-v30102.apk` (ToDus 2.1.2, versionCode 30102)
**Fecha del análisis:** 2026-10-20
**Herramientas:** REA 6.7.0 (MCP + CLI), jadx-headless-mcp 0.7.1, apktool 3.0.3,
JADX 1.5.6, Ghidra 12.1.4, readelf/objdump, scripts propios en `herramientas/`.

> **Regla de este informe:** cada hecho va etiquetado como *observado* (evidencia
> directa del artefacto), *inferido* (deducción razonada) o *pendiente*. No se
> afirma comportamiento de runtime a partir de análisis estático.

---

## 1. Resumen ejecutivo

ToDus es una app de mensajería cifrada (XMPP + WebRTC) con un cliente Android
en Kotlin/Compose. Su arquitectura es de **dos planos**:

| Plano | Transporte | Contenido | Autenticación |
|---|---|---|---|
| **Control/API** | HTTPS REST + protobuf (`application/x-protobuf`) | registro, token, perfil, catálogo, stickers, reportes, dispositivos | JWT crudo en `Authorization` + credenciales protobuf |
| **Mensajería** | XMPP (TCP) vía Smack | mensajes, canales, presencia, MAM, llamadas | SASL PLAIN con **el JWT de la API como contraseña** + OMEMO ausente |

Hallazgos principales:

1. **API REST documentada**: 33 operaciones con verbo, ruta y tipo de parámetro
   **leídos del `RequestFactory` de Retrofit incluido en la propia APK** (no
   inferidos). Ver `api/superficie-http.md`.
2. **Esquema protobuf reconstruido**: 34 mensajes / 78 campos, reconstruido
   desde el código generado (protobuf-lite no embebe descriptor) y **validado
   compilando con `protoc`**. Ver `api/ApiProto.reconstruido.proto`.
3. **Esquema de base de datos local completo**: 18 tablas Room con columnas,
   claves foráneas e índices. Ver `db/esquema-room.sql.md`.
4. **Cifrado de llamada**: `libjingle_peerconnection_so.so` (WebRTC) expone
   `FrameCryptorFactory` → cifrado de *frames* en cliente (insertable streams).
5. **No hay códecs propios**: el vídeo usa la pila de plataforma Android; el
   `libjingle` es el motor de llamada, no un codificador propio.
6. **La compresión de la app es de imágenes** (`cu.z17.compress`), no de vídeo;
   el vídeo se procesa con `androidx.media3.transformer` en el editor.

---

## 2. Identidad del artefacto

| Atributo | Valor | Evidencia |
|---|---|---|
| SHA-256 APK | `8da7f305e9763e542b087935714b726fafa3e26105e9e6f96261074c56461ce1` | REA `inspect_android_package` |
| Paquete | `cu.todus.android` | manifest decodificado |
| `versionName` / `versionCode` | 2.1.2 / 30102 | REA |
| `minSdkVersion` / `targetSdkVersion` | 23 (Android 6.0) / 36 | REA |
| Permisos declarados | 46 | REA |
| Clases / recursos | 35 057 / 1 877 | REA (`coverage: complete`) |
| `Application` | `cu.todus.android.ToDusApp` | manifest |
| `allowBackup` | `false` | manifest |
| `largeHeap` | `true` | manifest |
| `extractNativeLibs` | `false` (APK sin comprimir libs) | manifest |

### Firma

```
Owner:    CN=ToDus, OU=ToDus Development Group, O=UCI, L=La Habana, ST=La Habana, C=CU
Válido:   2017-12-20 → 2042-12-14   (SHA256withRSA, RSA 2048)
SHA-256:  C0:A5:3E:EB:C2:DB:91:45:35:14:9B:BB:53:16:2D:DC:E4:A8:CA:E2:51:F3:DA:28:96:D2:0C:27:F6:B4:9C:E9
```

*Observado* con `keytool -printcert` sobre `META-INF/CERT.RSA`.
**Pendiente:** el APK usa firma v1 (JAR) — la verificación de firma completa
no la realiza REA (`signature_verification: "not_performed"`).

---

## 3. Estructura del contenedor

| Categoría | Archivos | Bytes |
|---|---:|---:|
| Contenedor raíz | 1 | 71 554 413 |
| **DEX (bytecode)** | **7** | **65 111 524 (62,1 MiB)** |
| **Bibliotecas nativas** | **20** | **43 497 288 (41,5 MiB)** |
| Recursos compilados (`res/`) | 1 471 | 5 039 091 |
| `resources.arsc` + manifest | 2 | 2 467 876 |
| Assets | 80 | 1 006 960 |
| `META-INF` (firma + `.version`) | 245 | 673 462 |
| Kotlin metadata | 8 | 53 396 |

Listado completo con SHA-256 por archivo: `inventario/archivos.tsv` (1 878 entradas).
El grafo de artefacto de REA da `source_complete: true`, 0 contradicciones de integridad.

### DEX (multidex)

`classes.dex` (10,7 MB) · `classes2.dex` (0,59) · `classes3.dex` (20,4) ·
`classes4.dex` (9,0) · `classes5.dex` (8,9) · `classes6.dex` (9,8) ·
`classes7.dex` (5,6). 7 DEX → límite de 64K métodos casi seguro alcanzado.

### Bibliotecas nativas (4 ABIs: arm64-v8a, armeabi-v7a, x86, x86_64)

| Biblioteca | arm64 (bytes) | Función |
|---|---:|---|
| `libjingle_peerconnection_so.so` | 11 541 096 | **WebRTC** (motor de llamadas) |
| `libimage_processing_util_jni.so` | 32 528 | CameraX / procesamiento de imagen |
| `libandroidx.graphics.path.so` | 10 096 | AndroidX Graphics |
| `libdatastore_shared_counter.so` | 7 112 | DataStore multiproceso |
| `libsurface_util_jni.so` | 4 896 | CameraX surface |

*Observado:* los símbolos exportados son `Java_org_webrtc_*`; entre ellos
`FrameCryptorFactory_nativeCreateFrameCryptorForRtpSender/Receiver`,
`FrameCryptorKeyProvider_nativeExportKey/nativeRatchetKey` → **cifrado de
frames RTP en el cliente** (insertable streams, sin SFU con acceso al medio).

**Análisis nativo con Ghidra 12.1.4** (variante x86_64 de la misma
biblioteca; el ELF arm64 no se puede decompilar en un host Linux x64 porque
Ghidra no incluye el *sleigh* AArch64 para ese host):

| Métrica | Valor |
|---|---|
| Formato | ELF x86_64, `image_base` `0x100000` |
| Segmentos | 30 |
| **Funciones (procedures)** | **35 943** |
| **Strings** | **11 427** |
| Evidencia REA | `ev_79d1a4b93500eb591c775b67f894eb931f32eccb1bbef0403abc76f6427ad369` |

Archivo: `evidencia/analisis-nativo-libjingle-x86_64.json`.

---

## 4. Pila tecnológica

**App:** Kotlin + Jetpack Compose (4 637 clases en `uiCompose`), Dagger/Hilt
(`di/`: `RetrofitModule`, `DatabaseModule`, `MediaModule`, `PreferencesModule`),
Room, Retrofit + OkHttp + Protobuf-lite, Smack (XMPP) + jxmpp, Google WebRTC,
media3/ExoPlayer + Transformer, CameraX, Sentry, Firebase (analytics/FCM),
S3 (Subdir/almacenamiento de archivos), OkHttp con `PublicSuffixDatabase`.

**Servidores de terceros embebidos en el DEX:** OpenStreetMap, OpenTopoMap,
USGS NationalMap, Wikimedia, Cloudmade, ChartBundle, Google Fonts,
`i02.appmifile.com`, `wms.chartbundle.com`.

**Assets notables:** `PublicSuffixDatabase.list` (OkHttp), `jlatexmath/*`
(renderizado de fórmulas LaTeX), `dexopt/baseline.prof` (baseline profile),
`org.jivesoftware.smack*` (config XMPP), `.proto` de Google (Firebase).

Versiones de librerías: `inventario/dependencias.txt` (130 entradas).

---

## 5. Planos de servicio y URLs base

*Observado* en `cu/todus/android/constants/Service.smali` (métodos `provideXUrl`).

| Servicio | Producción | Test |
|---|---|---|
| Auth (REST) | `https://auth.todus.cu/` | `https://auth-test.todus.cu/` |
| Client (REST) | `https://client.todus.cu/` | `https://client-test.todus.cu/` |
| Catalogue | `https://todus.cu/catalog/` | `http://10.37.41.159:8086/catalog/` |
| Chat (HTTP) | `https://chat.todus.cu/` | `https://chat-test.todus.cu/` |
| IM (XMPP) | `im.todus.cu` | `im-test.todus.cu` (`10.37.41.107`) |
| S3 | `https://s3.todus.cu/` | `https://s3-test.todus.cu/` |
| Stickers | `https://stickers.todus.cu/` | `https://stickers-test.todus.cu/` |
| Linked devices | `http://10.37.41.190:8086/` | idem |

Entornos (`ApplicationEnvironment`): `PROD`, `PROD_2`, `F5`, `LOCAL`.
Puertos IM declarados: `0x6dc` = **1756**, `0x1543` = **5443** (además del
5222 estándar de XMPP, presente en la configuración de Smack).

Externos: `https://api.apklis.cu/` (distribución de apps) y `https://todus.cu/`.

---

## 6. API REST

Detalle completo en **`api/superficie-http.md`** (33 operaciones).

Resumen por servicio:

| Servicio | Operaciones | Rutas |
|---|---:|---|
| `AuthService` | 6 | `v2/auth/token`, `v2/auth/check`, `v2/auth/users.register`, `v2/auth/users.reserve`, `v3/preview` |
| `CatalogueService` | 12 | `product/{id}`, `product/{todus_id}/{id}`, `shop/{limit}/{offset}`, `search/...`, `share/product/{id}`, `version` |
| `CertifiedInfoService` | 4 | `card/info/{uid}`, `info/{uid}`, `card/subscribe/{uid}`, `card/unsubscribe/{uid}` |
| `ClientService` | 4 | `todus/release/{rel,bet}/latest`, `api.apklis.cu/...` |
| `LinkedDeviceService` | 3 | `list`, `logout`, `linkDevice` (@Url dinámico) |
| `ReportService` | 1 | `v1/report` |
| `StickerService` | 2 | `v1/stickers/info/{pack_name}`, `v1/stickers/packs/{limit}/{offset}` |
| `UserService` | 1 | `v2/todus/users.me` |

**Formato de cuerpo:** `application/x-protobuf` (*observado* en
`ProtoRequestBodyConverter`). Respuestas protobuf o JSON según el
conversor (`ProtoResponseBodyConverter` / `V2` / `ProtobufConverterFactory`).

**Mapeo de verbos (evidencia dura):** las anotaciones Retrofit están
ofuscadas por R8. El `RequestFactory` de Retrofit *dentro de la APK* resuelve
`Loi/o;`→`POST`, `Loi/f;`→`GET`, `Loi/b;`→`DELETE`, `Loi/n;`→`PATCH`,
`Loi/p;`→`PUT`, `Loi/g;`→`HEAD`, `Loi/m;`→`OPTIONS`, `Loi/h;`→`@HTTP`;
y para parámetros `Loi/a;`→`@Body`, `Loi/s;`→`@Path`, `Loi/t;`→`@Query`,
`Loi/y;`→`@Url`. Verificado leyendo `smali_classes7/mi/z$a.smali`.

### 6.1 Flujo de autenticación (completo, observado)

```
POST https://auth.todus.cu/v2/auth/token
Content-Type: application/x-protobuf
body: ProtoUserAuthTokenRequest { username, password, version /* clientVersion */ }
  -> ProtoUserAuthTokenResponse { tokenString }
```

* `version` se rellena con el parámetro `clientVersion` (nombre del parámetro
  Kotlin en `AuthDataSource`).
* El token se envía en **`Authorization: <tokenString>`**, **sin** el prefijo
  `Bearer ` *(corregido tras la verificación en vivo, ver §11.1)*. La APK
  contiene el literal `"Bearer "` (`TokenAuthenticator$Companion`), pero los
  servicios reales solo aceptan el JWT crudo: con `Bearer ` el catálogo
  responde `401 {"err":"invalid token"}`.
* `User-Agent: "ToDus 2.1.2 " + <sufijo>` (*observado* en
  `TokenAuthenticator$Companion.headerWithAuth`).
* `username` = **código de país sin `+` + número de teléfono** (Cuba: `53` +
  8 dígitos). El número solo (`8 dígitos`) no existe en la base de datos del
  servidor: `POST v2/auth/token` responde `500 sql: no rows in result set`.
  El JWT lleva `{"exp":…, "toDusId":"<alias>", "username":"<53+numero>"}`.
* Renovación: `TokenAuthenticator` es un `Authenticator` de OkHttp; cuando la
  respuesta es 401 invoca `requestToken` (lambda que vuelve a pedir el token),
  obtiene un `Pair<Boolean, String>` y **reintenta** la petición con la
  cabecera `Authorization` reemplazada.
* `AuthService.tokenNotLive` es la variante "sin red" del login (misma ruta
  `v2/auth/token`, retorno directo `ProtoUserAuthTokenResponse`).
* Existe además `cu.todus.android.accountManager.AuthenticatorService`
  (Android AccountManager) y `v2/auth/check` (`ProtoAuthCheckRequest`) para
  validar credenciales.

*Pendiente:* valor exacto del sufijo del User-Agent y el cuerpo de
`v2/auth/users.register` (incluye `code`, probablemente un SMS/OTP). El
servidor **rechaza** el `ProtoAuthCheckRequest` reconstruido
(`bad-proto-payload`), así que ese mensaje está incompleto.

---

## 7. Esquema protobuf de la app

**Archivo:** `api/ApiProto.reconstruido.proto` — 34 mensajes, 78 campos,
todos con número de campo, **compila con `protoc` sin errores**.

Mensajes clave:

| Mensaje | Campos | Uso |
|---|---|---|
| `ProtoUserAuthTokenRequest` | `username`, `password`, `version` | login (POST `v2/auth/token`) |
| `ProtoUserAuthTokenResponse` | `tokenString` | token de sesión |
| `ProtoUserRegisterRequest` | `username`, `clientToken`, `code` | registro |
| `ProtoUserRegisterResponse` | `passwd`, `protoUserReserve` (roster inicial) | registro |
| `ProtoUserReserveRequest` | `username`, `clientToken` | reserva de usuario |
| `ProtoAuthCheckRequest` | `username`, `password` | comprobación de credenciales |
| `ProtoUsersMeRequest` | `userInfo` (map) | actualización de perfil |
| `ProtoMamMessageRequest/Response` | `ids`, `messages{id,xml,from}` | archivo de mensajes (MAM) |
| `ProtoMuc*` | `groups`, `mucId`, `version`, `aff`, `config`, `admins` | canales/grupos (MUC) |
| `Channels*` | `channelUsername`, `userDomain`, `message`, `config`, `verAff` | canales versionados |
| `ProtoPresignedUrlResponse` | `put`, `get` | subida directa a S3 |

*Inferido:* el token viaja como `Authorization: <tokenString>` (el campo
`AUTHORIZATION` existe en `RetrofitModule`). El prefijo quedó *confirmado en
vivo*: el token va **crudo**, sin `Bearer ` (§6.1 y §11.1).

---

## 8. Base de datos local (Room)

**Archivo:** `db/esquema-room.sql.md` (18 tablas + 8 índices), extraído de
`ToDusDatabase_Impl.createAllTables()`.

Tablas: `messages`, `rooms`, `user`, `user_room_join`, `attachments`, `marker`,
`reaction`, `status`, `heritage`, `linked_device`, `roster_change`,
`message_chunks`, `sticker`, `stickerPack`, `Contact`.
Hay **11 migraciones** (`MIGRATION_2_3` … `MIGRATION_10_11`).

El campo `rooms.secret` (INTEGER) y `rooms.config` (TEXT, JSON) indican que la
configuración de cada sala viaja cifrada/serializada dentro del propio XMPP.
*Pendiente:* formato exacto de `config` y de `rooms.secret`.

---

## 9. Capa XMPP (mensajería)

*Observado:*

- Biblioteca **Smack** (`org.jivesoftware.smack`: 243 clases) + `smack-im`,
  `smack-tcp`, `smack-extensions`; JID con `org.jxmpp.jid.Jid`.
- Extensiones propias (25 con namespace propio, tabla completa en
  `xmpp/extensiones.md`): el patrón de namespace es **`<elemento>:n`**:
  `audio:n`, `button:n`, `tcall:n`, `msg:fragment:n` (troceado de mensajes
  largos), `contact:n`, `deletedowner:n`, `deleted:n`, `edited:n`, `event:n`,
  `file:n`, `resend:n`, `gif:n`, `image:n`, `linkInfo:n`, `location:n`,
  `mention:n`, `pubsdeleted:n`, `pubedited:n`, `reaction:n`, `refresht:n`,
  `statusexistence:n`, `sticker:n`, `streamvideo:n`, `video:n`, `voice:n`.
  Cada extensión expone además atributos propios (p. ej. `tcall` lleva
  `i`, `mi`, `st`, `cid`).
- Namespaces estándar usados: `urn:xmpp:blocking`, `urn:xmpp:delay`,
  `urn:xmpp:forward:0`, `urn:xmpp:ping`, `jabber.org/protocol/{muc,mam,rsm,…}`.
- Gestores (`cu.todus.android.xmpp.managers`): `BlocksManager`, `CallManager`,
  `ChannelManager`, `GroupsManager`, `LastManager`, `LocationManager`,
  `MessageManager`, `PresenceManager`, `PrivacyManager`, `ProfileManager`,
  `RosterManager`, `StatusManager`, `UrlManager`.
- Distintores (`dispatchers`): `OwnerDeleteMessageManager`, `OwnerMarkersManager`,
  `OwnerMessageManager`, `OwnerReactionManager`, `OwnerStatusManager`.
- Configuración de sesión (`XMPPSessionConfiguration`): `xmppDomain`, `xmppHost`,
  `xmppPort`, `credentials` (usuario/contraseña), `debugMode`, `replyTimeOut`,
  `sendPresence`.
- Fragmentación: la tabla `message_chunks` (`originalMsgId`, `chunkIndex`,
  `textContent`) y la extensión `msg:fragment:n` confirman que los mensajes
  largos se trocean y se reassemblan en cliente.

*Inferido:* no hay OMEMO/Signal en la APK (0 clases `eu.siacs.*`/`omemo`); el
cifrado de mensajes no es OMEMO. **Tink no lo usa el código de la app** (las
69 clases `com.google.crypto.tink` vienen por dependencia transitiva y no se
referencian desde `cu.todus.*`): el único criptografía propia es
`cu.z17.preferences.encription.Encryption` (PBKDF2WithHmacSHA1 +
AES/CBC/PKCS5Padding + Base64), y protege **preferencias locales**, no los
mensajes. El cifrado de las **llamadas** sí está en el cliente (DTLS-SRTP +
`frame_crypto_transformer`).
*Pendiente:* si existe cifrado extremo a extremo del texto del mensaje, no
está implementado en este cliente (o se hace en el servidor).

*Pendiente:* IQs usados para MAM/versiones de canal, y el mecanismo exacto de
cifrado de payload. El namespace propio ya **sí** está resuelto: patrón
`<elemento>:n` (tabla completa en `xmpp/extensiones.md`).

### 9.1 Extensión de protocolo propia: `toDusSM`

La app **parchea un plugin de Smack propio** (`org.jivesoftware.smack.toDusSM`:
`SMStanza`, `SMFilter`, `SMParseUtil`) que implementa un *Stream Management*
**no estándar**:

```xml
<tdack mi='<messageId>' o='<origin>'/>
```

`SMStanza.ELEMENT = "tdack"` con atributo `msgId` (serializado como `mi`);
`SMFilter.accept()` acepta las stanzas cuyo XML empieza por `<tdack`.
*Inferido:* es el mecanismo de acuse de recibo a nivel de stream propio de
ToDus (el SM estándar de XMPP usa `<r/>` y `<a h='N'/>`).

Detalle completo del protocolo: `xmpp/protocolo.md`.

---

## 10. Multimedia

| Área | Implementación | Evidencia |
|---|---|---|
| Llamadas | WebRTC (`libjingle_peerconnection_so.so` + `org.webrtc`, 208 clases) | símbolos JNI `Java_org_webrtc_*` |
| **Build de WebRTC** | **build propio** (no el oficial de Google): rutas de compilación `../../../home/kanat/webrtc/src/...` | strings del `.so` |
| Transporte de medios | DTLS-SRTP con **BoringSSL** (`dtls_srtp_transport.cc`, `srtp_session.cc`, `third_party/boringssl`) | strings del `.so` |
| Cifrado de media | `frame_crypto_transformer.cc` + `FrameCryptorFactory` (export/ratchet de clave) | símbolos + strings |
| Códecs de vídeo | **libvpx** (VP8/VP9 software) + H264 con parámetros `x-google-*` | strings del `.so` |
| Códecs de audio | **Opus** con control CBR/VBR y *boost for golden frame*; G722/PCMA/PCMU | strings del `.so` |
| Control de congestión | `goog-remb`, `goog-lntf`, `x-google-start-bitrate`, `x-google-max-bitrate`, `x-google-max-quantization`, `x-google-per-layer-pli` | strings del `.so` |
| Fiabilidad | `rtx`, `red`, `ulpfec`, `useinbandfec` | strings del `.so` |
| Red | ICE completo (candidatos, TURN, `use_candidate=`) | strings del `.so` |
| Señalización | `cu.todus.android.webrtc.SignalingCommand`, `peer.StreamPeerConnection` | clases de la app |
| Reproductor | `Z17HLSVideoPlayer` (media3 ExoPlayer, HLS) | clases `cu.z17.views.videoPlayer` |
| Editor de vídeo | `videoEditor.sections.{cutter,filter,viewer}` + `MatrixTransformationFactory`, `PeriodicVignetteShaderProgram` (OpenGL ES) | clases |
| Compresión | `cu.z17.compress.Compressor` + `constraint.{Quality,Resolution,Size,Format,Destination}` → **compresión de imágenes** (bitmap + quality) | decompilado |
| Cámara | CameraX (`camera.camera2`, `camera.core`, `camera.video`) | 910 + 459 + 185 clases |
| Stickers | `v1/stickers/*` + `stickers.todus.cu` | REST + assets |

*Corrección importante frente a la hipótesis inicial:* `cu.z17.compress` es
compresión de **imágenes** (`QualityConstraint.satisfy` → `loadBitmap` +
`overWrite(quality)`), no de vídeo. El vídeo se recomprime en el editor con
`androidx.media3.transformer`. Y los códecs de vídeo/c_audio son **los de
WebRTC (libvpx/Opus/H264 de plataforma)**, no códecs propios: no hay ni
FFmpeg ni un codificador propio en la APK.

---

## 11. Hallazgos de seguridad (observados en el código)

1. **Verificación de hostname desactivada en los 8 clientes REST.**
   `RetrofitModule` crea un `OkHttpClient` por servicio (auth, catalogue,
   certified, client, linkedDevice, report, stickers, user) y en todos ellos
   el `HostnameVerifier` es una lambda que **devuelve `true` sin comprobar
   nada**:

   ```smali
   .method private static final provideClientOkHttp$lambda$0$0$0(
       Ljava/lang/String;Ljavax/net/ssl/SSLSession;)Z
       const/4 p0, 0x1
       return p0
   ```

   *Consecuencia:* cualquier certificado válido (de cualquier CA) es aceptado
   para `*.todus.cu`. Es compatible con un MITM si la cadena de confianza se
   valida contra el almacén del sistema, que en Android incluye CAs de
   terceros. Un cliente propio **debería** verificar el hostname.

2. **Certificados fijados (pinned) solo en el servicio `certified`.**
   `RetrofitModule` mantiene `certPool`, `certificated1`, `certificated2` y
   `rawCertificated` (un recurso crudo con el certificado), y expone
   `getCertPool()`. *Inferido:* ese servicio (usuarios "certificados") valida
   contra un pool de CA propio.

3. **Tokens en claro en el transporte HTTP**, mitigado por el
   `Authorization: <jwt>` (crudo, sin `Bearer`). El token se renueva solo
   cuando OkHttp recibe un 401 (`TokenAuthenticator`), lo que implica que un
   token válido en tránsito es reutilizable.

4. **Cifrado local de preferencias** con PBKDF2-HMAC-SHA1 + AES/CBC/PKCS5
   (sal e IV configurables) — bien, aunque PBKDF2-HMAC-SHA1 es anticuado
   (preferir SHA-256/Argon2).

5. **Permisos sensibles**: `READ_SMS`, `RECEIVE_SMS`, `QUERY_ALL_PACKAGES`,
   `MANAGE_EXTERNAL_STORAGE`, `READ_CALENDAR`/`WRITE_CALENDAR`,
   `ACCESS_BACKGROUND_LOCATION`.

6. **`v2/auth/token` NO valida la contraseña** *(observado en el servidor, no
   en el código)*. Con un usuario existente, `POST https://auth.todus.cu/v2/auth/token`
   devuelve un JWT válido **para cualquier contraseña**, incluida la cadena
   vacía. Comprobado con tres contraseñas distintas sobre la misma cuenta: los
   tres tokens fueron aceptados por `todus.cu/catalog/version`. Si el usuario
   no existe, la respuesta es `500 {"error":"sql: no rows in result set"}`,
   lo que además **enumera cuentas** (distingue "usuario inexistente" de
   "contraseña incorrecta"). *Consecuencia:* la contraseña de la cuenta no
   protege nada frente a un atacante que conozca el identificador; además el
   endpoint permite enumerar qué números están registrados.

7. **El token JWT viaja sin el prefijo `Bearer `** *(observado)*. La APK
   contiene la cadena literal `"Bearer "` (`TokenAuthenticator$Companion`), pero
   los servicios reales solo aceptan el JWT crudo: `Authorization: Bearer <jwt>`
   → `401 {"err":"invalid token"}`; `Authorization: <jwt>` → `200`. El informe
   §6.1 queda corregido en este punto.

8. **Identificador de cuenta = prefijo telefónico + `toDusId`** *(observado)*.
   El `username` del login es el número con el **código de país sin `+`**
   (Cuba: `53` + 8 dígitos = 10 dígitos). El JWT lleva dos claims:
   `{"exp":…, "toDusId":"<alias>", "username":"<53+numero>"}`. El alias
   (`toDusId`) es el identificador social que muestra la app y con el que se
   firman los JID (`<toDusId>@im.todus.cu`).

---

## 11.1 Verificación contra los servidores reales (2026-10-21)

El análisis de las secciones anteriores es **estático**. Esta sección recoge
lo que se comprobó **hablando con los servidores de producción** desde una IP
no cubana (VPN). Todo lo de abajo es *observado*.

| Objetivo | Resultado |
|---|---|
| `GET api.apklis.cu/v1/application/338/` | **200**, 9 126 B de JSON (id 338, 108 releases) |
| `POST auth.todus.cu/v2/auth/token` | **200**, JWT de 165 chars (usuario existente) / **500** `sql: no rows in result set` (usuario inexistente) |
| `POST auth.todus.cu/v2/auth/check` | **500** `bad-proto-payload` → el mensaje reconstruido **no** es el que espera el servidor (hueco de reconstrucción) |
| `GET todus.cu/catalog/version` | **401** `missing-authorization-header` sin token → **200** `{"version":1}` con el JWT crudo |
| `GET todus.cu/catalog/shop/5/0` | **200** `{"catalog":[]}` (con token) |
| `GET client.todus.cu/todus/release/{rel,bet}/latest` | **400** `The authorization mechanism you have provided is not supported. Please use AWS4-HMAC-SHA256` → es un **bucket S3** y pide firma SigV4, no el JWT de la app |
| `GET client.todus.cu/list` (dispositivos) | ídem (bucket S3) |
| `GET stickers.todus.cu/v1/stickers/packs/5/0` | **404** → ese host es **un bucket S3 puro** (responde `ListBucketResult` en la raíz); la ruta `v1/stickers/*` no existe tal cual |
| `im.todus.cu:5222` (XMPP) | **TLS 1.3** directo (`CN=todus.cu`), sin STARTTLS; servidor **ejabberd** (`process-one.net`) |
| `*-test.todus.cu` | **inaccesibles**: `client-test` no resuelve, `auth-test` corta la conexión → no hay entorno de pruebas utilizable |

### Lo que el servidor de mensajería obliga a corregir en el cliente

El XMPP de ToDus **no** es un XMPP estándar en los detalles sintácticos; el
servidor (ejabberd con *abbreviated namespaces*) rompe varias suposiciones
del cliente reconstruido:

1. **TLS directo, no STARTTLS**: en claro, el servidor responde con un
   *alert* TLS `handshake_failure` (22) y cierra. El cliente debe usar
   `tls_mode="direct"`.
2. **Namespaces abreviados**: la cabecera del servidor trae
   `xmlns:stream='x1'` y `xmlns='jc'`; en `<features>` usa `xmlns='x2'` para
   SASL, `x4` para bind y `x5` para sesión. Hay que traducirlos a sus URI.
3. **Elementos abreviados**: `<es>` = `<mechanisms>`, `<e>` = `<mechanism>`,
   `<b1>` = `<bind>`, `<s1>` = `<session>`, `<or>` = `<optional>`.
4. **Atributos abreviados en las stanzas**: `<iq t='result' i='bind_1'>`
   (`t`=type, `i`=id, `f`=from). Ojo: **no** se renombran dentro de las
   extensiones de ToDus, donde `i` y `s` significan id y tamaño.
5. **Éxito SASL abreviado**: `<ok/>` en vez de `<success/>`.
6. **Reinicio de stream tras el login**: el servidor manda una cabecera
   `<stream:stream …>` nueva; el parser debe reiniciarse.
7. **Bind clásico, no SASL2**: el servidor anuncia `<bind/>` del namespace
   clásico; mandar el `<bind/>` de SASL2 no produce respuesta alguna.

### Autenticación de la mensajería

`im.todus.cu` ofrece `PLAIN` y `X-OAUTH2`. Lo que funciona es:

```
SASL PLAIN   authcid = <53+telefono>      passwd = <JWT de la API>
```

Es decir, **el token de la API es la contraseña de la sesión XMPP**
(`<ok/>` y `JID = <53+telefono>@im.todus.cu/todus`). Con `X-OAUTH2` el
servidor responde `Invalid token` y con `PLAIN` + contraseña de usuario,
`Invalid username or password`.

Con la sesión abierta, el roster responde
(`<query xmlns='todus:roster:list:2'/>`, lista vacía en la cuenta probada) y
las consultas `todus:roster:version` / `todus:offline:get` devuelven
`service-unavailable: No module is handling this query` (módulos no
habilitados en ese despliegue).

### Mensajería de texto: verificada de extremo a extremo

Prueba con **dos sesiones de la misma cuenta** (recursos `todus-a` y
`todus-b`), que es un bucle completo sin depender de terceros:

```
sesión B: <message type='chat' to='<jid-a>'>  →  servidor  →  sesión A
sesión A: <message from='<jid-b>'> + <b>texto</b>  →  <tdack mi='...'/>
```

* **Envío**: el servidor responde con el acuse propio de ToDus
  `<tdack mi='msg_2'/>` (elemento `<tdack>`, namespace `todus`).
* **Recepción**: el mensaje llega con `type='c'` (chat), `from` = JID del
  emisor y `to` = JID completo del receptor.
* **Troceado**: un texto de 9 003 bytes se envía en 2 stanzas con la
  extensión `msg:fragment:n` (`msgId`, `index`, `total`); al reensamblar por
  `msgId` sale el texto **íntegro**.
* **Cuerpo abreviado**: el servidor entrega el texto como `<b>texto</b>`, no
  como `<body>`.
* **Presencia**: el servidor **rechaza** `<show>online</show>`
  (`bad-request: Bad value of cdata in tag <show/>`) y acepta
  `<show>chat</show><priority>1</priority>`; `<presence/>` y
  `<status>…</status>` también valen. Es política del servidor, no un fallo
  del cliente.
* **A quién se dirige el mensaje**: el localpart del JID es el **`username`**
  (teléfono con código de país), **no** el `toDusId` (alias). Enviado a
  `5358799457@im.todus.cu` se entrega; enviado a `michel30@im.todus.cu` (el
  alias de esa misma cuenta) el servidor lo acepta **sin error pero no lo
  entrega nunca**. Esto corrige la lectura estática del §9, que sugería
  `<toDusId>@im.todus.cu`.

### Mensajería entre dos cuentas distintas

La prueba definitiva, con **dos cuentas reales** (no un bucle de la misma
cuenta), en los dos sentidos:

| Origen | Destino | Resultado |
|---|---|---|
| `5358799457` (alias `michel30`) | `5358548838` (alias `fc8234c1…`) | ✅ entregado, texto íntegro, `tdack` de vuelta |
| `5358548838` | `5358799457` | ✅ entregado, texto íntegro, `tdack` de vuelta |

La cuenta `5358548838` tiene el `toDusId` generado `fc8234c133e543808a1ef390c193f9d2`
(el alias lo genera el servidor: no es un nombre elegido por el usuario).

Durante estas pruebas se vio además la presencia de la **app Android** de la
cuenta `5358799457` conectada a la vez, con el recurso
`415bb0dc12c9ca21bbacba7def4f8297_Android`: conviven la sesión propia y el
teléfono sin interferirse. Los mensajes que envía la app se caracterizan por
llevar **dos elementos extra**:

| Elemento recibido | Qué es |
|---|---|
| `<x8:k/>` | `<markable/>` de `urn:xmpp:chat-markers:0` (XEP-0333) |
| `<uc1:csp/>` | estado "escribiendo" de un namespace **propio de ToDus** (elemento `csp`; la APK trae `ChatStateExtensionToDus`) |

El mapeo de `x8` se obtuvo **empíricamente**: enviando una stanza con 24
namespaces candidatos, el servidor los reescribe abreviados y solo
`urn:xmpp:chat-markers:0` volvió como `x8`; reenviando `<markable/>`,
`<received/>` y `<displayed/>` volvieron como `x8:k`, `x8:rd` y `x8:dd`. El
namespace de `uc1` sigue **sin identificar** (no es `urn:xmpp:chatstates` ni
`http://jabber.org/protocol/chatstates`).

*Implicación práctica:* la app marca los mensajes como leídos con XEP-0333,
así que un cliente propio debe responder `<displayed xmlns='urn:xmpp:chat-markers:0' id='…'/>`
para que el emisor vea el doble tick (ya implementado en el cliente).

### Un mensaje del script no llegó a la app

Enviado de `5358548838` a `5358799457` con la app del teléfono conectada, el
servidor **sí** entregó el mensaje a la sesión del script, pero el mensaje
**no apareció en la app**. Dos sesiones de la misma cuenta coexistieron sin
interferirse (se vio la presencia `…_Android`). Hipótesis pendiente: el
servidor reparte **un solo destino** por mensaje y elige una de las sesiones.
Queda por probar el envío al JID completo con el recurso del dispositivo
(`5358799457@im.todus.cu/415bb0dc12c9ca21bbacba7def4f8297_Android`).

### Video: stanza verificada, archivo pendiente (2026-10-24)

- **Límites de texto:** `MAX_MSG_TEXT_SIZE` = 8 192 **bytes** UTF-8 por stanza
  y `MAX_MESSAGE_LENGTH` = 65 536 bytes por mensaje lógico. Son bytes, no
  caracteres (acentos/emoji reducen el número). El cliente trocea por 8 192 B
  con `msg:fragment:n`; no hay tope documentado para el número de mensajes de
  una conversación.
- **Stanza de video en vivo:** `5358548838` → `5358799457` con
  `<video xmlns='video:n' i='…' mi='…' url='…' n='…' s='…' d='…' w='…'
  he='…' h='…' tnail='…'/>` y una URL **falsa** (sin subir archivo). El
  destino recibió la stanza con la metadata intacta; el texto de control
  también llegó. No se gastó dato de video.
- **Pendiente:** el flujo real de archivo (PUT a S3 con la URL prefirmada de
  `todus:purl`, GET con `todus:gurl`, descarga/reensamblado). Hoy la stanza
  solo apunta a una URL; no transporta el video.
- Pruebas: 4 nuevas offline en `api/cliente/xmpp/test_xmpp.py` (suite
  **7 REST + 33 XMPP** en verde) y `api/cliente/test_video_2cuentas.py`.

---

## 12. Permisos y componentes
**46 permisos.** Los relevantes para el modelo de la API:
`INTERNET`, `ACCESS_NETWORK_STATE`, `POST_NOTIFICATIONS`,
`FOREGROUND_SERVICE*` (4 variantes), `READ_CONTACTS`/`GET_ACCOMTS`/
`AUTHENTICATE_ACCOUNTS` (`cu.todus.android.accountManager.AuthenticatorService`),
`CAMERA`/`RECORD_AUDIO`/`MODIFY_AUDIO_SETTINGS`/`BLUETOOTH*` (llamadas),
`READ_MEDIA_IMAGES`/`READ_MEDIA_VIDEO`/almacenamiento, `ACCESS_*_LOCATION`,
`QUERY_ALL_PACKAGES`, `READ_SMS`/`RECEIVE_SMS` (integración SMS), Sentry y
Firebase (`AD_ID`, `c2dm`).

**Componentes exportados (superficie de ataque):** `MainActivity` (deep links
`https://chat.todus.cu/*`), `LoadingActivity`, `StartActivity`, `AuthActivity`,
`PlayerActivity`, `CallActivity`, `AudioCallActivity`, `VideoCallActivity`,
`IntentReceiverActivity` (SEND/SEND_MULTIPLE/SENDTO sms), `AliveService`,
`DownloadOfflineService`, `DirectReplyIntent`, `CallIntentReceiver`,
`CaptureActivity` (código de barras), `FileProvider`.

El manifest decodificado completo: `manifest/AndroidManifest.decoded.xml`.

---

## 13. Limitaciones de este análisis

- **Estático**: nada se ejecutó. No hay comportamiento de runtime observado.
- **Firma** no verificada por REA (`not_performed`).
- El **namespace propio de las extensiones XMPP** no aparece como literal:
  queda pendiente de resolver en runtime o en la librería Smack.
- El **cifrado de payloads** no está localizado (Tink está presente, pero sin
 Localizar la clave ni el esquema).
- `libjingle_peerconnection_so.so` **aún no analizado con Ghidra**.
- El contenido de `rooms.config`, `rooms.secret` y `messages.payload` sigue
  siendo opaco.
- Los *oversets* (`MIGRATION_*`) no se han reconstruido.

---

## 14. Reproducibilidad

Todo lo anterior se regenera con los scripts de `herramientas/`:

| Script | Qué produce |
|---|---|
| `extraer_api.py` | `api/superficie-http.md` (verbos + rutas + params desde smali) |
| `reconstruir_proto.py` | `api/ApiProto.reconstruido.proto` (esquema protobuf) |
| `extraer_urls.py` | URLs base por servicio y entorno |
| `volcar_smali.py` | volcado de constantes por método (config embebida) |

Flujo completo:

```bash
java -jar apktool.jar d -f -o apktool-out cu.todus.android-v30102.apk
python3 herramientas/extraer_api.py       apktool-out api/superficie-http.md
python3 herramientas/reconstruir_proto.py apktool-out "cu/todus/proto/ApiProto" \
        api/ApiProto.reconstruido.proto
python3 -m grpc_tools.protoc -I api --python_out=/tmp/out api/ApiProto.reconstruido.proto
```

Evidencia cruda de REA en `evidencia/` (JSON): `inspect-artifact.json`,
`inspect-android-package.json`, `search-android-classes.json`.
