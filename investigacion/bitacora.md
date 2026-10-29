# Bitácora de la conversación

Registro cronológico. Una entrada por sesión o avance relevante.

---

## 2026-10-20 — Sesión 1: identificación e instalación de REA

1. **Pregunta inicial:** el usuario preguntó si conozco la herramienta "REA".
   La búsqueda web vía el proveedor MCP falló (HTTP 403), así que se buscó
   por DuckDuckGo (`webfetch`) con éxito.

2. **Identificación:** REA = **Reverse Engineer Anything**.
   - Sitio oficial: https://rea.tools
   - Repo: https://github.com/morluto/rea (MIT, ~129k stars, v6.4.0 oct-2026
     al momento de la búsqueda; npm entregó `6.7.0` al instalar)
   - Conecta agentes de código a herramientas de análisis vía MCP/CLI.

3. **Decisión del usuario:** conectar REA a OpenCode por MCP.

4. **Ejecución:**
   - `node -v` → v24.19.0 (cumple el requisito 24.11+ de REA).
   - `rea setup --client opencode --dry-run` → plan de 2 acciones:
     actualizar `opencode.jsonc` + instalar skill. Se revisó antes de aplicar.
   - `rea setup --client opencode --yes` → `status: "ready"`,
     acciones aplicadas: `configured_opencode`, `installed_skill`.
   - `doctor --client opencode --json` → `"healthy": true`.

5. **Resultado:** servidor MCP `rea` añadido a la config global de OpenCode;
   backup creado; skill v42 con 207 herramientas instalada.

6. **Limitación detectada:** ningún motor de análisis externo instalado
   (Ghidra/Hopper/IDA/JADX). REA queda funcional para JS/Electron y
   binarios solo si se instala un motor.

7. **Hallazgo lateral:** hay un `cu.todus.android-v30102.apk` en la carpeta
   de trabajo, sugerencia de que el objetivo de ingeniería inversa es esa app.

8. **Pendiente inmediato:** reiniciar OpenCode para que el servidor MCP
   cargue y sus herramientas estén disponibles en la sesión.

---

## 2026-10-20 — Sesión 2: publicación del repositorio en GitHub

1. **Petición del usuario:** subir la carpeta `Ing Inversa` a un proyecto
   nuevo de GitHub.

2. **Estado previo detectado:** `git` 2.55.0 instalado; **no** había `gh`
   CLI, **no** había `credential.helper`, **no** había `winget`. Identidad
   git ya global: `michel3006 / michelcry300@gmail.com`.

3. **Decisiones del usuario (preguntadas y respondidas):**
   - APK de 71.5 MB → **incluirlo** en el repo.
   - Visibilidad → **privado**.
   - Nombre → `ing-inversa`.

4. **Autenticación:** el usuario proporcionó un PAT (`ghp_...`). Verificado
   con `GET /user` → login `Michel3006`, id 216888597.

5. **Hallazgo:** al intentar crear el repo con la API devolvió **422
   "name already exists"**. Investigando, el repo **`Michel3006/Ing-Inversa`
   ya existía**, creado por el usuario desde la web de GitHub (committer
   `web-flow`) con un `README.md` automático de 13 bytes, en
   `2026-10-20T16:10:04Z`. No se creó otro.

6. **Problemas técnicos resueltos:**
   - La API devolvía **400 "Problems parsing JSON"**: PowerShell 5.1
     descolocaba las comillas/acentos al pasárselos a `curl.exe` nativo.
     Solución: escribir el body en un temporal **UTF-8 sin BOM** y usar
     `--data-binary @archivo`.
   - Historias **no relacionadas** (repo remoto con "Initial commit",
     local con 1 commit). Solución: `git fetch` + `git rebase origin/main`
     → historia lineal, sin merge commit y **sin perder el README**.
   - Autenticación sin credential helper: helper inline que lee
     `$GH_TOKEN` del entorno. **El token no quedó escrito en ningún archivo**
     (ni en `.git/config`, ni en disco).

