# Despliegue

## 1. Imagen Docker

`deploy/Dockerfile` es multi-etapa:

1. **build** (`ubuntu:24.04`): instala el toolchain y las dependencias,
   incluye `libpqxx` 8 desde el repositorio PGDG y compila
   `tds_server_a` y `tds_server_b` con CMake/Ninja en `Release`.
2. **runtime**: sólo instala las bibliotecas necesarias, copia los dos
   ejecutables, las migraciones y `deploy/entrypoint.sh`, y ejecuta como
   usuario sin privilegios `app` (uid 10001).

El *entrypoint* elige el proceso según `SERVICE`:

```bash
SERVICE=server_a   # ejecuta tds_server_a
SERVICE=server_b   # ejecuta tds_server_b
```

`MIGRATIONS_DIR` apunta a `/app/database/migrations`; las migraciones se
aplican al arrancar y son idempotentes (`schema_migrations`).

### Construir y ejecutar a mano

```bash
docker build -f deploy/Dockerfile -t ing-inversa .

docker run --rm -p 8080:8080 \
  -e SERVICE=server_a \
  -e PORT=8080 \
  -e DATABASE_URL='postgresql://usuario:clave@host:5432/ing_inversa' \
  -e INTERNAL_TOKEN='un-secreto-de-32-caracteres' \
  ing-inversa
```

Repite con `SERVICE=server_b` para el otro proceso. La imagen sólo contiene
los servidores; el receptor `tds_receiver` se compila aparte (o se usa el
binario del `build/` local).

## 2. Docker Compose (entorno local completo)

```bash
docker compose -f deploy/docker-compose.yml up --build
```

Levanta:

- `db`: PostgreSQL 16 con volumen `pgdata`;
- `server_a`: puerto publicado **8080**;
- `server_b`: puerto publicado **8081**, `TRANSPORT=mock`.

El compose habilita `ALLOW_HTTP=1` y `ALLOW_PRIVATE_NETWORKS=1` para poder
descargar de un servidor de ficheros del host. **No usar esa configuración en
producción.**

### Recuperar el fichero en local

El *outbox* del transporte mock vive dentro del contenedor de B:

```bash
docker compose -f deploy/docker-compose.yml cp \
  server_b:/tmp/tds-outbox ./outbox

build/client/tds_receiver --frames ./outbox \
  --job-id "$JOB_ID" --server-a http://127.0.0.1:8080 \
  --client-token "$CLIENT_TOKEN" --out recuperado.bin --confirm
```

El `JOB_ID` y el `CLIENT_TOKEN` son los devueltos por `POST /jobs`.

## 3. Render

`render.yaml` describe el blueprint: dos servicios web Docker y una base de
datos PostgreSQL gratuita. Pasos:

1. Subir el repositorio a GitHub.
2. En Render: **New → Blueprint** y elegir el repositorio.
3. Rellenar las variables marcadas `sync: false`:
   - `INTERNAL_TOKEN`: el **mismo** valor en `ing-inversa-a` e
     `ing-inversa-b` (mínimo 16 caracteres);
   - `SERVER_B_URL` en A y `SERVER_A_URL` en B, si se quiere ping mutuo
     (por ejemplo `https://ing-inversa-b.onrender.com`);
   - `TODUS_*` sólo cuando el transporte real esté autorizado.
4. Desplegar. Render construye `deploy/Dockerfile` con el contexto de la raíz.
5. Comprobar:
   ```bash
   curl -fsS https://<servicio-a>/health
   curl -fsS https://<servicio-a>/ready
   curl -fsS https://<servicio-b>/health
   ```

La base de datos se aprovisiona con `DATABASE_URL` inyectada por Render. El
primer arranque aplica las migraciones.

### Variables mínimas en Render

| Servicio | Variables |
|---|---|
| A | `SERVICE=server_a`, `DATABASE_URL`, `INTERNAL_TOKEN`, `SERVER_B_URL`, `MAX_VIDEO_BYTES=26214400`, `PING_INTERVAL_SECONDS=600` |
| B | `SERVICE=server_b`, `DATABASE_URL`, `INTERNAL_TOKEN`, `SERVER_A_URL`, `TRANSPORT=mock`, `MOCK_OUTBOX=/tmp/tds-outbox`, `PING_INTERVAL_SECONDS=600` |

