#!/usr/bin/env bash
# Prueba integral local del sistema completo:
#
#   generador -> HTTP local -> Servidor A (descarga + codifica + PostgreSQL)
#             -> Servidor B (trama + transporte mock)
#             -> tds_receiver (reensambla, verifica SHA-256, confirma)
#             -> limpieza en PostgreSQL
#
# Requisitos: binarios ya compilados (./build), curl, jq, sha256sum, psql y,
# si no se define E2E_DATABASE_URL, Docker para levantar PostgreSQL.
#
# Uso:
#   tools/e2e_test.sh                 # 2 MiB de datos aleatorios
#   E2E_SIZE=$((5*1024*1024)) tools/e2e_test.sh
#   E2E_DATABASE_URL=postgresql://... tools/e2e_test.sh
#   E2E_RECIPIENT=55123456 tools/e2e_test.sh   # teléfono de 8 dígitos
#   KEEP=1 tools/e2e_test.sh          # conserva el directorio temporal
#
# El servidor HTTP estático es un fixture de prueba (python3 -m http.server).
# No forma parte de la solución: la lógica de producción es C++.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build}"
SIZE="${E2E_SIZE:-2097152}"
HTTP_PORT="${E2E_HTTP_PORT:-18180}"
A_PORT="${E2E_A_PORT:-18181}"
B_PORT="${E2E_B_PORT:-18182}"
TOKEN="e2e-internal-token-0123456789"
RECIPIENT="${E2E_RECIPIENT:-5300000000}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/tds-e2e.XXXXXX")"

PIDS=()
DB_CONTAINER=""
FAILED=0

say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
ok()   { printf '  \033[32mOK\033[0m  %s\n' "$*"; }
fail() { printf '  \033[31mFALLO\033[0m  %s\n' "$*"; FAILED=1; }

cleanup() {
  local code=$?
  for pid in "${PIDS[@]:-}"; do
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  done
  if [[ -n "$DB_CONTAINER" ]]; then
    docker rm -f "$DB_CONTAINER" >/dev/null 2>&1 || true
  fi
  if [[ "${KEEP:-0}" == "1" || $FAILED -ne 0 ]]; then
    printf '\nDirectorio de la prueba: %s\n' "$TMP"
    for log in a b http; do
      [[ -f "$TMP/$log.log" ]] && { printf '\n--- %s.log (últimas 25 líneas) ---\n' "$log"; tail -25 "$TMP/$log.log"; }
    done
  else
    rm -rf "$TMP"
  fi
  exit $code
}
trap cleanup EXIT INT TERM

need() { command -v "$1" >/dev/null 2>&1 || { echo "falta $1" >&2; exit 2; }; }
need curl; need jq; need sha256sum; need psql
[[ -x "$BUILD/server_a/tds_server_a" ]] || { echo "falta build/server_a/tds_server_a" >&2; exit 2; }
[[ -x "$BUILD/server_b/tds_server_b" ]] || { echo "falta build/server_b/tds_server_b" >&2; exit 2; }
[[ -x "$BUILD/client/tds_receiver" ]] || { echo "falta build/client/tds_receiver" >&2; exit 2; }

say "Base de datos"
if [[ -n "${E2E_DATABASE_URL:-}" ]]; then
  DB_URL="$E2E_DATABASE_URL"
else
  need docker
  DB_CONTAINER="tds-e2e-$RANDOM$RANDOM"
  docker run -d --name "$DB_CONTAINER" \
    -e POSTGRES_USER=tds -e POSTGRES_PASSWORD=tds -e POSTGRES_DB=tds_test \
    -p 127.0.0.1:55432:5432 postgres:16 >/dev/null
  for _ in $(seq 1 60); do
    docker exec "$DB_CONTAINER" pg_isready -U tds -d tds_test >/dev/null 2>&1 && break
    sleep 1
  done
  DB_URL="postgresql://tds:tds@127.0.0.1:55432/tds_test"
fi
psql "$DB_URL" -tAc 'select 1' >/dev/null
ok "PostgreSQL disponible"

say "Fichero de prueba ($SIZE bytes aleatorios)"
mkdir -p "$TMP/www" "$TMP/outbox"
head -c "$SIZE" /dev/urandom > "$TMP/www/video.bin"
ORIG_SHA="$(sha256sum "$TMP/www/video.bin" | cut -d' ' -f1)"
ORIG_SIZE="$(stat -c %s "$TMP/www/video.bin")"
ok "sha256=$ORIG_SHA"

say "Servicios"
( cd "$TMP/www" && exec python3 -m http.server "$HTTP_PORT" --bind 127.0.0.1 ) \
  > "$TMP/http.log" 2>&1 &
PIDS+=($!)

