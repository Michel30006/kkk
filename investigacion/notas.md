# Notas de investigación — Compresión de video / Ingeniería inversa

Documento principal. Registra premisas verificadas, descartes, avances y
preguntas abiertas. Todo fechado.

---

## 1. Herramientas

### REA (Reverse Engineer Anything) — instalado y conectado (2026-10-20)

**Qué es (verificado en https://rea.tools y https://github.com/morluto/rea):**
toolkit de código abierto (MIT) que conecta un agente de código a herramientas
de análisis de software vía MCP o CLI, para inspeccionar programas sin su
código fuente.

- Versión instalada: `rea-agents@6.7.0` (CLI y registro MCP). Es la última
  publicada en npm (verificado 2026-10-20 vía registro npm).
- Node requerido: 22.19+ / 24.11+ / 26+. Instalado: **v24.19.0** ✓
- Registro MCP escrito en `C:\Users\pc_4\.config\opencode\opencode.jsonc`,
  servidor local `rea`:
  `cmd.exe /d /c npx -y rea-agents@6.7.0 mcp`
- Backup previo: `opencode.jsonc.rea.backup` (mismo directorio).
- Skill instalada: `C:\Users\pc_4\.agents\skills\reverse-engineer-anything`,
  versión 42, **207 herramientas MCP**, 6 prompts.
- Verificación: `doctor --client opencode` → `"healthy": true`,
  registro `state: "aligned"`.
- **Reinicio de opencode requerido** para que las herramientas REA aparezcan
  en la sesión (los MCP locales se cargan al inicio).

### REA en el contenedor Linux de trabajo (2026-10-20) — operativo

**Distinción clave:** la instalación de arriba es la de la máquina **Windows**
del usuario. Esta sesión corre en un **contenedor Linux** (`/workspaces/Ing-Inversa`,
usuario `codespace`), que es un entorno distinto: aquí se instaló REA por
separado y ya está verificado.

| Componente | Estado verificado en Linux |
|---|---|
| Paquete npm | `rea-agents@6.7.0` (npx y global) |
| CLI `rea` | ✅ `/home/codespace/nvm/current/bin/rea` (global, v6.7.0) |
| Registro MCP | ✅ `/home/codespace/.config/opencode/opencode.json`, servidor local `rea` |
| Skill | ✅ `/home/codespace/.agents/skills/reverse-engineer-anything` |
| Herramientas en la sesión | ✅ 207 (MCP `rea` conectado y verificado) |
| Node | ✅ v24.21.0 |
| JDK completo | ✅ OpenJDK 25.0.4.1 en `/home/codespace/java/current` |
| `ps` procps | ✅ procps-ng 4.0.4 (prerrequisito Linux de REA) |
| Motor JADX | ✅ `jadx-headless-mcp 0.7.1` (JAR de 42 619 858 B) |
| Ghidra / IDA / Kuna | ❌ ausentes (solo hacen falta para binarios nativos) |

- JAR de JADX en `~/.local/share/rea/android/jadx-headless-mcp-0.7.1-all.jar`,
  **SHA-256 verificado** contra el digest pineado por REA:
  `6e5eacf500b64292bfb73c49797c1958f6ee44646e43e868039ae7feb573ff75` → OK.
- Variables de entorno (registro MCP + `~/.bashrc` + `~/.profile`):
  `REA_JADX_MCP_JAR`, `JAVA_HOME`, `REA_JADX_OPERATION_TIMEOUT_MS=600000`,
  `REA_JADX_HEAP_MIB=8192`.
- `doctor --client opencode --json` → `healthy: true`; proveedor `jadx`
  `configured: true`, `available: true` (clasificación `config_drift` = solo
  que el JAR está validado, no que un APK concreto entre en memoria).
- El registro MCP necesitó reinicio para que el proceso MCP heredara las
  variables; OpenCode recargó solo y el servidor quedó operativo en la misma
  sesión.
- `frida` (dependencia opcional de REA) no quedó instalado: npm no ejecutó su
  script de instalación. No hace falta para análisis estático de APK; sí para
  captura de runtime con Frida.

**Premisa verificada (2026-10-20):** REA **no** usa el JADX estándar. Envuelve
`jadx-headless-mcp 0.7.1` (proyecto `1013503897/jadx-headless-mcp`), que el
usuario debe aportar. REA tampoco instala Java, JADX, Ghidra, IDA ni ADB.

**Capacidades declaradas por REA (documentación oficial, tabla resumida):**

| Objetivo | Motor requerido | Estado en el contenedor Linux |
|---|---|---|
| JavaScript / Electron (carpeta o `.asar`) | solo Node.js | ✅ disponible |
| APK Android (estático) | `jadx-headless-mcp 0.7.1` + JDK completo | ✅ configurado y verificado |
| Android en dispositivo | `adb` (propio del usuario) | ❌ `adb` no instalado |
| Recursos Android (Apktool) | launcher Apktool | ❌ no instalado |
| .NET (managed) | solo Node.js (estático) | ✅ disponible |
| Flutter / Go / Rust / IL2CPP / Godot / WASM | estático con Node (WASM y EVM necesitan WABT / portador local) | ✅ mayormente disponible |
| SQLite, pcap, HAR/SAZ, qlog, señales | herramientas propias del usuario (tshark, sigrok, mitmproxy…) | ❌ no instaladas |
| Binarios nativos (PE/ELF) | Ghidra, Hopper o IDA | ❌ ninguno instalado |
| Navegador / runtime | Chrome/Chromium/Edge | sin verificar |

**Premisa verificada:** el análisis de binarios nativos NO funciona sin un
motor externo (Ghidra/Hopper/IDA). El doctor lo reporta como
`missing_analysis_engine`, no como error. El análisis de JS/Electron sí
funciona solo con Node.

**Premisa verificada (2026-10-20):** REA publica soporte explícito para
OpenCode (`--client opencode`, setup ID `opencode`), así que la integración
es oficial y no un arreglos manual.

**Dato actualizado (2026-10-20):** el repo `morluto/rea` tiene **345.3k
stars**, 87k forks, 3.880 commits y ~810 issues abiertos. La tabla de
capacidades del README oficial es hoy mucho más amplia que la de estas notas
(WASM, EVM, SQLite, Godot PCK, IL2CPP, Go/Rust, Frida, qlog, pcap, HAR/SAZ,
procesos, firmwares…), por lo que esta tabla es un resumen, no el catálogo
completo.

---

## 2. Objetivos en el directorio de trabajo

- `cu.todus.android-v30102.apk` — paquete Android de Todus (mensajería).
  **Analizado el 2026-10-20** con REA + JADX headless (ver §7).

---

## 3. Preguntas abiertas

1. ¿El objetivo es el APK de Todus, los codecs de video, o ambos?
2. ¿Qué compresión de video se investiga exactamente? (falta definir)
3. ¿Se busca el códec nativo (bibliotecas `.so` del APK) o solo la capa Java?
   Si son las `.so`, REA necesita Ghidra (no instalado) o un descompilador
   equivalente; el análisis estático del APK con JADX no llega al código nativo.
4. ¿Se instala Ghidra (gratis) en el contenedor para cubrir binarios nativos?

### Abiertas tras la prueba en vivo de la API (2026-10-21)

5. ¿Cuál es el mensaje protobuf real de `v2/auth/check`? El reconstruido
   (`ProtoAuthCheckRequest{username, password}`) lo rechaza el servidor con
   `bad-proto-payload`. Falta descompilar el `ProtoAuthCheckRequest` de la APK
   (con `apktool`/`jadx`, no instalados en esta máquina) para ver sus campos
   y números de campo reales.
6. ¿Dónde vive la API de stickers? `stickers.todus.cu` es un bucket S3 puro y
   `v1/stickers/packs/{limit}/{offset}` da 404. La ruta de la APK puede estar
   obsoleta o el servicio movido de host.
7. ¿Cómo firma la app las peticiones a `client.todus.cu` (releases y
   dispositivos)? Ese host es un bucket S3 que exige **AWS4-HMAC-SHA256**
   (SigV4): las credenciales AWS estarían embebidas en la APK.
8. ¿Por qué `todus:roster:version` y `todus:offline:get` responden
   `service-unavailable`? Parece que esos módulos no están habilitados en el
   despliegue de `im.todus.cu` (ejabberd), no un error del cliente.
9. Falta una prueba de extremo a extremo de **envío y recepción** de mensajes:
   requiere que alguien mande un mensaje a la cuenta desde la app.
   → **Hecho el 2026-10-21**: ver bitácora, sesión 4 (puntos 19-24) y
   `informe.md` §11.1.

### Pendientes tras la sesión de mensajería (2026-10-21)

10. **Namespace del chat-state propio de ToDus (`uc1`).** El servidor abrevia
    como `uc1` un namespace que la app usa para el estado "escribiendo…"
    (elemento `csp`; la APK trae `ChatStateExtensionToDus` y los strings
    `csp`/`csr` en `classes3.dex`). Probados sin acierto: `urn:xmpp:chatstates`,
    `http://jabber.org/protocol/chatstates` y 7 variantes `todus:*`. **Cómo
    encontrarlo**: desensamblar `ChatStateExtensionToDus` con JADX (falta
    `apktool`/`jadx` en esta máquina) y leer su constante de namespace.
11. **Por qué un mensaje enviado por el script no llegó a la app.** El
    servidor entregó el mensaje a la sesión del script pero **no** al
    dispositivo Android, que estaba conectado a la vez. Hipótesis: el
    servidor reparte **un solo destino** por mensaje y, con dos sesiones de la
    misma cuenta, elige una. Comprobar mandando al **JID completo con
    recurso**: `5358799457@im.todus.cu/415bb0dc12c9ca21bbacba7def4f8297_Android`.
12. **Marcar como "leído" con la app real.** Ya está implementado
    (`send_chat_marker`, XEP-0333) pero falta ver el doble tick azul en el
    teléfono: la app debe receber `<displayed xmlns='urn:xmpp:chat-markers:0'/>`.
    Depende de (11) para que el mensaje llegue a la app.
13. **Firmas AWS4 para `client.todus.cu`** (releases, dispositivos) y para
    subir adjuntos al S3: hay que extraer las credenciales AWS de la APK.
14. **`v2/auth/check`**: falta el mensaje protobuf real (ver punto 5).
15. **API de stickers**: la ruta de la APK da 404; hay que localizar el host
    actual (ver punto 6).
16. **Extensiones sin probar contra el servidor**: imagen, fichero, voz,
    vídeo, sticker, ubicación, reacción, mención y botón. El troceado y el
    texto ya están verificados.
17. **Llamadas (WebRTC)**: fase 2, sin empezar.

---

## 4. Repositorio

### `Michel3006/Ing-Inversa` — creado y poblado (2026-10-20)

- **URL:** https://github.com/Michel3006/Ing-Inversa
- **Visibilidad:** privado
- **Rama:** `main`, historia lineal (2 commits: README inicial del usuario
  + commit nuestro con APK e `investigacion/`).
- **Remoto local:** `https://github.com/Michel3006/Ing-Inversa.git`
  (sin token embebido en la URL).
- **Contenido:** `README.md`, `cu.todus.android-v30102.apk` (71 554 413 B),
  `investigacion/notas.md`, `investigacion/bitacora.md`.

**Premisa verificada:** Git para Windows 2.55.0 instalado, pero **sin**
`gh` CLI, **sin** `credential.helper` global y **sin** `winget`. La
autenticación se hizo con PAT vía helper inline de entorno.

**Aviso del remoto (esperado, no es error):** GH001 — el APK de 68.24 MiB
supera el máximo *recomendado* de 50 MB de GitHub. El límite duro es 100 MB,
por eso el push funcionó. Consecuencia: si el APK cambia frecuentemente, el
repo engordará rápido; plantear Git LFS en ese caso.

---

## 5. Descartes

- **Crear el repo vía API REST** → descartado en la práctica: devolvió 422
  "name already exists" porque el usuario ya lo había creado desde la web.
  No es un descarte técnico del método, sino que no hizo falta.
- **`gh` CLI como vía de creación** → descartado por falta de `winget` para
  instalarlo; se usó `curl.exe` (incluido en Windows) contra la API REST,
  que bastó.
- **Usar el JADX estándar** → descartado: REA no lo consume. Requiere
  `jadx-headless-mcp 0.7.1` (JAR propio) con SHA-256 pineado.

---

## 6. Riesgos y pendientes

- **CRÍTICO:** el PAT usado en la sesión 2 está comprometido (vio el chat).
  **Revocarlo** en https://github.com/settings/tokens antes de reutilizar.
- **Pendiente de la máquina Windows:** reiniciar OpenCode para que cargue el
  servidor MCP de REA (ya resuelto en el contenedor Linux).
- Instalar Ghidra si el objetivo incluye el código nativo de las `.so` del APK.
- `adb` no instalado: sin análisis en dispositivo ni captura de runtime Android.
- `frida` sin script de instalación ejecutado: sin captura de runtime con Frida.

---

## 7. Análisis del APK de Todus (2026-10-20)

**Herramienta:** REA 6.7.0 → `jadx-headless-mcp 0.7.1` (SHA-256
`6e5eacf5…3ff75`, revisión `5844800a486d2248fa949b74c9896285f5b54de2`),
1 worker, heap 8192 MiB, JDK 25.0.4.1.

**Artefacto:** `cu.todus.android-v30102.apk`
SHA-256 `8da7f305e9763e542b087935714b726fafa3e26105e9e6f96261074c56461ce1`.

**Declaraciones observadas (evidencia REA `inspect_android_package`):**

| Campo | Valor |
|---|---|
| Paquete | `cu.todus.android` |
| `versionName` | 2.1.2 |
| `versionCode` | 30102 |
| `minSdkVersion` | 23 (Android 6.0) |
| `targetSdkVersion` | 36 |
| Permisos declarados | 46 |
| Clases | 35 057 |
| Recursos | 1 877 |
| Manifest decodificado | completo (25 877 unidades de código UTF-16) |
| Verificación de firma | `not_performed` (no la hace este proveedor) |

**Limitaciones declaradas por REA (se conservan):** las observaciones del
decompilador estático no establecen comportamiento en runtime; las firmas de
método son tipos de presentación del proveedor (los prototipos DEX exactos
vienen de metadatos JADX congelados, y los índices de sobrecarga son locales
a este artefacto y motor); el inventario de clases usa metadatos parseados
antes de generar código e incluye miembros sintéticos; los totales de
referencias entrantes cuentan nodos "using" de JADX y no cubren todos los
llamados dinámicos o reflectivos; el proveedor no verifica firmas, APKs
divididos, bibliotecas nativas ni captura de runtime Android.

**Pendiente para el objetivo real:** el APK no incluye el código de los códecs
en Java; eso vive en bibliotecas nativas `.so`, que requieren Ghidra/IDA.

---

## 8. Mensajería: límites, consumo de datos y video (2026-10-24)

### 8.1 Límite de tamaño

| Constante | Valor | Alcance |
|---|---|---|
| `MAX_MSG_TEXT_SIZE` | `0x2000` = **8 192 bytes** | texto de **cada stanza** |
| `MAX_MESSAGE_LENGTH` | `0x10000` = **65 536 bytes** | **mensaje lógico** completo |

- El límite es en **bytes UTF-8**, no en caracteres: `ñ`/acentos ocupan 2 B y
  un emoji hasta 4 B, así que el número de caracteres es menor. En ASCII puro
  coinciden (8 192 car./stanza, 65 536 car./mensaje).
- `split_text()` trocea por 8 192 B y marca cada trozo con `msg:fragment:n`;
  `send_text()` **no** aplica el tope de 65 536 (solo fragmenta).
- **Escalado:** 4 000 000 de caracteres ASCII caben en ~62 mensajes de
  65 536 B (o ~489 stanzas de 8 192 B). No hay límite documentado para el
  número de mensajes de una conversación; el tope es por mensaje.

### 8.2 Consumo de datos móviles en Cuba

- **Texto de toDus: libre de costo** (no consume datos ni saldo), según
  ETECSA. **Fotos, notas de voz, archivos, video y llamadas sí consumen**
  del Plan toDus (1 GB / 25 CUP / 35 días desde abril 2026) o del plan de
  datos. El correo Nauta también consume.
- Fuentes consultadas: Cubadebate (2026-04-01, "ETECSA actualiza el Plan
  toDus"), FAQ oficial de toDus y página de planes de ETECSA.
- **Matiz para clientes propios:** el zero-rating se aplica al tráfico hacia
  `im.todus.cu` sobre red Cubacel con APN `nauta`. Como la API reconstruida
  usa esos mismos servidores, el texto *debería* quedar libre de costo, pero
  ETECSA tasa por tráfico y advierte que el uso de "aplicaciones no
  oficiales" puede limitar la cuenta. No hay garantía.

### 8.3 Estado del video en la API reconstruida

- **Funciona:** construcción de `<video xmlns='video:n' .../>` con sus 10
  atributos (`i, mi, url, n, s, d, w, he, h, tnail`), envío dentro de
  `<message>` y recepción/parseo (`extensions['video']`). Verificado en vivo
  `5358548838` → `5358799457` con URL falsa (solo metadata, sin datos de
  video). Los IQ prefirmados `todus:purl` (subida) y `todus:gurl` (descarga)
  aceptan `type='video'`.
- **Falta:** PUT real del archivo a S3, GET/descarga, método de alto nivel
  `send_video()` y reensamblado/almacenamiento local. La stanza por sí sola
  no entrega el video: solo apunta a una URL.
