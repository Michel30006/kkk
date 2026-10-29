#!/usr/bin/env bash
# Ejecuta los benchmarks del códec con los tamaños indicados y, si existe
# GNU time, registra CPU y memoria. Uso:
#   tools/bench.sh                     # 1, 10 y 25 MiB
#   SIZES="5242880 26214400" tools/bench.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BENCH="${BUILD_DIR:-$ROOT/build}/benchmarks/tds_bench"
SIZES="${SIZES:-1048576 10485760 26214400}"

[[ -x "$BENCH" ]] || { echo "falta $BENCH (compila con cmake)" >&2; exit 2; }

TIME_BIN=""
[[ -x /usr/bin/time ]] && TIME_BIN=/usr/bin/time

for size in $SIZES; do
  printf '\n=== tds_bench %s bytes\n' "$size"
  if [[ -n "$TIME_BIN" ]]; then
    "$TIME_BIN" -f 'pared=%e s cpu_usuario=%U s cpu_sistema=%S s rss_max=%M kB' \
      "$BENCH" "$size"
  else
    "$BENCH" "$size"
  fi
done