Para activar ToDus, en B: `TRANSPORT=todus`, `TODUS_USERNAME`,
`TODUS_PASSWORD` (o `TODUS_TOKEN`), `TODUS_AUTH_URL`, `TODUS_DOMAIN`,
`TODUS_PORT`, `TODUS_VERIFY_TLS=1`. No se debe cambiar a `todus` sin haber
probado antes el envío real; ver [limitaciones.md](limitaciones.md).

### Advertencias de Render

- El plan gratuito tiene **0,1 CPU**, **512 MB** y suspensión tras 15 min sin
  tráfico entrante.
- El disco es efímero: el `outbox` de B no sobrevive a un reinicio y no es
  accesible desde A. En producción el estado está en PostgreSQL.
- PostgreSQL gratuito tiene **1 GB** y caduca a los 30 días.
- Los servicios gratuitos no tienen red privada; A y B se hablan por sus URLs
  públicas y comparten la base pública.
- `MAX_CONCURRENT_JOBS=1` es deliberado: con 0,1 CPU, dos codificaciones
  simultáneas agotan el tiempo de respuesta y pueden provocar timeouts.

## 4. Comprobar un despliegue

```bash
# Salud
curl -fsS https://<a>/health | jq
curl -fsS https://<b>/health | jq

# Crear trabajo (token interno)
curl -sS -X POST https://<a>/jobs \
  -H "X-Internal-Token: $INTERNAL_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"url":"https://ejemplo/video.mp4","recipient":"5300000000"}' | tee job.json

JOB_ID=$(jq -r .job_id job.json)
CLIENT_TOKEN=$(jq -r .client_token job.json)

# Estado
curl -sS -H "X-Client-Token: $CLIENT_TOKEN" https://<a>/jobs/$JOB_ID | jq

# Traza interna
curl -sS -H "X-Internal-Token: $INTERNAL_TOKEN" \
  https://<a>/jobs/$JOB_ID/events | jq
```

Estados esperados: `queued → downloading → encoding → ready_for_delivery →
sending → awaiting_client_confirmation → completed → cleaned`.
El salto a `completed` sólo ocurre tras la confirmación del receptor.

## 5. Operación

- **Logs**: stdout con nivel `LOG_LEVEL`; no incluyen tokens, contraseñas ni
  contenido del vídeo.
- **Salud**: `/health` comprueba la base; Render lo usa como *health check*.
- **Mantenimiento**: cada `MAINTENANCE_INTERVAL_SECONDS` (300 s por defecto)
  se expiran trabajos atascados (`STALE_JOB_MINUTES`,
  `CLIENT_CONFIRM_HOURS`) y se purgan metadatos de trabajos limpiados
  (`RETENTION_HOURS`).
- **Reinicios**: B omite las tramas ya registradas en `chunk_deliveries`; A
  reclama trabajos con lease (`JOB_LEASE_SECONDS`). Un trabajo a medias se
  reanuda o se marca `expired` si supera la ventana.
- **Cambios de esquema**: añadir una migración nueva en
  `database/migrations/` (numerada) y redesplegar; no editar las aplicadas.

## 6. Limitaciones del despliegue

La **imagen Docker sí se ha verificado** el 2026-10-29: se arrancaron los dos
servicios desde `ing-inversa:final` con PostgreSQL local y un trabajo de 1 MiB
recorrió todo el pipeline; el SHA-256 reconstruido coincidió
(`68b944d9ac667d35b9f722e8ababdf1f9b5e907fa20f2caa96a91cb3c5b5ca90`), el
progreso observado fue 215/215 tramas y el trabajo terminó en `cleaned`. No se
ha desplegado en Render.

Este repositorio no incluye una prueba de despliegue real en Render. Las
cifras y advertencias de Render proceden de los límites verificados el
2026-10-26 y documentados en `render.yaml`; pueden cambiar. La integración
ToDus sigue pendiente de validación con cuentas autorizadas.
