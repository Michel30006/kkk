# Ing Inversa — transferencia de vídeos por ToDus

Implementación en **C++20** de un sistema de transferencia de ficheros de vídeo
en dos servidores y un receptor:

1. **Servidor A** descarga el vídeo desde una URL, calcula su SHA-256 y lo
   codifica byte a byte con una tabla fija de 256 símbolos UTF-8.
2. **Servidor B** fragmenta el texto codificado en tramas `TDS1` y las envía
   por el transporte disponible (ToDus/XMPP real o un *outbox* local para
   pruebas).
3. **Receptor** (`tds_receiver`, CLI C++) reensambla las tramas, decodifica,
   comprueba tamaño y SHA-256, escribe el fichero y confirma la reconstrucción
   al Servidor A.
4. **Servidor A** borra los datos temporales sólo después de esa confirmación
   (o tras la política de expiración segura).

El estado de los trabajos vive en **PostgreSQL**, compartido por A y B; no se
usa disco efímero para transportar datos entre servicios.

> **Estado real de la integración ToDus:** el transporte `mock` está probado
> de extremo a extremo (2, 10 y 25 MiB byte a byte idénticos). El cliente
> XMPP/ToDus está implementado y compila. **La integración ToDus real se ha
> verificado el 2026-11-07:** se obtuvo un JWT válido de
> `POST https://auth.todus.cu/v2/auth/token` con solo el número de teléfono
> (sin contraseña válida), se autenticó por XMPP con SASL PLAIN usando el JWT
> como contraseña, y se envió y recibió **5 MB de vídeo de extremo a extremo**
> (destinatario `5358548838@im.todus.cu`) con SHA-256 idéntico, byte a byte
> idéntico, y confirmación al Servidor A. Detalle en
> [docs/limitaciones.md](docs/limitaciones.md) y
> [docs/prueba-integral.md](docs/prueba-integral.md).
> El 2026-10-29 se probaron los dos números de prueba indicados con el
> transporte `mock` (normalización de JID y recorrido completo).

## Documentación

| Documento | Contenido |
|---|---|
| [docs/arquitectura.md](docs/arquitectura.md) | Diagrama, componentes, estados, esquema y formato de trama. |
| [docs/prueba-integral.md](docs/prueba-integral.md) | Prueba de extremo a extremo con tamaños, hashes y resultados medidos. |
| [docs/benchmarks.md](docs/benchmarks.md) | Duración, CPU y memoria medidos. |
| [docs/limitaciones.md](docs/limitaciones.md) | Límites de Render/ToDus y riesgos confirmados. |
| [docs/despliegue.md](docs/despliegue.md) | Despliegue con Docker, Compose y Render. |
| [docs/manual_usuario.md](docs/manual_usuario.md) | Manual de usuario: desplegar en Render y pasar a producción. |
| [docs/tabla-codificacion.md](docs/tabla-codificacion.md) | Tabla de codificación v1 y sus criterios. |
| [especificacion_proyecto_transferencia_todus_2.md](especificacion_proyecto_transferencia_todus_2.md) | Especificación original del proyecto. |

## Estructura del repositorio

```text
.
├── shared/            # protocolo: codificación, tramas, SHA-256, XMPP, HTTP
├── storage/           # PostgreSQL (libpqxx), migraciones lógicas y mantenimiento
├── server_a/          # descarga + codificación (Drogon)
├── server_b/          # fragmentación + envío (Drogon)
├── client/            # tds_receiver: reensamblado y verificación
├── benchmarks/        # tds_bench
├── database/migrations/
├── deploy/            # Dockerfile, docker-compose.yml, entrypoint
├── tools/             # e2e_test.sh y generador de la tabla
├── investigacion/     # ingeniería inversa de la APK (material de apoyo)
└── docs/
```

Toda la lógica de producción es C++. Python sólo se usa para generar la tabla
de codificación y como servidor de ficheros estático en la prueba integral.

## Requisitos

- CMake ≥ 3.22, compilador con C++20 (probado con GCC 13.3).
- Drogon/Trantor, libcurl, OpenSSL, libpqxx, nlohmann-json, spdlog/fmt,
  zlib, uuid y GoogleTest.
- PostgreSQL 16 (local, Docker o Render).
- Opcional: Docker para compilar y desplegar igual que en producción.

