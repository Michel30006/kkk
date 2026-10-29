# Benchmarks medidos

Todas las cifras de este documento se han medido en la máquina descrita
abajo, no se han estimado.

## Entorno

| Dato | Valor |
|---|---|
| Contenedor | 2 vCPU, 7,9 GB de RAM, Linux |
| Compilador | GCC 13.3.0, `-O2 -g` (`RelWithDebInfo`) |
| CMake | 3.28.3, Ninja |
| libpqxx | 8.0.2 (PGDG) |
| PostgreSQL | 16 (contenedor local) |
| Fecha | 2026-10-28 |

Los benchmarks son de un solo proceso y una sola ejecución por tamaño; no se
ha hecho un análisis estadístico de varianza. En Render gratuito (0,1 CPU) los
tiempos de CPU serían mucho mayores. Las cifras varían con la carga del
equipo: en repeticiones posteriores sin carga de compilación, para 1 MiB,
`encode` dio entre 108 y 122 MiB/s y `decode` 111 MiB/s, por lo que deben
tomarse como orden de magnitud y no como un valor exacto.

## 1. Núcleo del códec (`tds_bench`)

`tds_bench` genera bytes aleatorios, mide codificación, decodificación,
SHA-256 y el plan de tramas con `MAX_STANZA_BYTES=8192`. No incluye red ni
base de datos.

| Entrada | Codificado | encode | decode | SHA-256 | Plan de tramas | CPU usuario | Pared | RSS máximo |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 MiB | 1,66 MiB | 121 MiB/s | 111 MiB/s | 513 MiB/s | 214 tramas en 12 ms (17 722/s) | 0,03 s | 0,04 s | 10 880 kB |
| 10 MiB | 16,56 MiB | 121 MiB/s | 110 MiB/s | 1 144 MiB/s | 2 135 tramas en 120 ms (17 757/s) | 0,33 s | 0,36 s | 44 416 kB |
| 25 MiB | 41,41 MiB | 121 MiB/s | 111 MiB/s | 1 247 MiB/s | 5 338 tramas en 302 ms (17 668/s) | 0,86 s | 0,90 s | 100 608 kB |

Notas:

- El factor de expansión es **1,656** (1,6562 bytes UTF-8 por byte de vídeo),
  consecuencia de la tabla v1: 88 símbolos de 1 byte y 168 de 2 bytes.
- La velocidad de codificación es estable (~121 MiB/s) porque cada byte se
  traduce con una tabla, sin hexadecimal ni binario intermedio.
- El RSS de `tds_bench` crece con el tamaño porque mantiene en memoria la
  entrada, el texto codificado y la salida decodificada a la vez. Los
  servidores procesan por bloques y no tienen ese perfil.
- El número de tramas del benchmark (p. ej. 5 338 para 25 MiB) es una
  estimación del plan con payload exacto de 8 192 bytes; en la prueba real
  fueron 5 370 porque cada trama reserva cabecera y el último bloque es menor.
  El valor exacto depende de la distribución de bytes del fichero (la tabla
  mezcla símbolos de 1 y 2 bytes), así que puede variar en ±1 trama para el
  mismo tamaño.

Reproducir:

```bash
cmake --build build -j"$(nproc)"
tools/bench.sh                 # 1, 10 y 25 MiB con CPU/RAM
build/benchmarks/tds_bench 1048576
/usr/bin/time -v build/benchmarks/tds_bench 26214400
```

## 2. Sistema completo (prueba integral, transporte mock)

Medido con `tools/e2e_test.sh` en el mismo equipo, PostgreSQL local,
`SEND_DELAY_MS=0`, `ENCODE_PART_BYTES=65536`, un worker por servicio y
`MAX_STANZA_BYTES=8192`. Las duraciones por etapa salen de `job_events`:
«descarga+codificación» va desde el evento `downloading` hasta
`encoding_done`, y «total hasta `cleaned`» desde `created_at` hasta
`cleaned_at`.

| Tamaño | Tramas | Descarga+codificación | Envío (mock) | Total hasta `cleaned` | Throughput total |
|---:|---:|---:|---:|---:|---:|
| 2 MiB | 430 | 0,182 s | 0,793 s | **1,38 s** | 1,45 MiB/s |
| 10 MiB | 2 148 | 0,812 s | 4,165 s | **5,62 s** | 1,78 MiB/s |
| 25 MiB | 5 370 | 2,920 s | 10,430 s | **14,53 s** | 1,72 MiB/s |

El envío mock sostiene ~515 tramas/s (≈4,2 MiB/s de texto codificado) porque
escribe un fichero por trama. **No representa el caudal de ToDus**: con el
ritmo conservador por defecto (`SEND_DELAY_MS=120`) el mismo trabajo tardaría
~11 minutos sólo en enviar.

Pico de memoria residente durante el trabajo de 25 MiB (muestreo de
`VmHWM` cada 200 ms):

| Proceso | Pico RSS |
|---|---:|
| `tds_server_a` | ~20 MB |
| `tds_server_b` | ~77 MB |

B mantiene el texto codificado del trabajo en memoria para fragmentarlo; A
trabaja por bloques y por eso su pico es menor. Ambos quedan por debajo de los
512 MB del plan gratuito de Render, pero el margen de CPU de Render es mucho
menor que el de esta máquina.

## 3. Qué no se ha medido

- **ToDus real**: no hay cifras de caudal, latencia ni tasa de `tdack` porque
  no se ha iniciado sesión con una cuenta en esta entrega.
- **Render gratuito**: no se ha desplegado ni medido allí; 0,1 CPU y la
  suspensión por inactividad cambian por completo el rendimiento.
- **Vídeos mayores de 25 MiB**: el límite por defecto y la cuota de base de
  datos gratuita lo desaconsejan.
- **Comparación con una implementación en Python**: la especificación pide no
  inventar porcentajes de mejora. No se dispone de una versión Python del
  sistema que medir en igualdad de condiciones, así que no se afirma ninguna
  mejora porcentual.
