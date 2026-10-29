# Ingeniería inversa de la APK de ToDus

Análisis completo de `cu.todus.android-v30102.apk` (ToDus 2.1.2, versionCode 30102).

**👉 Informe principal: [`informe.md`](informe.md)**

## Contenido

| Ruta | Contenido |
|---|---|
| [`informe.md`](informe.md) | Informe técnico: arquitectura, API, protobuf, BD, XMPP, multimedia |
| `api/superficie-http.md` | 33 operaciones REST con verbo, ruta y parámetros (verbo leído del `RequestFactory` de Retrofit, no inferido) |
| `api/ApiProto.proto` | Esquema protobuf reconstruido (34 mensajes, 78 campos) — **compila con `protoc`** |
| `api/cliente/` | Cliente Python: API REST (33/33 ops) + cliente XMPP de mensajería, con pruebas |
| `xmpp/protocolo.md` | Protocolo XMPP: sesión, 25 extensiones, `toDusSM`, MAM/MUC/channels |
| `xmpp/iqs.md` | Los 78 IQs propios de ToDus con su `ELEMENT` y su `NAMESPACE` |
| `xmpp/extensiones.md` | Tabla de extensiones XMPP con namespace y elemento |
| `db/esquema-room.sql.md` | Esquema SQL completo de la base local Room (18 tablas) |
| `manifest/AndroidManifest.decoded.xml` | Manifest decodificado (componentes, permisos, deep links) |
| `inventario/archivos.tsv` | Los 1 878 archivos del APK con SHA-256, tamaño y formato |
| `inventario/clases.txt` | Las 35 057 clases (inventario JADX completo) |
| `inventario/dependencias.txt` | 130 librerías con versión |
| `evidencia/` | Evidencia cruda de REA en JSON (artefacto, paquete, clases) |
| `herramientas/` | Scripts de extracción reproducibles |

## Herramientas

Todo gratuito: REA 6.7.0 (MCP), `jadx-headless-mcp 0.7.1`, apktool 3.0.3,
JADX 1.5.6, Ghidra 12.1.4, `grpcio-tools` (protoc), binutils, JDK 25.

## Convenciones

Cada afirmación del informe está etiquetada como *observado* (evidencia del
artefacto), *inferido* (deducción) o *pendiente*. El análisis es **estático**:
la APK nunca se ejecutó.
