#!/bin/sh
set -eu

case "${SERVICE:-server_a}" in
  server_a)
    exec /usr/local/bin/tds_server_a
    ;;
  server_b)
    exec /usr/local/bin/tds_server_b
    ;;
  *)
    echo "SERVICE desconocido: ${SERVICE}. Use server_a o server_b." >&2
    exit 64
    ;;
esac
