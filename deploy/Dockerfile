# Imagen única para los dos servicios: se compilan ambos ejecutables y
# deploy/entrypoint.sh elige según la variable SERVICE (server_a | server_b).
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# libpqxx 8 sólo está en el repositorio PGDG en Ubuntu 24.04.
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl gnupg \
 && install -d /usr/share/postgresql-common/pgdg \
 && curl -fsSL -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc \
      https://www.postgresql.org/media/keys/ACCC4CF8.asc \
 && echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] \
https://apt.postgresql.org/pub/repos/apt noble-pgdg main" \
      > /etc/apt/sources.list.d/pgdg.list \
 && apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build \
      libdrogon-dev libtrantor-dev libjsoncpp-dev \
      libcurl4-openssl-dev libssl-dev libpq-dev libpqxx-dev \
      libspdlog-dev libfmt-dev nlohmann-json3-dev \
      uuid-dev zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF \
 && cmake --build build --target tds_server_a tds_server_b -j"$(nproc)"

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl gnupg \
 && install -d /usr/share/postgresql-common/pgdg \
 && curl -fsSL -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc \
      https://www.postgresql.org/media/keys/ACCC4CF8.asc \
 && echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] \
https://apt.postgresql.org/pub/repos/apt noble-pgdg main" \
      > /etc/apt/sources.list.d/pgdg.list \
 && apt-get update && apt-get install -y --no-install-recommends \
      libdrogon1t64 libtrantor1 libjsoncpp25 \
      libcurl4t64 libssl3t64 libpq5 libpqxx-8.0 \
      libspdlog1.12 libfmt9 zlib1g \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 10001 --create-home --shell /usr/sbin/nologin app

WORKDIR /app
COPY --from=build /src/build/server_a/tds_server_a /usr/local/bin/tds_server_a
COPY --from=build /src/build/server_b/tds_server_b /usr/local/bin/tds_server_b
COPY --from=build /src/database/migrations /app/database/migrations
COPY deploy/entrypoint.sh /usr/local/bin/entrypoint

RUN chmod 0755 /usr/local/bin/entrypoint && chown -R app:app /app
USER app

ENV MIGRATIONS_DIR=/app/database/migrations
# Render y docker-compose inyectan PORT; por defecto 8080.
EXPOSE 8080

ENTRYPOINT ["/usr/local/bin/entrypoint"]