env -i PATH="$PATH" \
  SERVICE_NAME=server_a PORT="$A_PORT" LISTEN_ADDRESS=127.0.0.1 \
  DATABASE_URL="$DB_URL" INTERNAL_TOKEN="$TOKEN" \
  MIGRATIONS_DIR="$ROOT/database/migrations" \
  SERVER_B_URL="http://127.0.0.1:$B_PORT" \
  ALLOW_HTTP=1 ALLOW_PRIVATE_NETWORKS=1 \
  MAX_VIDEO_BYTES=$((64*1024*1024)) ENCODE_PART_BYTES=65536 \
  WORKER_POLL_MS=100 MAX_CONCURRENT_JOBS=1 PING_INTERVAL_SECONDS=3600 \
  MAINTENANCE_INTERVAL_SECONDS=2 STALE_JOB_MINUTES=60 \
  "$BUILD/server_a/tds_server_a" > "$TMP/a.log" 2>&1 &
PIDS+=($!)

env -i PATH="$PATH" \
  SERVICE_NAME=server_b PORT="$B_PORT" LISTEN_ADDRESS=127.0.0.1 \
  DATABASE_URL="$DB_URL" INTERNAL_TOKEN="$TOKEN" \
  MIGRATIONS_DIR="$ROOT/database/migrations" \
  SERVER_A_URL="http://127.0.0.1:$A_PORT" \
  TRANSPORT=mock MOCK_OUTBOX="$TMP/outbox" \
  MAX_STANZA_BYTES=8192 SEND_DELAY_MS=0 ACK_TIMEOUT_MS=2000 \
  WORKER_POLL_MS=100 MAX_CONCURRENT_JOBS=1 PING_INTERVAL_SECONDS=3600 \
  MAINTENANCE_INTERVAL_SECONDS=2 \
  "$BUILD/server_b/tds_server_b" > "$TMP/b.log" 2>&1 &
PIDS+=($!)

wait_http() {
  local url=$1 name=$2
  for _ in $(seq 1 100); do
    if curl -fsS -m 2 "$url" >/dev/null 2>&1; then ok "$name responde"; return 0; fi
    sleep 0.2
  done
  fail "$name no responde"
  return 1
}
wait_http "http://127.0.0.1:$A_PORT/health" "Servidor A"
wait_http "http://127.0.0.1:$B_PORT/health" "Servidor B"
wait_http "http://127.0.0.1:$HTTP_PORT/video.bin" "HTTP de prueba" >/dev/null

say "Crear trabajo"
CREATE="$(curl -fsS -m 10 -X POST "http://127.0.0.1:$A_PORT/jobs" \
  -H "X-Internal-Token: $TOKEN" -H 'Content-Type: application/json' \
  -d "{\"url\":\"http://127.0.0.1:$HTTP_PORT/video.bin\",\"recipient\":\"$RECIPIENT\"}")"
JOB_ID="$(jq -r .job_id <<<"$CREATE")"
CLIENT_TOKEN="$(jq -r .client_token <<<"$CREATE")"
[[ -n "$JOB_ID" && "$JOB_ID" != "null" ]] || { fail "sin job_id: $CREATE"; exit 1; }
ok "job_id=$JOB_ID"

job_status() { # $1 puerto  $2 cabecera
  curl -fsS -m 5 -H "$2" "http://127.0.0.1:$1/jobs/$JOB_ID" | jq -r .status
}
wait_status() { # puerto, cabecera, estado esperado, nombre
  local port=$1 hdr=$2 want=$3 name=$4
  for _ in $(seq 1 600); do
    local got
    got="$(job_status "$port" "$hdr" 2>/dev/null || true)"
    if [[ "$got" == "$want" ]]; then ok "$name: $want"; return 0; fi
    if [[ "$got" == failed_* || "$got" == expired || "$got" == cancelled ]]; then
      fail "$name terminó en $got"; return 1
    fi
    sleep 0.2
  done
  fail "$name no llegó a $want (último: ${got:-?})"
  return 1
}

wait_status "$A_PORT" "X-Client-Token: $CLIENT_TOKEN" "awaiting_client_confirmation" "A (codificado y entregado)"
wait_status "$B_PORT" "X-Internal-Token: $TOKEN" "awaiting_client_confirmation" "B"
PROGRESS="$(curl -fsS -m 5 -H "X-Client-Token: $CLIENT_TOKEN" \
  "http://127.0.0.1:$A_PORT/jobs/$JOB_ID")"
DELIVERED="$(jq -r '.delivered_chunks // -1' <<<"$PROGRESS")"
TOTAL="$(jq -r '.total_chunks' <<<"$PROGRESS")"
[[ "$DELIVERED" == "$TOTAL" ]] \
  && ok "progreso observable: $DELIVERED/$TOTAL tramas" \
  || fail "progreso inesperado: $DELIVERED/$TOTAL"
ok "tramas en outbox: $(find "$TMP/outbox" -name '*.txt' | wc -l)"

