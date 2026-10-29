# Informe de prueba integral

## Objetivo

Demostrar el recorrido completo:

```text
POST /jobs → descarga HTTP → SHA-256 → codificación → PostgreSQL
           → fragmentación → transporte → reensamblado
           → decodificación → SHA-256 → confirmación → limpieza
```

y que el fichero reconstruido es **idéntico byte a byte** al original.

## Entorno

| Dato | Valor |
|---|---|
| Fecha | 2026-10-28 |
| Equipo | contenedor de 2 vCPU, 7,9 GB de RAM |
| Binarios | `RelWithDebInfo` (`-O2 -g`), GCC 13.3.0 |
| PostgreSQL | 16, contenedor `tds-test` en `127.0.0.1:5432` |
| Servidor HTTP del fichero | `python3 -m http.server` en `127.0.0.1:18180` (sólo fixture de prueba) |
| Servidor A | `127.0.0.1:18181`, un worker |
| Servidor B | `127.0.0.1:18182`, transporte `mock` |
| Trama máxima | 8 192 bytes UTF-8 |
| Bloque de codificación | 65 536 bytes |
| Retardo de envío | `SEND_DELAY_MS=0` (para medir, no es el valor por defecto) |

Mandato ejecutado:

```bash
E2E_DATABASE_URL=postgresql://tds:tds@127.0.0.1:5432/tds_test \
  tools/e2e_test.sh
```

El script también puede levantar su propio PostgreSQL con Docker si no se
define `E2E_DATABASE_URL`.

## Resultados

Cada ejecución usa un fichero **aleatorio nuevo** (`/dev/urandom`), por lo que
los hashes cambian entre ejecuciones; lo relevante es que el hash del original
y el del reconstruido coinciden.

| Tamaño | job_id | Tramas | SHA-256 original = reconstruido | Resultado |
|---:|---|---:|---|---|
| 2 MiB (2 097 152 B) | `10700fec-5ce8-481f-8bf0-77250391c471` | 430 | `d095dd74ce6726f9abb59880ca4d043d515944f6e49ae508379921188906262c` | ✅ |
| 10 MiB (10 485 760 B) | `f4a56412-6b7f-4826-9854-dafe9ada77ba` | 2 148 | `28b3ad72bd83d0f628efe5a043bf25a82f403ea30c8c82658425d4fb07c63508` | ✅ |
| 25 MiB (26 214 400 B) | `9c2f9237-1b3f-47dd-90b4-4687240414cc` | 5 370 | `13b8dcfc71e7deea028dc85c5ec6f34d809adafa5052eda353c8dd56eb6c9a82` | ✅ |
| 25 MiB (repetición) | `fe16e3d2-29e6-4faa-abf8-a4d6a3b44057` | 5 370 | `5f5dd4fbf81ebf414261597577ab224c7f3eed0d255ef12543373bd12dc20b8c` | ✅ |
| 25 MiB (repetición) | `fa339ed4-ee9d-40f5-80d0-7e350c09090a` | 5 370 | `ee1647f1c3114add0de72d64b6a7901bca01545e36e4cbcd26fc9ef576da0b42` | ✅ |

Además, la **imagen Docker** (`ing-inversa:final`) se probó el 2026-10-29 con
un trabajo de 1 MiB arrancando los dos servicios en contenedores: hash
reconstruido
`68b944d9ac667d35b9f722e8ababdf1f9b5e907fa20f2caa96a91cb3c5b5ca90`, idéntico
al original, progreso 215/215 tramas y estado final `cleaned`.

Los `job_id` son los de la ejecución original. El mantenimiento purga los
metadatos de trabajos limpiados tras `RETENTION_HOURS` (24 h por defecto), así
que esas filas ya no estarán en la base al reproducir la prueba.

En todos los casos el script comprobó, además del SHA-256:

- progreso observable: `GET /jobs/{id}` devuelve `delivered_chunks` igual a
  `total_chunks` cuando B termina de enviar;
- tamaño idéntico al original;
- `cmp` byte a byte sin diferencias;
- estado final `cleaned` tras la confirmación;
- `SELECT count(*) FROM job_data_parts WHERE job_id=…` igual a **0**.

### Duraciones por etapa

Extraídas de `job_events` (no incluyen el arranque de los servicios ni las
pruebas negativas). «Descarga + codificación» va de `downloading` a
`encoding_done`; el total, de `created_at` a `cleaned_at`:

| Tamaño | Descarga + codificación | Envío (mock) | Total hasta `cleaned` |
|---:|---:|---:|---:|
| 2 MiB | 0,182 s | 0,793 s | 1,38 s |
| 10 MiB | 0,812 s | 4,165 s | 5,62 s |
| 25 MiB | 2,920 s | 10,430 s | 14,53 s |

El detalle y el análisis están en [benchmarks.md](benchmarks.md).

## Prueba con los dos números indicados (2026-10-29)