En Ubuntu 24.04, `libpqxx` del repositorio oficial es 7.8 y **no enlaza** con
Drogon/Ubuntu por un símbolo de `conversion_overrun`. La imagen Docker usa el
repositorio PGDG (libpqxx 8). Para compilar en local hay que instalar también
libpqxx 8 desde PGDG:

```bash
sudo install -d /usr/share/postgresql-common/pgdg
curl -fsSL https://www.postgresql.org/media/keys/ACCC4CF8.asc \
  | sudo tee /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc >/dev/null
echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] \
https://apt.postgresql.org/pub/repos/apt noble-pgdg main" \
  | sudo tee /etc/apt/sources.list.d/pgdg.list
sudo apt-get update && sudo apt-get install -y libpqxx-dev libpqxx-8.0
```

## Compilación

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

Si hay una instalación de Conda en el `PATH`, sus cabeceras de `fmt` pueden
contaminar la compilación. En ese caso:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_IGNORE_PREFIX_PATH=/opt/conda -DCMAKE_IGNORE_PATH=/opt/conda \
  -Dfmt_DIR=/usr/lib/x86_64-linux-gnu/cmake/fmt
```

Artefactos: `build/server_a/tds_server_a`, `build/server_b/tds_server_b`,
`build/client/tds_receiver`, `build/benchmarks/tds_bench`.

## Pruebas

### Unitarias

```bash
cd build && ctest --output-on-failure
```

Cinco suites: `shared`, `storage`, `server_a_units`, `server_b_units`,
`client_units`. La suite `storage` necesita PostgreSQL:

```bash
docker run -d --name tds-test -p 5432:5432 \
  -e POSTGRES_USER=tds -e POSTGRES_PASSWORD=tds -e POSTGRES_DB=tds_test \
  postgres:16
```

Las pruebas de almacenamiento leen `TEST_DATABASE_URL` (ya configurada en
CMake para `postgresql://tds:tds@127.0.0.1:5432/tds_test`).

### Prueba integral

```bash
E2E_DATABASE_URL=postgresql://tds:tds@127.0.0.1:5432/tds_test tools/e2e_test.sh
```

Sin `E2E_DATABASE_URL`, el script levanta su propio contenedor PostgreSQL.
Requiere `curl`, `jq`, `sha256sum` y `psql` en el `PATH`, además de los
binarios compilados. Opciones: `E2E_SIZE` (bytes, por defecto 2 MiB),
`E2E_RECIPIENT` (teléfono de 8 dígitos o JID completo) y `KEEP=1` para
conservar el directorio temporal. El script comprueba:

- descarga, codificación, entrega y recepción completas;
- progreso observable (`delivered_chunks` igual a `total_chunks` antes de
  recibir);
- SHA-256 y tamaño idénticos y comparación byte a byte;
- limpieza de payloads tras la confirmación;
- detección de trama ausente y de corrupción por CRC;
- tolerancia a duplicados y a tramas desordenadas.

Resultados medidos en [docs/prueba-integral.md](docs/prueba-integral.md).

### Benchmarks

```bash
build/benchmarks/tds_bench 1048576
/usr/bin/time -v build/benchmarks/tds_bench 26214400
# O los tres tamaños con CPU/RAM resumidas:
tools/bench.sh
```

Resultados en [docs/benchmarks.md](docs/benchmarks.md).

## Ejecución local

```bash
cp .env.example .env    # ajusta DATABASE_URL e INTERNAL_TOKEN
set -a; . ./.env; set +a

build/server_a/tds_server_a &
build/server_b/tds_server_b &
```

Crear un trabajo (la URL debe ser accesible por A):

```bash
curl -sS -X POST http://127.0.0.1:8080/jobs \
  -H "X-Internal-Token: $INTERNAL_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"url":"https://example.com/video.mp4","recipient":"5300000000"}'
```

Respuesta: `job_id`, `transfer_id`, `client_token`. Consultar el estado:

```bash
curl -sS -H "X-Client-Token: $CLIENT_TOKEN" \
  http://127.0.0.1:8080/jobs/$JOB_ID
```

Recibir y verificar (transporte mock: B escribe las tramas en `MOCK_OUTBOX`):