7. **Resultado verificado por API:**
   - Repo: `Michel3006/Ing-Inversa`, **privado**.
   - Historia lineal: `3ccb69f` (Initial commit) → `396abde` (nuestro).
   - Contenido remoto: `README.md` (13 B), `cu.todus.android-v30102.apk`
     (71 554 413 B), `investigacion/`.
   - Aviso remoto esperado: GH001, el APK supera los 50 MB recomendados
     por GitHub (límite duro 100 MB, así que sí entra).

8. **Pendiente de seguridad:** el PAT fue proporcionado en texto plano en
   el chat → **debe revocarse/rotarse**. No debe reutilizarse.

---

## 2026-10-20 — Sesión 3: instalación de REA en el contenedor Linux y primer análisis del APK

1. **Petición del usuario:** actualizar el proyecto (había 2 commits nuevos en
   `origin/main`) y luego confirmar qué es REA e instalarla.

2. **Actualización del repo:** `git fetch` + `git pull --ff-only` →
   `3ccb69f..2227c53`. Entraron `investigacion/bitacora.md`,
   `investigacion/notas.md` y el APK (71 554 413 B). Árbol limpio, sin
   conflictos.

3. **Hallazgo clave del entorno:** esta sesión corre en un **contenedor Linux**
   (`/workspaces/Ing-Inversa`, usuario `codespace`), **no** en la máquina
   Windows donde se instaló REA en la Sesión 1. Allá no había nada: sin CLI
   `rea`, sin `rea-agents` global, sin `opencode.json`, sin skill y sin
   servidor MCP. Tratarlo como un entorno separado evitó confundir la
   instalación de la Sesión 1 con la de aquí.

4. **Verificación de la documentación oficial** (rea.tools + repo
   `morluto/rea` + registro npm): REA = "Reverse Engineer Anything", MIT,
   `rea-agents@6.7.0` sigue siendo la última versión publicada. El repo tiene
   **345.3k stars** (las notas decían ~129k) y 3.880 commits, y su tabla de
   capacidades es hoy mucho más amplia (WASM, EVM, SQLite, Godot, IL2CPP,
   Go/Rust, Frida, qlog, pcap, HAR/SAZ…).

5. **Instalación ejecutada:**
   - `setup --client opencode --dry-run` → 2 acciones planned
     (registro MCP + skill). Revisado antes de aplicar.
   - `setup --client opencode --yes` → `status: "ready"`, aplicadas
     `configured_opencode` + `installed_skill`.
   - `npm install -g rea-agents@6.7.0` → CLI `rea` en
     `/home/codespace/nvm/current/bin/rea`.
   - JAR de JADX `jadx-headless-mcp-0.7.1-all.jar` (42 619 858 B) descargado a
     `~/.local/share/rea/android/` y **SHA-256 verificado** contra el digest
     pineado por REA → `OK`.

6. **Primer fallo y su causa real:** la llamada MCP `inspect_android_package`
   devolvió `capability_unavailable` / `unsupported_provider` con
   `jar_configured: false`. Causa: el proceso del servidor MCP se había
   arrancado **sin** `REA_JADX_MCP_JAR`; la variable estaba exportada en la
   shell, no en el entorno del servidor MCP. No era un problema de versión ni
   de JDK.

7. **Solución:** registrar el `environment` del servidor MCP en
   `~/.config/opencode/opencode.json` (`REA_JADX_MCP_JAR`, `JAVA_HOME`,
   `REA_JADX_OPERATION_TIMEOUT_MS=600000`, `REA_JADX_HEAP_MIB=8192`) y exportar
   lo mismo en `~/.bashrc` y `~/.profile`. OpenCode recargó la config y
   reinició el servidor MCP en la misma sesión; la llamada pasó.