Se pidió probar con los números `58799457` y `58548838`. Son teléfonos
cubanos de 8 dígitos, así que `todus_jid()` les antepone `53` y los convierte
en `5358799457@im.todus.cu` y `5358548838@im.todus.cu`. El outbox del
transporte `mock` incluye el JID en el nombre de cada fichero, de modo que la
normalización se pudo comprobar directamente.

Cada ejecución usó 2 MiB de datos aleatorios nuevos, un destinatario distinto
y el mismo recorrido completo (descarga, codificación, tramas, reensamblado,
SHA-256, confirmación y limpieza):

| Número | JID resuelto | `job_id` | Tramas | SHA-256 original = reconstruido | Estado final |
|---|---|---|---|---:|---|---|
| `58799457` | `5358799457@im.todus.cu` | `5daaaa32-42cb-435f-9f0e-8679c5da31cb` | 430 | `a27f8ce85afa4030c7bf223a634a43457ca4fe3f0de601fcd3db0bc80416ea9e` | `cleaned`, 0 partes |
| `58548838` | `5358548838@im.todus.cu` | `43b825f2-7290-4841-8487-db48453d7ae0` | 430 | `35175f2cea78d3762a4a03eb98457ea8e0319d717c2502a113cc60b7f3383153` | `cleaned`, 0 partes |

```bash
E2E_RECIPIENT=58799457 E2E_DATABASE_URL=... tools/e2e_test.sh
E2E_RECIPIENT=58548838 E2E_DATABASE_URL=... tools/e2e_test.sh
```

**Alcance:** esto valida el recorrido completo y que los dos números se
normalizan y se aceptan como destinatarios. **No es un envío real por ToDus:**
el transporte fue `mock` y no se inició sesión XMPP. No hay en el entorno
credenciales ni token de una cuenta autorizada, y no se han inventado. La
conectividad TLS con `im.todus.cu:5222` sí se volvió a comprobar ese día
(TLS 1.3, `CN=todus.cu`, verificación correcta). Para un envío real hacen
falta `TODUS_TOKEN` o `TODUS_PASSWORD` de la cuenta emisora y autorización
para entregar al destinatario.

## Casos negativos

Todos se ejecutaron sobre las tramas reales del trabajo de 25 MiB:

| Caso | Manipulación | Resultado esperado | Resultado |
|---|---|---|---|
| Trama ausente | Se elimina un fichero del directorio | Error “faltan tramas” y código ≠ 0 | ✅ |
| Corrupción | Se añade un símbolo válido al final de una trama | CRC-32 no coincide y se rechaza | ✅ |
| Duplicado | Se copia una trama con otro nombre | Se tolera; reconstrucción correcta | ✅ |
| Desorden | Se renombran las tramas en orden inverso | Se tolera; reconstrucción correcta | ✅ |

El receptor también valida que la trama pertenece al `job_id` y
`transfer_id` esperados, y que la versión de codificación coincide.

## Qué demuestra y qué no

**Demuestra:**

- la codificación es reversible para datos binarios aleatorios;
- el protocolo de tramas respeta el límite de 8 192 bytes;
- el reensamblado tolera duplicados y desorden y detecta huecos y CRC;
- el estado compartido en PostgreSQL permite que A y B colaboren sin disco
  compartido;
- la confirmación con hash/tamaño es la que dispara el estado `completed` y la
  limpieza;
- el sistema completo funciona con el transporte `mock`.

**No demuestra:**

- el envío real por ToDus (no se usó una cuenta; ver
  [limitaciones.md](limitaciones.md));
- el comportamiento en Render (0,1 CPU, suspensión, base de 1 GB);
- la recepción en un teléfono real: el receptor es la CLI C++ acordada;
- tolerancia a caídas de red reales o reinicios a mitad de envío; esas rutas
  están implementadas y cubiertas por pruebas unitarias de lógica, pero no por
  una prueba de caos de varias horas.

## Reproducir

```bash
# 1. Compilar
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"

# 2. PostgreSQL (si no hay uno disponible)
docker run -d --name tds-test -p 5432:5432 \
  -e POSTGRES_USER=tds -e POSTGRES_PASSWORD=tds -e POSTGRES_DB=tds_test \
  postgres:16

# 3. Prueba integral
E2E_DATABASE_URL=postgresql://tds:tds@127.0.0.1:5432/tds_test tools/e2e_test.sh

# 4. Tamaño mayor
E2E_SIZE=26214400 \
  E2E_DATABASE_URL=postgresql://tds:tds@127.0.0.1:5432/tds_test \
  tools/e2e_test.sh

# 5. Conservar artefactos y logs
KEEP=1 E2E_DATABASE_URL=... tools/e2e_test.sh

# 6. Destinatario concreto (8 dígitos se normalizan a 53XXXXXXXX)
E2E_RECIPIENT=55123456 E2E_DATABASE_URL=... tools/e2e_test.sh
```

El script termina con código 0 sólo si todas las comprobaciones pasan.