```bash
build/client/tds_receiver --frames "$MOCK_OUTBOX" \
  --job-id "$JOB_ID" --server-a http://127.0.0.1:8080 \
  --client-token "$CLIENT_TOKEN" --out recuperado.bin --confirm
```

Con ToDus real (requiere credenciales y red autorizada):

```bash
TRANSPORT=todus TODUS_USERNAME=53XXXXXXXX TODUS_PASSWORD=... \
  build/server_b/tds_server_b

build/client/tds_receiver --xmpp-user 53XXXXXXXX --xmpp-token "$JWT" \
  --job-id "$JOB_ID" --server-a https://... --client-token "$CLIENT_TOKEN" \
  --out recuperado.bin --confirm
```

El mensaje sólo se considera entregado cuando el receptor confirma el hash;
enviar las tramas no marca el trabajo como completado.

## Despliegue

Compilación y ejecución con Docker:

```bash
docker build -f deploy/Dockerfile -t ing-inversa .
docker run --rm -p 8080:8080 --env-file .env -e SERVICE=server_a ing-inversa
```

Entorno completo con PostgreSQL y ambos servicios:

```bash
docker compose -f deploy/docker-compose.yml up --build
```

Render: `render.yaml` describe dos servicios web y una base de datos. Los
límites del plan gratuito y sus consecuencias están en
[docs/limitaciones.md](docs/limitaciones.md); el procedimiento, en
[docs/despliegue.md](docs/despliegue.md).

## Variables de entorno

Todas se leen en `shared/src/config.cpp`; `.env.example` documenta los valores
por defecto. Las principales:

| Variable | Uso |
|---|---|
| `DATABASE_URL` | PostgreSQL compartido por A y B. |
| `INTERNAL_TOKEN` | Secreto compartido (≥ 16 caracteres) para llamadas internas. |
| `PORT`, `LISTEN_ADDRESS` | Puerto y dirección de escucha. |
| `SERVER_A_URL`, `SERVER_B_URL` | Ping mutuo cada `PING_INTERVAL_SECONDS`. |
| `MAX_VIDEO_BYTES` | Límite de descarga (25 MiB por defecto). |
| `ALLOW_HTTP`, `ALLOW_PRIVATE_NETWORKS`, `ALLOWED_HOST_SUFFIXES` | Política SSRF (todo cerrado por defecto). |
| `ENCODE_PART_BYTES` | Tamaño de bloque de codificación en la base. |
| `TRANSPORT` | `mock` o `todus`. |
| `MAX_STANZA_BYTES` | Presupuesto de trama (8192 por defecto). |
| `MAX_SEND_RETRIES`, `SEND_DELAY_MS`, `ACK_TIMEOUT_MS` | Reintentos y ritmo de envío. |
| `TODUS_*` | Credenciales y endpoint XMPP (sólo `TRANSPORT=todus`). |
| `STALE_JOB_MINUTES`, `CLIENT_CONFIRM_HOURS`, `RETENTION_HOURS` | Expiración y retención. |
| `MAINTENANCE_INTERVAL_SECONDS` | Periodicidad del hilo de mantenimiento. |
| `MAX_CONCURRENT_JOBS` | Trabajos simultáneos por proceso. |
| `LOG_LEVEL` | `trace`…`critical`. |

Nunca se deben incluir credenciales reales en el repositorio: `.env` está en
`.gitignore` y `.env.example` sólo contiene marcadores.

## API resumida

Servidor A: `GET /health`, `GET /ready`, `POST /jobs`,
`GET /jobs/{id}`, `POST /jobs/{id}/confirm`, `GET /jobs/{id}/events`,
`GET /stats`.

Servidor B: `GET /health`, `GET /ready`, `POST /jobs/{id}/deliver`,
`GET /jobs/{id}`, `GET /stats`.

Las rutas internas exigen `X-Internal-Token`; el cliente usa `X-Client-Token`.
Detalle completo en [docs/arquitectura.md](docs/arquitectura.md).

## Licencia y material de investigación

No se ha definido una licencia para este repositorio. `investigacion/`
contiene material de ingeniería inversa de la APK de ToDus con fines de
interoperabilidad; no incluye contraseñas ni tokens de acceso reales. Los
números de teléfono que aparecen en esa documentación son los identificadores
usados en las pruebas originales y conviene redactarlos antes de publicar el
repositorio.