8. **Verificación funcional real** (no solo `doctor`): `doctor` dio
   `healthy: true` con `jadx` `configured: true` / `available: true`
   (clasificación `config_drift`, que solo significa "JAR validado", no "el
   APK cabe en memoria"). Y `inspect_android_package` sobre el APK real
   devolvió evidencia completa.

9. **Resultado del primer análisis (ver notas §7):** paquete
   `cu.todus.android`, versionName 2.1.2, versionCode 30102, minSdk 23,
   targetSdk 36, 46 permisos, 35 057 clases, 1 877 recursos, manifest
   completo. APK SHA-256 `8da7f305e9763e542b087935714b726fafa3e26105e9e6f96261074c56461ce1`.

10. **Premisa registrada:** el códec de video no está en la capa Java; vive en
    las `.so` nativas del APK, que exigen Ghidra/IDA. Para eso REA necesita un
    motor nativo, hoy ausente.

11. **Documentación actualizada:** `investigacion/notas.md` (tabla de REA en
    Linux, capacidades actualizadas, §7 con el análisis, preguntas abiertas,
    riesgos) y esta bitácora.

12. **Pendiente de seguridad (de la Sesión 2):** el PAT sigue revocado por
    el usuario; sin token no se puede hacer `push` de este commit.

---

## 2026-10-21 — Sesión 4: verificación en vivo de la API reconstruida

1. **Petición del usuario:** actualizar el repositorio y **probar la API** de
   ToDus contra los servidores reales.

2. **Actualización del repo:** `git fetch` + `git pull --ff-only` →
   `2227c53..17d6b01` (11 commits). Entró todo `investigacion/apk/` (informe,
   inventario, XMPP, cliente Python, herramientas). Árbol limpio, sin
   conflictos.

3. **Entorno de trabajo:** esta sesión corre en **Windows** (PowerShell 5.1,
   Python 3.12.10), no en el contenedor Linux de la Sesión 3. Faltaban
   `protobuf` y `requests` → instalados (`protobuf 7.36.2`, `requests 2.34.2`).

4. **Pruebas offline:** 5 REST + 19 XMPP, todas en verde. Sirvieron de línea
   base antes de tocar nada.

5. **Bug 1 — URLs absolutas concatenadas.** `_call()` pegaba siempre la URL
   base, así que las dos operaciones con `@Url` completo de Retrofit
   (`api.apklis.cu/v1/application/338/` y `api.apklis.cu/v2/release/get_url/`)
   salían como `https://client.todus.cu/https://api.apklis.cu/...` →
   `HTTP 400 InvalidBucketName`. Corregido: si la ruta de la operación ya es
   una URL, se usa tal cual. Regresión cubierta con un test nuevo.

6. **Verificación contra producción** (solo lectura; ver
   `investigacion/apk/informe.md` §11.1 para la tabla completa):
   - `api.apklis.cu` → **200**, JSON de 9 126 B.
   - `auth.todus.cu/v2/auth/token` → el servidor **decodifica bien** el
     protobuf reconstruido (responde `sql: no rows in result set` para un
     usuario inexistente), lo que valida `ProtoUserAuthTokenRequest`.
   - `auth.todus.cu/v2/auth/check` → `bad-proto-payload`: el mensaje
     reconstruido no es el que espera el servidor.
   - `todus.cu/catalog/version` → `401 missing-authorization-header` sin token.
   - `client.todus.cu/*` → es un **bucket S3** y pide firma AWS4-HMAC-SHA256.
   - `stickers.todus.cu/v1/stickers/*` → **404**: ese host es un bucket S3 puro.
   - `im.todus.cu:5222` → TLS 1.3, servidor **ejabberd**.
   - `*-test.todus.cu` → **inaccesibles**: no hay entorno de pruebas.

7. **Bug 2 — la cabecera `Authorization`.** Con `Authorization: Bearer <jwt>`
   el catálogo responde `401 {"err":"invalid token"}`; con el **JWT crudo**
   responde `200 {"version":1}`. La APK contiene el literal `"Bearer "`, pero
   los servicios reales solo aceptan el token sin prefijo. Corregido en el
   cliente y en el test correspondiente.

8. **Bug 3 — el parser XMPP no contaba los elementos auto-cerrados.**
   `tag.rstrip().endswith("/")` nunca es cierto para `<x/>` (acaba en `>`),
   así que la profundidad de anidamiento crecía de más y **ninguna stanza con
   un hijo vacío se parseaba jamás**. Corregido a `endswith("/>")`.

9. **Bug 4 — namespaces, elementos y atributos abreviados.** El servidor
   (ejabberd) responde `xmlns:stream='x1'`, `xmlns='jc'`, `xmlns='x2'`
   (SASL), `x4` (bind), `x5` (sesión); elementos `<es>`, `<e>`, `<b1>`, `<s1>`,
   `<or>`; y atributos de stanza `t` (type), `i` (id), `f` (from). El parser
   no entendía ninguno de los tres. Añadida la traducción en el parser, con
   cuidado de **no** renombrar `i`/`s` dentro de las extensiones de ToDus
   (donde significan id y tamaño).

10. **Bug 5 — reinicio del stream tras el login.** Tras `<ok/>` el servidor
    manda una cabecera `<stream:stream>` nueva; el parser la tomaba por una
    etiqueta normal y se quedaba esperando para siempre. Ahora la reconoce y
    reinicia el estado.

11. **Bug 6 — el bind siempre en modo SASL2.** El cliente mandaba
    `<bind xmlns='urn:xmpp:bind:0'/>` siempre; `im.todus.cu` solo anuncia el
    bind clásico (`urn:ietf:params:xml:ns:xmpp-bind`) y **no responde nada**
    al bind de SASL2. Ahora se elige el modo según lo que anuncie el servidor
    y, si no, se usa el `<iq>` heredado.

12. **TLS directo en vez de STARTTLS.** En claro, el servidor responde con un
    *alert* TLS `handshake_failure` y cierra. El cliente usaba STARTTLS por
    defecto → ahora `tls_mode="direct"` (sigue soportando STARTTLS si el
    servidor lo ofrece).

13. **La contraseña del usuario no hace falta.** El servidor **no valida** la
    contraseña: `POST v2/auth/token` devolvió token válido con tres
    contraseñas distintas (incluida la vacía) para la cuenta probada. Solo
    importa que el usuario exista; y el error `sql: no rows in result set`
    **enumera** qué números están registrados. Hallazgo de seguridad
    documentado en el informe (§11, punto 6).

14. **Formato del identificador.** El `username` es el **código de país sin
    `+` + teléfono** (Cuba: `53` + 8 dígitos = 10). Con el número solo el
    servidor responde `no rows in result set`. El JWT lleva
    `{"exp":…, "toDusId":"<alias>", "username":"<53+numero>"}`.

15. **Cómo entra en el XMPP.** `im.todus.cu` ofrece `PLAIN` y `X-OAUTH2`. Lo
    que funciona es **SASL PLAIN con el JWT de la API como contraseña**
    (`authcid` = `53+telefono`). Con `X-OAUTH2` → `Invalid token`; con
    `PLAIN` y la contraseña del usuario → `Invalid username or password`.
    Sesión completa lograda: `JID = <53+telefono>@im.todus.cu/todus`, y el
    roster responde.

16. **Pruebas nuevas:** 7 tests de regresión (URL absoluta, cabecera sin
    `Bearer`, hosts de `linkedDevice`/`certified`, auto-cerrados, namespaces
    abreviados, reinicio de stream, atributos de stanza, atributos de las
    extensiones). Suite completa en verde: **7 REST + 24 XMPP**.

17. **Scripts nuevos** (para repetir las pruebas):
    `test_real.py` (endpoints públicos), `test_real_auth.py` (con sesión),
    `test_real_xmpp.py` (XMPP), `test_formato_usuario.py` (deduce el formato
    del identificador). Ninguno guarda credenciales en el repo: se pasan por
    argumentos o por `TODUS_USER`/`TODUS_PASS`.

18. **Documentación actualizada:** `investigacion/apk/informe.md` (nueva
    §11.1 con la tabla de verificación en vivo, correcciones a §6.1 y §7, y
    tres hallazgos de seguridad nuevos), esta bitácora y el README del
    cliente.

19. **Mensajería de texto probada de extremo a extremo** (petición del
    usuario). Como no había terceros disponibles, se cerró el circuito con
    **dos sesiones XMPP de la misma cuenta** (recursos `todus-a` y
    `todus-b`): B envía, A recibe. Salieron **tres bugs más** del cliente:

    1. **El texto se enviaba fuera de `<body>`** (como contenido suelto de la
       stanza). El servidor lo aceptaba y acusaba con `tdack`, pero **no
       entregaba el texto** al destinatario. Ahora siempre va en `<body>`.
    2. **Las stanzas llegan abreviadas**: `<m>` = `<message>`, `<p>` =
       `<presence>`, y el cuerpo como `<b>` (no `<body>`). El parser traducía
       namespaces y atributos, pero no los *nombres de elemento* de las
       stanzas: `parse_message` no encontraba el texto. Añadidos
       `STANZA_ABBREV` y `BODY_ABBREV`, más el atributo `o` = `to`.
    3. **La presencia se rechazaba** con `<show>online</show>`:
       `bad-request: Bad value of cdata in tag <show/>`. El servidor solo
       acepta ciertos valores de `<show>` (`chat` sí). No es un bug del
       cliente, es política del servidor; documentado.

    Resultado: envío confirmado con `tdack`, recepción con el texto íntegro
    y **troceado verificado** (9 003 bytes → 2 fragmentos con
    `msg:fragment:n` → reensamblado idéntico al original).

20. **Pruebas nuevas:** 3 más de regresión (stanzas abreviadas, cuerpo
    normalizado, texto siempre dentro de `<body>`). Suite completa en verde:
    **7 REST + 27 XMPP**.

21. **Scripts nuevos:** `test_mensaje.py` (envía y escucha, para probar con
    un tercero o desde la app) y `test_mensaje_loopback.py` (bucle de dos
    sesiones, automático).

22. **Mensajería entre dos cuentas distintas** (petición del usuario). Con la
    cuenta `5358548838` (la segunda del usuario):
    - El `username` es **el teléfono con el código de país** (`53` + 8
      dígitos). Con el número suelto el servidor dice `no rows in result
      set`; con `+53`/`0053`/sufijo `@todus.cu` tampoco.
    - Login de `5358548838` → token de 197 chars, claims
      `toDusId=fc8234c133e543808a1ef390c193f9d2` (alias **generado por el
      servidor**, no elegido), `username=5358548838`. Sesión XMPP:
      `5358548838@im.todus.cu/todus`.
    - Mensaje `5358799457` → `5358548838`: **entregado**, texto íntegro y
      `tdack` de vuelta. Sentido contrario (`5358548838` → `5358799457`):
      **entregado** también.
    - Se observó la presencia de la **app Android** de `5358799457`
      conectada a la vez (recurso `…_Android`), conviviendo con la sesión
      del script sin interferencias.

23. **Corrección de semántica:** `todus.user_jid()` tomaba el `toDusId`
    (alias) como argumento, pero el localpart del JID es el **`username`**.
    Verificado: un mensaje al alias se acepta **sin error y no se entrega
    nunca**. Documentado en `todus.py` y en el §9 del informe.

24. **Script nuevo:** `test_mensaje_2cuentas.py` (mensaje entre dos
    cuentas, con traza completa de lo que llega a cada lado).

25. **Mensajes reales de la app recibidos por el script.** Escuchando la
    cuenta `5358548838` con la app del teléfono abierta, llegaron **5
    mensajes de texto** (`Jshhshsjsidhwhwt`, `Jdhhshsgwgw`, `Uwjejbehhdhwhw`,
    `Jdjejehhsgstqtrq`, `Biuvuuvyfyffufgi`) intercalados con **4 stanzas de
    estado** sin texto. Todos con `from` = recurso `…_Android` de la cuenta
    `5358799457`.

26. **Identificados los dos elementos que la app manda con cada mensaje.**
    Mapeando 24 namespaces candidatos en una sola stanza (el servidor los
    reescribe al abreviar) se sacaron:
    - `<x8:k/>` → **`<markable/>` de `urn:xmpp:chat-markers:0`** (XEP-0333).
      Se confirmó reenviando `<markable/>`, `<received/>` y `<displayed/>`:
      el servidor los devuelve como `x8:k`, `x8:rd` y `x8:dd`.
    - `<uc1:csp/>` → estado "escribiendo" de un namespace **propio de ToDus**
      (la APK trae `ChatStateExtensionToDus`; strings `csp`/`csr` en
      `classes3.dex`). El namespace exacto sigue **sin identificar**: no es
      `urn:xmpp:chatstates` ni `http://jabber.org/protocol/chatstates` ni
      ninguna de las 7 variantes `todus:*` probadas.

27. **Implementadas las marcas de chat (XEP-0333)** en el cliente
    (`todus.chat_marker()` y `TodusXMPPClient.send_chat_marker()`), porque
    la app marca los mensajes como leídos con `<displayed/>` y sin eso el
    emisor nunca ve el doble tick. Parser actualizado para `x8`/`k`/`rd`/`dd`.

28. **Observación pendiente:** un mensaje enviado por el script a
    `5358799457` **no llegó a la app**, aunque la app estaba conectada
    (su presencia se vio) y el servidor sí lo entregó a la sesión del
    script. Hipótesis: el servidor reparte **un solo destino** por mensaje.
    Queda pendiente mandarlo al JID completo con el recurso de la app
    (`…/415bb0dc12c9ca21bbacba7def4f8297_Android`).

29. **Pruebas nuevas:** 2 más (marca de chat XEP-0333 y su forma abreviada).
    Suite completa en verde: **7 REST + 29 XMPP**.

30. **Script nuevo:** `test_escuchar.py` (escucha mensajes, responde con
    `tdack` y con la marca de leído).

---

## 2026-10-24 — Sesión 5: límites de texto, consumo de datos y video

1. **Límite de texto (bytes, no caracteres).** `MAX_MSG_TEXT_SIZE` = 8 192
   **bytes** UTF-8 por stanza y `MAX_MESSAGE_LENGTH` = 65 536 bytes por
   mensaje lógico. Con `ñ`/acentos (2 B) o emoji (hasta 4 B) entran menos
   caracteres. El cliente trocea por 8 192 B con `msg:fragment:n`;
   `send_text` **no** aplica el tope de 65 536. En ASCII: 8 192 car./trozo y
   65 536 por mensaje.
   - Teóricamente se pueden enviar 4 000 000 de caracteres repartidos en
     ~62 mensajes de 65 536 B (o ~489 stanzas de 8 192 B): el límite aplica
     **por mensaje**, no al total de la conversación.

2. **Consumo de datos en Cuba (ETECSA, consultado 2026-10-24).** La
   mensajería instantánea de **texto** de toDus se mantiene **libre de
   costo**; otros servicios (fotos, notas de voz, archivos, video, llamadas)
   y el correo Nauta **sí** descuentan del Plan toDus / plan de datos.
   Fuentes: Cubadebate (2026-04-01), FAQ de toDus y ETECSA. Matiz: el
   zero-rating va por el tráfico hacia los servidores de toDus; con un
   cliente no oficial no está garantizado y ETECSA advierte que puede
   limitar la cuenta por su uso.

3. **Video: stanza verificada en vivo, sin gastar datos de video.**
   - **Offline:** `ext_video()` genera los 10 atributos exactos; 
     `parse_message` expone `extensions['video']`; los IQ `todus:purl` /
     `todus:gurl` aceptan `type='video'`. Añadidas **4 pruebas** a
     `xmpp/test_xmpp.py` (suite offline en verde: **7 REST + 33 XMPP**).
   - **En vivo:** `5358548838` → `5358799457` con una URL **falsa** (sin
     subir archivo). El destino recibió el texto de control y la stanza de
     video con todos los metadatos intactos (`i, mi, url, n, s, d, w, he,
     h, tnail`). No se gastó dato de video.
   - **Pendiente:** subida real a S3 (PUT), descarga y métodos de alto nivel
     `send_video()` / recepción del archivo. Hoy solo viaja la metadata.

4. **Scripts nuevos:** `xmpp/test_xmpp.py` (+4 pruebas de video) y
   `test_video_2cuentas.py` (prueba real con URL falsa). El cliente REST
   necesita `protobuf` y `requests`; la prueba se ejecutó en un venv
   temporal (`/tmp/opencode/venv`).