say "Recepción y verificación"
set +e
"$BUILD/client/tds_receiver" --frames "$TMP/outbox" \
  --job-id "$JOB_ID" --server-a "http://127.0.0.1:$A_PORT" \
  --client-token "$CLIENT_TOKEN" \
  --out "$TMP/recuperado.bin" --confirm > "$TMP/receiver.log" 2>&1
RC=$?
set -e
sed -n '1,20p' "$TMP/receiver.log"
[[ $RC -eq 0 ]] || { fail "tds_receiver salió con $RC"; exit 1; }

GOT_SHA="$(sha256sum "$TMP/recuperado.bin" | cut -d' ' -f1)"
GOT_SIZE="$(stat -c %s "$TMP/recuperado.bin")"
[[ "$GOT_SHA" == "$ORIG_SHA" ]] && ok "SHA-256 idéntico" || fail "SHA-256 distinto ($GOT_SHA)"
[[ "$GOT_SIZE" == "$ORIG_SIZE" ]] && ok "tamaño idéntico ($GOT_SIZE bytes)" || fail "tamaño distinto ($GOT_SIZE)"
cmp -s "$TMP/www/video.bin" "$TMP/recuperado.bin" && ok "byte a byte idéntico" || fail "difiere byte a byte"

wait_status "$A_PORT" "X-Client-Token: $CLIENT_TOKEN" "cleaned" "A tras confirmar"
PARTS="$(psql "$DB_URL" -tAc "select count(*) from job_data_parts where job_id='$JOB_ID'")"
[[ "$PARTS" == "0" ]] && ok "payloads eliminados" || fail "quedan $PARTS partes"

say "Casos negativos"
receiver() { # dir salida -> imprime el código de salida
  set +e
  "$BUILD/client/tds_receiver" --frames "$1" \
    --job-id "$JOB_ID" --server-a "http://127.0.0.1:$A_PORT" \
    --client-token "$CLIENT_TOKEN" --out "$2" > "$2.log" 2>&1
  local rc=$?
  set -e
  echo "$rc"
}

cp -r "$TMP/outbox" "$TMP/n-missing"
rm -f "$(find "$TMP/n-missing" -name '*.txt' -print -quit)"
RC="$(receiver "$TMP/n-missing" "$TMP/n-missing.bin")"
if [[ "$RC" -ne 0 ]] && grep -q "faltan" "$TMP/n-missing.bin.log"; then
  ok "trama ausente detectada"
else
  fail "no se detectó la trama ausente (rc=$RC)"; tail -3 "$TMP/n-missing.bin.log"
fi

cp -r "$TMP/outbox" "$TMP/n-crc"
VICTIM="$(find "$TMP/n-crc" -name '*.txt' -print -quit)"
printf '!' >> "$VICTIM"   # símbolo válido: sólo rompe el CRC
RC="$(receiver "$TMP/n-crc" "$TMP/n-crc.bin")"
if [[ "$RC" -ne 0 ]] && grep -qi "crc" "$TMP/n-crc.bin.log"; then
  ok "corrupción detectada por CRC"
else
  fail "no se detectó la corrupción (rc=$RC)"; tail -3 "$TMP/n-crc.bin.log"
fi

cp -r "$TMP/outbox" "$TMP/n-dup"
FIRST="$(find "$TMP/n-dup" -name '*.txt' -print -quit)"
cp "$FIRST" "$TMP/n-dup/dup-$(basename "$FIRST")"
RC="$(receiver "$TMP/n-dup" "$TMP/n-dup.bin")"
if [[ "$RC" -eq 0 ]] && cmp -s "$TMP/n-dup.bin" "$TMP/www/video.bin"; then
  ok "duplicado tolerado"
else
  fail "el duplicado rompió la reconstrucción (rc=$RC)"; tail -3 "$TMP/n-dup.bin.log"
fi

cp -r "$TMP/outbox" "$TMP/n-order"
i=0
for f in $(find "$TMP/n-order" -name '*.txt' | sort -r); do
  mv "$f" "$TMP/n-order/rev-$(printf '%08d' "$i").txt"; i=$((i+1))
done
RC="$(receiver "$TMP/n-order" "$TMP/n-order.bin")"
if [[ "$RC" -eq 0 ]] && cmp -s "$TMP/n-order.bin" "$TMP/www/video.bin"; then
  ok "desorden tolerado"
else
  fail "el desorden rompió la reconstrucción (rc=$RC)"; tail -3 "$TMP/n-order.bin.log"
fi

say "Resumen"
if [[ $FAILED -eq 0 ]]; then
  printf '\033[32mPRUEBA INTEGRAL SUPERADA\033[0m  (%s bytes, sha256 %s)\n' "$ORIG_SIZE" "$ORIG_SHA"
else
  printf '\033[31mPRUEBA INTEGRAL CON FALLOS\033[0m\n'
fi
exit $FAILED
