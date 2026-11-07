# Manual de usuario — Despliegue y producción (Ing Inversa)

Sistema de transferencia de vídeo por ToDus en C++20. Dos servidores (A descarga/codifica, B fragmenta/envía), un receptor CLI y PostgreSQL como estado compartido.

---

## 1. Arquitectura resumida

| Componente | Responsabilidad |
|---|---|
| **Servidor A** | Descarga vídeo desde URL, calcula SHA-256, codifica, notifica a B |
| **Servidor B** | Fragmenta en tramas `TDS1`, envía por `mock` o `todus` (XMPP) |
| **tds_receiver** | CLI C++ que reensambla, decodifica, verifica SHA-256 y confirma |
| **PostgreSQL** | Estado compartido entre A y B (trabajos, partes, entregas, eventos) |
| **ToDus / XMPP** | Transporte real opcional (`TRANSPORT=todus`) |

**Flujo de un trabajo:**

```
queued → downloading → encoding → ready_for_delivery → sending → awaiting_client_confirmation → completed → cleaned
```

El salto a `completed` sólo ocurre cuando el receptor confirma el SHA-256 y el tamaño.

---

## 2. Despliegue en Render (paso a paso)

### Requisitos previos

- Cuenta en [Render](https://render.com)
- Repositorio en GitHub con el código
- Token interno compartido (mínimo 16 caracteres, aleatorio)

### Paso 1: Crear la base de datos

En Render: **New → PostgreSQL → `ing-inversa-db`**

| Campo | Valor |
|---|---|
| Database Name | `ing_inversa` |
| User | `ing_inversa` |
| Region | Oregón (mismo que los servicios) |
| Plan | Free (o superior para producción) |

Render inyecta `DATABASE_URL` automáticamente en los servicios.

### Paso 2: Desplegar los servicios con Blueprint

1. En Render: **New → Blueprint**
2. Conectar el repositorio GitHub
3. Render detecta `render.yaml` y propone:
   - `ing-inversa-a` (servidor A)
   - `ing-inversa-b` (servidor B)
   - `ing-inversa-db` (PostgreSQL)
4. Hacer **Apply**

### Paso 3: Rellenar variables `sync: false`

Render no permite compartir variables entre servicios. Rellenar a mano en el dashboard de cada servicio:

| Variable | Valor en A | Valor en B |
|---|---|---|
| `INTERNAL_TOKEN` | `abc123...` (32+ chars, igual en ambos) | `abc123...` (igual que A) |
| `SERVER_B_URL` | `https://ing-inversa-b.onrender.com` | — |
| `SERVER_A_URL` | — | `https://ing-inversa-a.onrender.com` |

Si se usa ToDus real (en B):

| Variable | Valor |
|---|---|
| `TRANSPORT` | `todus` |
| `TODUS_USERNAME` | `5358799457` (o el número autorizado) |
| `TODUS_TOKEN` | JWT de `POST https://auth.todus.cu/v2/auth/token` |
| `TODUS_DOMAIN` | `im.todus.cu` |
| `TODUS_PORT` | `5222` |
| `TODUS_VERIFY_TLS` | `1` |

### Paso 4: Verificar el despliegue

```bash
curl -fsS https://ing-inversa-a.onrender.com/health
curl -fsS https://ing-inversa-a.onrender.com/ready
curl -fsS https://ing-inversa-b.onrender.com/health
curl -fsS https://ing-inversa-b.onrender.com/ready
```

Todos deben devolver `{"status":"ok"}`.

### Paso 5: Crear un trabajo de prueba

```bash
curl -sS -X POST https://ing-inversa-a.onrender.com/jobs \
  -H "X-Internal-Token: $INTERNAL_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"url":"https://ejemplo.com/video.mp4","recipient":"5300000000"}'
```

Respuesta: `job_id`, `transfer_id`, `client_token`.

### Paso 6: Recibir el fichero

Transporte `mock` (Render):

```bash
# Las tramas quedan en el contenedor de B; acceder via Render shell o
# usar ToDus real si está configurado.
```

Transporte `todus` (real):

```bash
build/client/tds_receiver --xmpp-user 5358548838 \
  --xmpp-token "$JWT_RECEPTOR" \
  --job-id "$JOB_ID" \
  --server-a https://ing-inversa-a.onrender.com \
  --client-token "$CLIENT_TOKEN" \
  --out recuperado.bin \
  --confirm
```

---

## 3. Despliegue local con Docker Compose

Para pruebas antes de subir a Render:

```bash
cd /workspaces/kkk
docker compose -f deploy/docker-compose.yml up --build
```

Levanta PostgreSQL + A (puerto 8080) + B (puerto 8081, `TRANSPORT=mock`).

Crear trabajo:

```bash
curl -sS -X POST http://localhost:8080/jobs \
  -H "X-Internal-Token: cambio-obligatorio-1234567890" \
  -H 'Content-Type: application/json' \
  -d '{"url":"http://host.docker.internal:18180/video.bin","recipient":"5300000000"}'
```

Recibir (outbox en contenedor B):

```bash
docker compose -f deploy/docker-compose.yml cp server_b:/tmp/tds-outbox ./outbox
build/client/tds_receiver --frames ./outbox \
  --job-id "$JOB_ID" --server-a http://localhost:8080 \
  --client-token "$CLIENT_TOKEN" --out recuperado.bin --confirm
```

---

## 4. Variables de entorno

| Variable | Requerido | Valor por defecto | Descripción |
|---|---|---|---|
| `DATABASE_URL` | Sí | — | PostgreSQL compartido A y B |
| `INTERNAL_TOKEN` | Sí | — | Secreto compartido (≥16 chars) |
| `SERVICE` | Sí | `server_a` | `server_a` o `server_b` |
| `PORT` | No | `8080` | Puerto de escucha |
| `LISTEN_ADDRESS` | No | `0.0.0.0` | Dirección de escucha |
| `MIGRATIONS_DIR` | No | `./database/migrations` | Ruta de migraciones SQL |
| `SERVER_A_URL` | No | — | URL pública de A (ping mutuo) |
| `SERVER_B_URL` | No | — | URL pública de B (ping mutuo) |
| `PING_INTERVAL_SECONDS` | No | `600` | Intervalo de ping mutuo |
| `MAX_VIDEO_BYTES` | No | `26214400` | 25 MB límite de descarga |
| `ENCODE_PART_BYTES` | No | `65536` | Tamaño de bloque de codificación |
| `ALLOW_HTTP` | No | `0` | Permitir URLs `http://` (SSRF) |
| `ALLOW_PRIVATE_NETWORKS` | No | `0` | Permitir redes privadas (SSRF) |
| `ALLOWED_HOST_SUFFIXES` | No | (vacío) | Sufijos de host permitidos |
| `TRANSPORT` | No | `mock` | `mock` o `todus` |
| `MOCK_OUTBOX` | No | `/tmp/tds-outbox` | Directorio de outbox (mock) |
| `MAX_STANZA_BYTES` | No | `8192` | Límite de trama XMPP |
| `SEND_DELAY_MS` | No | `120` | Retardo entre envíos (ms) |
| `MAX_SEND_RETRIES` | No | `3` | Reintentos de envío |
| `ACK_TIMEOUT_MS` | No | `15000` | Timeout de confirmación (ms) |
| `TODUS_USERNAME` | No* | — | Usuario XMPP (sólo `todus`) |
| `TODUS_PASSWORD` | No* | — | Contraseña XMPP (sólo `todus`) |
| `TODUS_TOKEN` | No* | — | JWT XMPP (alternativa a contraseña) |
| `TODUS_AUTH_URL` | No | `https://auth.todus.cu/v2/auth/token` | Endpoint de token |
| `TODUS_DOMAIN` | No | `im.todus.cu` | Dominio XMPP |
| `TODUS_PORT` | No | `5222` | Puerto XMPP |
| `TODUS_RESOURCE` | No | `tds` | Recurso XMPP |
| `TODUS_VERIFY_TLS` | No | `1` | Verificar certificado TLS |
| `WORKER_POLL_MS` | No | `1000` | Intervalo de sondeo de jobs (ms) |
| `JOB_LEASE_SECONDS` | No | `180` | Tiempo de lease del job |
| `STALE_JOB_MINUTES` | No | `60` | Tiempo para marcar job atascado |
| `RETENTION_HOURS` | No | `24` | Horas de retención de metadatos |
| `CLIENT_CONFIRM_HOURS` | No | `24` | Horas para confirmación del cliente |
| `MAINTENANCE_INTERVAL_SECONDS` | No | `300` | Intervalo de mantenimiento (s) |
| `MAX_CONCURRENT_JOBS` | No | `1` | Jobs simultáneos |
| `LOG_LEVEL` | No | `info` | `trace`…`critical` |

\* Requeridos solo cuando `TRANSPORT=todus`.

---

## 5. API de Servidor A

| Método | Ruta | Autenticación | Descripción |
|---|---|---|---|
| `GET` | `/health` | — | Salud de la base de datos |
| `GET` | `/ready` | — | Servicio listo |
| `POST` | `/jobs` | `X-Internal-Token` | Crear trabajo |
| `GET` | `/jobs/{id}` | `X-Client-Token` | Estado y progreso del trabajo |
| `POST` | `/jobs/{id}/confirm` | `X-Client-Token` | Confirmar reconstrucción (SHA-256 + tamaño) |
| `GET` | `/jobs/{id}/events` | `X-Internal-Token` | Traza de eventos del trabajo |
| `GET` | `/stats` | — | Estadísticas del servicio |

**Crear trabajo:**

```bash
curl -X POST https://<a>/jobs \
  -H "X-Internal-Token: $INTERNAL_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"url":"https://ejemplo.com/video.mp4","recipient":"5300000000"}'
```

**Consultar progreso:**

```bash
curl -sS -H "X-Client-Token: $CLIENT_TOKEN" \
  https://<a>/jobs/$JOB_ID | jq '{status, delivered_chunks, total_chunks, progress_pct}'
```

`progress_pct` devuelve el porcentaje de tramas entregadas (0-100).

---

## 6. Operación de producción

### 6.1 Monitoreo de salud

```bash
# Cada servicio expone /health y /ready
curl -f https://<servicio>/health   # comprueba PostgreSQL
curl -f https://<servicio>/ready    # servicio listo para tráfico
```

Render usa `/health` como health check. Si falla, reinicia el contenedor.

### 6.2 Estados de trabajo y qué significan

| Estado | Significado |
|---|---|
| `queued` | Trabajo creado, esperando descarga |
| `downloading` | Servidor A descargando el vídeo |
| `encoding` | Codificando el vídeo en texto |
| `ready_for_delivery` | Listo para entregar a B |
| `sending` | B enviando tramas al receptor |
| `awaiting_client_confirmation` | Todas las tramas enviadas, esperando confirmación |
| `completed` | Receptor confirmó SHA-256 y tamaño |
| `cleaned` | Datos temporales borrados |
| `failed_*` | Error en alguna etapa |

### 6.3 Mantenimiento automático

Cada `MAINTENANCE_INTERVAL_SECONDS` (300 s por defecto):

- Expira trabajos atascados (`STALE_JOB_MINUTES`, `CLIENT_CONFIRM_HOURS`)
- Purga metadatos de trabajos limpiados (`RETENTION_HOURS`)
- B omite tramas ya registradas en `chunk_deliveries` (reanudación tras reinicio)

### 6.4 Reinicios y recuperación

- Un trabajo a medias se reanuda gracias a los leases de base de datos (`JOB_LEASE_SECONDS`)
- Si B se reinicia, omite las tramas ya entregadas
- Si A se reinicia, reclama trabajos con lease expirado

### 6.5 Logs

- Salida stdout con nivel configurable (`LOG_LEVEL`)
- No incluyen tokens, contraseñas ni contenido del vídeo
- En Render: ver desde el dashboard o `render logs`

---

## 7. Seguridad

| Medida | Descripción |
|---|---|
| `INTERNAL_TOKEN` | Secreto compartido obligatorio, mínimo 16 caracteres |
| `X-Client-Token` | Token por trabajo, devuelto una sola vez en `POST /jobs` |
| `ALLOW_HTTP=0` | URLs `http://` bloqueadas por defecto (SSRF) |
| `ALLOW_PRIVATE_NETWORKS=0` | Redes privadas bloqueadas (SSRF) |
| `ALLOWED_HOST_SUFFIXES` | Sufijos de host permitidos (vacío = cualquier público) |
| `TODUS_*` | No commitear en el repositorio |
| `DATABASE_URL` | Contiene credenciales; no commitear |

### Rotación de secretos

1. Generar nuevo `INTERNAL_TOKEN`
2. Actualizar en ambos servicios (Render dashboard)
3. Esperar a que los pods se reinicien
4. Revocar el token anterior

---

## 8. Backup y recuperación

### Base de datos

Render plan gratuito: 1 GB, caduca a los 30 días. Para producción:

- Usar plan pagado de Render PostgreSQL con backups automáticos
- O exportar periódicamente: `pg_dump $DATABASE_URL > backup.sql`
- Restaurar: `psql $DATABASE_URL < backup.sql`

### Migraciones

Las migraciones son idempotentes (`schema_migrations`). Para añadir una nueva:

1. Crear archivo en `database/migrations/002_nombre.sql`
2. Redesplegar; se aplica automáticamente al arranque
3. Nunca editar migraciones ya aplicadas

---

## 9. Limitaciones conocidas

### Render plan gratuito

| Límite | Valor | Impacto |
|---|---|---|
| CPU | 0,1 vCPU | `MAX_CONCURRENT_JOBS=1` obligatorio |
| RAM | 512 MB | Máximo ~25 MB de vídeo por job |
| Disco | Efímero | `outbox` no sobrevive reinicios |
| Suspensión | 15 min sin tráfico | Usar `PING_INTERVAL_SECONDS` para mitigar |
| PostgreSQL | 1 GB, 30 días caducidad | No apto para producción larga |
| Red privada | No disponible | A y B se hablan por URLs públicas |

### ToDus real

- Requiere `TODUS_TOKEN` o `TODUS_USERNAME`/`TODUS_PASSWORD` de cuenta autorizada
- El servidor puede entregar a un solo recurso por mensaje (bare JID)
- Usar `--xmpp-listen` en el receptor para evitar race conditions
- Prioridad XMPP 10 configurada para favorecer el recurso del receptor

---

## 10. Troubleshooting

| Síntoma | Causa probable | Solución |
|---|---|---|
| `/health` falla | PostgreSQL no accesible | Verificar `DATABASE_URL` |
| Trabajo en `queued` para siempre | Worker no arranca | Revisar logs de A |
| Trabajo en `sending` sin avanzar | B no conecta al transporte | Verificar `TRANSPORT` y `TODUS_*` |
| Receptor no recibe tramas | Race condition (B envió antes de conectar) | Usar `--xmpp-listen` antes de crear el job |
| SHA-256 no coincide | Tramas perdidas o corruptas | Revisar CRC y duplicados |
| `progress_pct` nulo | `delivered_count` falló | Verificar permisos de base de datos |

```bash
# Ver logs de un servicio en Render
render logs --service ing-inversa-a
render logs --service ing-inversa-b

# Ver eventos de un trabajo
curl -sS -H "X-Internal-Token: $INTERNAL_TOKEN" \
  https://<a>/jobs/$JOB_ID/events | jq
```

---

## 11. Checklist de producción

- [ ] Repositorio en GitHub
- [ ] Base de datos PostgreSQL en Render (plan pagado para producción)
- [ ] `INTERNAL_TOKEN` generado y configurado en ambos servicios
- [ ] `SERVER_A_URL` y `SERVER_B_URL` configurados
- [ ] `TRANSPORT=mock` para pruebas iniciales
- [ ] `TRANSPORT=todus` solo tras validar con cuentas autorizadas
- [ ] `ALLOW_HTTP=0` y `ALLOW_PRIVATE_NETWORKS=0` en producción
- [ ] `MAX_VIDEO_BYTES` ajustado al plan (25 MB en gratuito)
- [ ] `MAX_CONCURRENT_JOBS=1` en Render gratuito
- [ ] Health checks pasando en ambos servicios
- [ ] Prueba de extremo a extremo con vídeo de prueba
- [ ] Plan de backup de PostgreSQL definido

---

## 12. Comandos rápidos

```bash
# Health check
curl -f https://<servicio>/health

# Crear trabajo
curl -X POST https://<a>/jobs -H "X-Internal-Token: $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"url":"<url-video>","recipient":"5300000000"}'

# Consultar progreso
curl -sS -H "X-Client-Token: $CLIENT_TOKEN" \
  https://<a>/jobs/$JOB_ID | jq '{status, delivered_chunks, total_chunks, progress_pct}'

# Confirmar recepción (después de descargar y verificar)
curl -X POST https://<a>/jobs/$JOB_ID/confirm \
  -H "X-Client-Token: $CLIENT_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"sha256":"<hash>","size":<tamano>}'
```

---

*Documento generado para el proyecto Ing Inversa. Última actualización: 2026-11-07.*
