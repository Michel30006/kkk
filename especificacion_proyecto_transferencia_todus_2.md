# Especificación técnica del proyecto: transferencia de vídeos mediante ToDus

**Versión:** 2.0\
**Fecha:** 24 de octubre de 2026\
**Propósito:** documento de requisitos para una IA que implementará el
proyecto íntegramente en C++.

> **Estado del proyecto:** la arquitectura y el objetivo general están
> definidos, pero no se ha proporcionado aquí un repositorio de código
> completo ni una interfaz de ToDus verificada. La IA desarrolladora
> debe inspeccionar los archivos que reciba y comprobar las
> integraciones reales. No debe inventar endpoints ni declarar funcional
> una integración que no haya probado.

## 1. Objetivo

### Decisión tecnológica obligatoria: C++ en todo el sistema

La implementación debe hacerse en **C++ de extremo a extremo**. No se debe
implementar la lógica principal en Python ni introducir servicios Python como
parte de la solución final. Esta decisión incluye los servidores A y B, la
descarga, codificación/decodificación, fragmentación, comunicación HTTP,
acceso a datos, gestión de trabajos, pruebas automatizadas y herramientas de
construcción que formen parte del proyecto.

El cliente móvil también debe usar C++ para la lógica compartida de protocolo,
recepción, reconstrucción y verificación. Si la plataforma móvil exige código
nativo de integración para interfaz o ciclo de vida (por ejemplo, Kotlin/Java
en Android), ese código debe limitarse a la capa de integración imprescindible;
la lógica de transferencia seguirá en una biblioteca C++ y no se duplicará. Si
existe ya un cliente o una integración de ToDus, inspeccionarla antes de
modificarla y conservar su funcionamiento.

La elección de C++ busca buen rendimiento y control de memoria, pero no autoriza
a afirmar porcentajes de mejora sin mediciones. El diseño debe priorizar primero
la corrección, el procesamiento por bloques y la reducción de copias; después se
medirá y optimizará el rendimiento real.

Permitir que un usuario inicie desde su teléfono el procesamiento de un
enlace de vídeo, que dos servicios en Render procesen y transporten sus
datos mediante ToDus y que el teléfono reconstruya el archivo original a
partir de los fragmentos recibidos.

El sistema consta de: - **Servidor A (Render):** recibe el enlace,
descarga el vídeo y lo codifica. - **Base de datos temporal
compartida:** mantiene trabajos, estados y datos mientras se completa la
operación. - **Servidor B (Render):** recupera los datos, los divide en
fragmentos y los envía por ToDus mediante una integración real y
verificada. - **Cliente del teléfono:** inicia el trabajo, recibe los
fragmentos, reconstruye el archivo y verifica su integridad.

## 2. Arquitectura y flujo completo

``` text
TELÉFONO
  │ envía enlace y datos necesarios
  ▼
SERVIDOR A — RENDER
  ├─ valida la solicitud
  ├─ crea un job_id
  ├─ descarga el vídeo autorizado
  ├─ calcula tamaño y SHA-256
  ├─ convierte bytes directamente a caracteres
  └─ guarda el resultado y el estado
  │
  ▼
BASE DE DATOS / ALMACENAMIENTO TEMPORAL COMPARTIDO
  │
  ▼
SERVIDOR B — RENDER
  ├─ toma el trabajo listo
  ├─ divide el resultado en fragmentos
  ├─ envía los fragmentos mediante ToDus
  ├─ registra intentos y confirmaciones
  └─ espera confirmación de finalización
  │
  ▼
TELÉFONO
  ├─ recibe y ordena los fragmentos
  ├─ detecta fragmentos perdidos o duplicados
  ├─ convierte los caracteres de vuelta a bytes
  ├─ reconstruye el archivo
  ├─ verifica tamaño y SHA-256
  └─ confirma que terminó correctamente
  │
  ▼
LIMPIEZA DE DATOS TEMPORALES
```

### Flujo detallado

1.  El usuario introduce un enlace en el cliente.
2.  El cliente envía una solicitud al Servidor A.
3.  A valida la solicitud y crea un identificador único `job_id`.
4.  A descarga el vídeo y registra el tamaño y el hash SHA-256 del
    archivo descargado.
5.  A codifica los bytes mediante una tabla fija de 256 caracteres.
6.  A guarda el resultado, los metadatos y el estado en el
    almacenamiento temporal compartido.
7.  A marca el trabajo como `ready_for_delivery`.
8.  B localiza y reserva el trabajo para evitar que dos procesos lo
    envíen a la vez.
9.  B divide los datos en fragmentos que respeten el límite real de
    ToDus.
10. B envía los fragmentos y registra el progreso.
11. El cliente recibe los fragmentos, los valida y los ordena.
12. El cliente reconstruye los bytes originales y compara el hash
    SHA-256.
13. El cliente comunica el resultado final.
14. Solo después de una confirmación suficiente de finalización, el
    sistema elimina los datos temporales.

La descarga debe limitarse a contenido al que el usuario tenga derecho
de acceso y respetar las condiciones de la fuente. No se deben evadir
DRM ni controles de acceso.

## 3. Responsabilidades por componente

### 3.1 Cliente móvil

Debe: - permitir introducir/pegar el enlace; - iniciar una operación y
mostrar su `job_id`; - mostrar estados y progreso cuando se conozcan; -
recibir fragmentos por un mecanismo que realmente exista; - detectar
fragmentos duplicados, ausentes, dañados o fuera de orden; - reconstruir
y guardar el archivo; - verificar tamaño y SHA-256; - mostrar errores
comprensibles y permitir reintentar cuando sea seguro.

**Punto crítico:** antes de desarrollar el transporte, hay que confirmar
qué interfaz de ToDus está realmente disponible, autorizada y
técnicamente utilizable para enviar y recibir mensajes. No se debe
fingir una API pública ni inventar endpoints. Si la integración no es
viable, documentar el bloqueo antes de continuar.

### 3.2 Servidor A --- descarga y codificación

Debe: - exponer un endpoint para crear trabajos; - validar enlaces,
tamaño máximo y parámetros; - protegerse contra SSRF y contra acceso a
direcciones internas; - descargar el vídeo con límites de tiempo y
tamaño; - calcular el hash SHA-256 de los bytes descargados; - codificar
directamente desde los bytes, sin cadenas intermedias binarias ni
hexadecimales; - guardar los datos y metadatos en almacenamiento
compartido; - actualizar los estados del trabajo; - eliminar archivos
locales temporales al terminar o fallar; - exponer endpoints ligeros de
salud y estado; - registrar errores sin filtrar secretos.

### 3.3 Servidor B --- envío

Debe: - localizar trabajos `ready_for_delivery`; - reservar cada trabajo
de forma atómica; - obtener los datos en orden; - fragmentarlos según
límites reales comprobados; - enviar por la integración real de ToDus; -
gestionar confirmaciones, errores y reintentos limitados; - evitar
envíos duplicados tras reinicios; - registrar progreso; - marcar la
entrega según la evidencia disponible; - no eliminar los datos antes de
la confirmación final acordada.

Enviar un mensaje no demuestra por sí solo que el teléfono haya
reconstruido correctamente el archivo.

### 3.4 Base de datos y almacenamiento temporal

Debe guardar al menos: - `job_id`; - estado del trabajo; - fechas de
creación y actualización; - información estrictamente necesaria del
destinatario; - tamaño original; - hash SHA-256; - versión de la tabla
de codificación; - cantidad de fragmentos; - estado e índice de cada
fragmento; - intentos de envío y errores resumidos; - fecha de
finalización.

A y B deben acceder al mismo estado de trabajo. La tecnología se elige
después de verificar los servicios y límites gratuitos vigentes.

La limpieza es obligatoria, pero no se debe vaciar la base de datos al
comenzar el envío ni al terminar de enviar sin confirmación. Primero se
necesita una confirmación de reconstrucción e integridad, o una política
de expiración segura si el cliente no puede confirmar.

## 4. Codificación directa de bytes a caracteres

### Optimización acordada

Conversión anterior planteada:

``` text
bytes → texto binario → texto hexadecimal → caracteres
```

Conversión optimizada:

``` text
cada byte original → un carácter de una tabla fija de 256 símbolos
```

La nueva ruta elimina las cadenas intermedias binarias y hexadecimales,
reduce conversiones y puede reducir notablemente el uso de memoria
temporal.

### Requisitos de la tabla

-   Debe contener exactamente 256 símbolos imprimibles.
-   Cada valor de byte entre `0x00` y `0xFF` debe tener una
    correspondencia.
-   Todos los símbolos deben ser únicos; no puede haber ambigüedad al
    decodificar.
-   La tabla debe ser fija, documentada, versionada y compartida por
    servidor y cliente.
-   No se debe generar aleatoriamente ni depender del orden accidental
    de estructuras.
-   Debe existir una función inversa que recupere exactamente cada byte.
-   La tabla definitiva aún debe seleccionarse y probarse
    explícitamente.

**"256 caracteres imprimibles" no basta por sí solo:** hay que
especificar los 256 símbolos concretos y comprobar cómo se comportan en
el lenguaje, la base de datos y el transporte.

### Unicode y transporte

Algunos caracteres imprimibles ocupan más de un byte en UTF-8. Eso no
impide necesariamente la conversión, pero aumenta el tamaño transmitido.
Debe comprobarse que: - la base de datos conserva los símbolos sin
alterarlos; - ToDus no elimina, normaliza, sustituye ni interpreta
símbolos de forma especial; - el límite del mensaje se calcula con la
medida real que usa ToDus, no suponiendo que cada carácter equivale a un
byte; - la versión de tabla llega al decodificador; - una prueba de ida
y vuelta produce exactamente los bytes originales.

### Pruebas obligatorias

1.  Probar individualmente los 256 valores de byte.
2.  Probar una secuencia con todos los valores.
3.  Probar datos binarios aleatorios.
4.  Probar un archivo de vídeo pequeño.
5.  Codificar y decodificar cada muestra.
6.  Comparar los bytes y los hashes SHA-256.
7.  Repetir tras guardar y leer desde la base de datos.
8.  Repetir a través del canal real de ToDus cuando esté integrado.

## 5. Fragmentación y reconstrucción

No enviar el vídeo completo como un único mensaje. El protocolo lógico
de cada fragmento debe identificar, como mínimo: - `job_id`; -
identificador de transferencia si hace falta distinguir reintentos; -
`chunk_index`, comenzando en cero; - `total_chunks`; -
`encoding_version`; - contenido (`payload`); - checksum del fragmento si
resulta útil.

La estructura final debe adaptarse a los límites y capacidades reales de
ToDus. Los campos no deben concatenarse ingenuamente sin una
codificación de protocolo inequívoca.

El receptor debe: - aceptar fragmentos fuera de orden si puede
almacenarlos; - detectar duplicados y no añadirlos dos veces; - rechazar
índices fuera de rango; - detectar fragmentos ausentes; - verificar que
todos pertenecen al mismo trabajo; - comprobar que llegaron todos antes
de reconstruir; - reconstruir en orden; - decodificar con la misma
tabla; - comparar tamaño y SHA-256; - informar de forma clara si la
verificación falla.

El tamaño de fragmento debe ser configurable y determinarse con pruebas
y documentación. Debe contar el tamaño total del mensaje, incluidos
encabezados y metadatos.

## 6. Estados de trabajo

Estados principales recomendados:

``` text
created → queued → downloading → encoding
        → ready_for_delivery → sending
        → awaiting_client_confirmation
        → completed → cleaned
```

Estados de error posibles: - `failed_download` - `failed_encoding` -
`failed_delivery` - `failed_reconstruction` - `cancelled` - `expired`

Todos los cambios deben persistirse. Las solicitudes repetidas deben ser
idempotentes en la medida de lo posible. Debe existir una política
configurable para trabajos atascados o abandonados y limpiar sus datos
después del periodo establecido.

## 7. Solicitudes mutuas cada 10 minutos

### Decisión acordada

A y B intentarán enviarse solicitudes HTTP mutuamente cada 10 minutos:

``` text
Servidor A ───── solicitud HTTP cada 10 min ─────► Servidor B
Servidor B ◄──── solicitud HTTP cada 10 min ────── Servidor A
```

Cada servidor debe ofrecer un endpoint ligero como `GET /health`. No
debe iniciar descargas ni ejecutar trabajo pesado.

### Advertencia

Esto es un **intento** de evitar la suspensión por inactividad de Render
gratuito; no garantiza que ambos servicios permanezcan activos
indefinidamente. El proveedor puede aplicar restricciones, reiniciar
servicios o cambiar las condiciones del plan. La IA debe revisar la
documentación y las condiciones vigentes de Render antes de
implementarlo.

El sistema no debe depender de que los dos servicios estén siempre
despiertos. Debe tolerar que una petición falle, un servicio tarde en
despertar o un proceso se reinicie.

Implementación: - intervalo configurable, inicialmente 10 minutos; -
timeout en cada solicitud; - registro de intentos y resultados; -
reintentos limitados con espera progresiva; - sin bucles de solicitudes
intensivas; - secretos fuera de URLs y logs; - respetar las condiciones
del proveedor.

## 8. Restricciones de Render gratuito que deben verificarse

Antes de desplegar, comprobar la documentación vigente sobre: - RAM y
CPU por servicio; - suspensión por inactividad y tiempo de
reactivación; - duración máxima de peticiones HTTP; - disponibilidad de
procesos en segundo plano en el plan gratuito; - almacenamiento local
efímero y comportamiento tras reinicios; - límites, caducidad y
condiciones de la base de datos gratuita; - tráfico, ancho de banda y
número de servicios; - política sobre solicitudes periódicas de
mantenimiento.

No asumir que una descarga o conversión larga seguirá viva porque el
cliente mantenga abierta una petición HTTP. Los procesos deben fallar de
forma controlada y reanudarse cuando sea viable.

No usar el disco local de A como único almacenamiento de los datos que
necesita B. Los datos compartidos deben guardarse en un almacenamiento
accesible para ambos.

## 9. API interna propuesta

Estos son contratos sugeridos, no endpoints existentes.

### Servidor A

-   `POST /jobs`: validar solicitud y crear trabajo.
-   `GET /jobs/{job_id}`: consultar estado, con autorización.
-   `GET /health`: salud básica.
-   `GET /ready`: opcional, indica si puede aceptar trabajos.

La creación debe devolver un `job_id` y un estado inicial. No depender
necesariamente de completar toda la descarga dentro de una única
petición HTTP.

### Servidor B

-   `POST /jobs/{job_id}/deliver`: solicitar/programar entrega.
-   `GET /jobs/{job_id}`: consultar estado de entrega, con autorización.
-   `GET /health`: salud básica.
-   `GET /ready`: opcional.

Las operaciones internas deben autenticarse. No exponer públicamente
operaciones de envío sin autorización.

## 10. Seguridad, privacidad y registros

-   Validar enlaces y rechazar protocolos no permitidos.
-   Prevenir SSRF y acceso a direcciones privadas arbitrarias.
-   Limitar tamaño de vídeo, tiempo de descarga y concurrencia.
-   Guardar claves y tokens como variables de entorno, nunca en el
    código.
-   Crear `.env.example` sin credenciales reales.
-   Proteger endpoints que crean trabajos o consultan datos privados.
-   Comprobar que el solicitante tiene acceso al `job_id`.
-   No registrar contenido completo del vídeo, tokens ni datos
    personales innecesarios.
-   Evitar que la limpieza borre datos de otro trabajo.
-   Eliminar archivos temporales al finalizar o fallar.
-   Respetar las condiciones de la fuente del vídeo y de ToDus.

Logs mínimos: `job_id`, etapa, hora, tamaño, cantidad de fragmentos,
progreso, reintentos, duración, estado final y tipo de error seguro.

## 11. Lenguaje, bibliotecas y compilación

### Reglas obligatorias

- Usar una versión moderna de C++ compatible con las herramientas de despliegue;
  preferir **C++20** si las dependencias y el entorno lo permiten. Si no, usar
  C++17 y documentar el motivo.
- Usar **CMake** como sistema de construcción, salvo que el repositorio ya tenga
  un sistema C++ funcional que convenga conservar.
- Compilar en un entorno reproducible y desplegar los servicios como ejecutables
  nativos o contenedores Docker. No depender de un entorno Python en producción.
- Fijar las versiones de las dependencias y documentar cómo instalarlas y
  compilar el proyecto desde cero.
- No escribir ensamblador manual de entrada. Usar C++ optimizado y bibliotecas
  probadas; añadir SIMD o ensamblador solo si un benchmark demuestra una mejora
  relevante y se mantiene una alternativa correcta para otras CPU.

### Bibliotecas candidatas (validar antes de elegir)

Estas son opciones técnicas, no dependencias que deban instalarse todas. La IA
debe comprobar compatibilidad, mantenimiento, licencia y disponibilidad en el
entorno de compilación antes de decidir:

- **Servidor HTTP:** Drogon o Boost.Beast. Elegir una opción principal y evitar
  mantener dos frameworks HTTP sin necesidad.
- **Descargas HTTP/HTTPS:** libcurl; configurar timeouts, límites de tamaño,
  validación TLS, redirecciones seguras y cancelación.
- **JSON:** nlohmann/json o la biblioteca incluida con el framework elegido.
- **SHA-256 y primitivas criptográficas:** OpenSSL EVP, evitando APIs antiguas.
- **Base de datos PostgreSQL:** libpqxx si se selecciona PostgreSQL. Usar
  transacciones, consultas parametrizadas y migraciones versionadas.
- **Almacenamiento de objetos:** cliente C++ compatible con el proveedor elegido
  o su API S3, si se necesita almacenar los datos grandes fuera de la base de
  datos. Verificar primero la disponibilidad real y el coste/límite del servicio.
- **Pruebas:** GoogleTest o Catch2.
- **Logging:** spdlog u opción equivalente del framework. No registrar secretos.

La selección final debe ser pequeña y justificada. No asumir que Render ofrece
una base de datos, almacenamiento de objetos o ejecución en segundo plano sin
comprobar los límites vigentes. Los datos binarios o codificados de gran tamaño
no deben guardarse automáticamente como enormes campos de texto en una base de
datos si un almacenamiento de objetos resulta más adecuado. La base de datos
debe conservar los metadatos, estados y referencias necesarias.

### Estructura de código sugerida

Adaptar esta estructura al repositorio real después de inspeccionarlo:

``` text
proyecto/
├── CMakeLists.txt
├── server_a/
│   ├── src/
│   └── tests/
├── server_b/
│   ├── src/
│   └── tests/
├── shared/
│   ├── include/
│   ├── src/
│   └── tests/
├── mobile_client/
│   ├── cpp/
│   └── platform_bridge/   # solo si la plataforma lo exige
├── database/              # esquema y migraciones
├── deploy/                # Dockerfiles y configuración de despliegue
├── docs/
├── .env.example
└── README.md
```

## 12. Plan de implementación por fases

### Fase 1: inspección y decisiones

1.  Inspeccionar repositorio, archivos, dependencias y configuración.
2.  Elegir framework compatible con el alojamiento.
3.  Verificar límites vigentes de Render.
4.  Verificar la interfaz real de ToDus.
5.  Elegir almacenamiento compartido.
6.  Definir la tabla de 256 símbolos.
7.  Definir límites de vídeo, mensajes y reintentos.
8.  Documentar bloqueos antes de programar dependencias críticas.

**Entregable:** decisiones técnicas y riesgos confirmados.

### Fase 2: codificación aislada

1.  Definir tabla fija.
2.  Implementar bytes → caracteres y caracteres → bytes.
3.  Probar todos los valores de byte.
4.  Probar archivos binarios.
5.  Comparar SHA-256.

**Entregable:** conversión reversible demostrada.

### Fase 3: base de datos

1.  Crear esquema y migraciones.
2.  Implementar estados y transiciones.
3.  Guardar metadatos y fragmentos.
4.  Implementar limpieza segura.
5.  Probar duplicados y trabajos simultáneos.

### Fase 4: Servidor A

1.  Crear endpoint de trabajos.
2.  Validar enlaces y límites.
3.  Descargar un vídeo de prueba autorizado.
4.  Codificar directamente los bytes.
5.  Guardar resultado y hash.
6.  Exponer estado y salud.
7.  Gestionar errores y temporales.

### Fase 5: Servidor B y ToDus

1.  Verificar la integración real.
2.  Implementar adaptador de transporte.
3.  Determinar límite de mensaje mediante pruebas.
4.  Fragmentar y enviar.
5.  Implementar confirmaciones y reintentos.
6.  Evitar duplicados tras reinicios.
7.  Registrar progreso y errores.

Si no existe una interfaz utilizable, detener esta fase y documentar el
bloqueo; no simular una integración terminada.

### Fase 6: cliente y reconstrucción

1.  Implementar inicio de trabajo.
2.  Implementar recepción por el mecanismo disponible.
3.  Validar y ordenar fragmentos.
4.  Detectar faltantes y duplicados.
5.  Reconstruir bytes.
6.  Comparar tamaño y hash.
7.  Guardar archivo y mostrar resultado.

### Fase 7: mantenimiento y recuperación

1.  Crear endpoints de salud.
2.  Implementar solicitudes mutuas cada 10 minutos, sujeto a las
    condiciones de Render.
3.  Probar suspensión y reactivación.
4.  Probar reinicios durante cada etapa.
5.  Recuperar o marcar trabajos atascados.
6.  Limpiar trabajos expirados de forma segura.

### Fase 8: prueba integral

Con un vídeo pequeño y autorizado: 1. enviar enlace; 2. descargar y
codificar en A; 3. guardar en la base de datos; 4. fragmentar y enviar
en B; 5. recibir en el teléfono; 6. reconstruir; 7. comparar SHA-256; 8.
confirmar finalización; 9. comprobar la limpieza.

Después probar archivos mayores, cortes de conexión, mensajes perdidos,
duplicados, reinicios y límites de recursos.

## 13. Criterios de aceptación

-   [ ] Tabla documentada con 256 símbolos únicos e imprimibles.
-   [ ] Conversión reversible para todos los valores de byte.
-   [ ] No se generan cadenas intermedias de binario ni hexadecimal.
-   [ ] Hash del archivo reconstruido idéntico al del descargado.
-   [ ] Cada trabajo tiene ID y estado persistente.
-   [ ] A y B comparten el estado de forma segura.
-   [ ] El tamaño de fragmento respeta límites reales comprobados.
-   [ ] Se detectan fragmentos perdidos, repetidos, dañados y fuera de
    orden.
-   [ ] Los reintentos no provocan duplicación descontrolada.
-   [ ] La limpieza ocurre tras la confirmación acordada.
-   [ ] El sistema maneja fallos y reinicios de forma controlada.
-   [ ] El mantenimiento cada 10 minutos está monitorizado y no se
    presenta como garantía.
-   [ ] ToDus está probado con una integración real, no simulada.
-   [ ] Los límites de Render y del transporte están documentados.
-   [ ] README explica configuración, pruebas y despliegue.
-   [ ] La lógica de producción está implementada en C++; no depende de Python.
-   [ ] La compilación con CMake es reproducible desde un entorno limpio.
-   [ ] Hay benchmarks de duración, CPU y RAM, con resultados medidos y no estimados.

## 14. Instrucciones obligatorias para la IA desarrolladora

1.  Inspeccionar el repositorio antes de modificar archivos existentes.
2.  Implementar en C++ toda la lógica del sistema; no usar Python para los
    servidores ni para componentes de producción.
3.  No inventar APIs ni afirmar que ToDus funciona sin pruebas reales.
4.  Desarrollar por fases pequeñas y verificables.
5.  Ejecutar pruebas después de cada fase y compilar con advertencias activadas.
6.  Priorizar la integridad binaria exacta.
7.  Mantener una única tabla de codificación versionada compartida.
8.  Evitar copias completas e innecesarias de los datos en RAM; usar procesamiento
    por bloques y flujos cuando sea viable.
9.  No depender del disco efímero de Render para compartir resultados.
10. No borrar datos antes de confirmar la reconstrucción o aplicar una política
    de expiración segura.
11. Implementar timeouts, reintentos limitados, cancelación y recuperación.
12. Verificar las limitaciones vigentes de Render y ToDus antes de depender de
    una función concreta.
13. Actualizar README y documentación con instrucciones reproducibles de CMake,
    compilación, pruebas, variables de entorno y despliegue.
14. Nunca incluir secretos reales en el repositorio.
15. No declarar éxito sin una prueba que compare el SHA-256 del original y el
    reconstruido.
16. Añadir benchmarks reproducibles para medir duración, uso de CPU y memoria
    con archivos de prueba de distintos tamaños. Comparar resultados medidos;
    no inventar porcentajes de mejora frente a Python.
17. Compilar y probar cada servicio en el mismo tipo de entorno que se usará en
    Render, preferiblemente mediante Docker si esa es la estrategia de despliegue.
18. Si una biblioteca C++ o la integración de ToDus no resulta viable, documentar
    el bloqueo y las alternativas antes de cambiar el lenguaje o la arquitectura.

### Entregables finales

-   código de A y B;
-   cliente móvil o componente receptor acordado;
-   esquema/migraciones de base de datos;
-   tabla fija de codificación;
-   pruebas unitarias e integración;
-   `.env.example`;
-   README de instalación y despliegue;
-   diagrama de arquitectura actualizado;
-   informe de limitaciones confirmadas;
-   informe de prueba integral con tamaños y hashes.

## 15. Riesgos y decisiones pendientes

1.  **ToDus:** confirmar cómo enviar y recibir mensajes de forma
    técnicamente viable y autorizada.
2.  **Tabla de símbolos:** elegir y validar explícitamente los 256
    caracteres.
3.  **Límites de mensajes:** confirmar el tamaño máximo y calcular los
    fragmentos.
4.  **Render gratuito:** verificar si los recursos y límites permiten
    descargas y conversiones largas.
5.  **Almacenamiento temporal:** escoger una opción compartida
    compatible con los límites del plan.
6.  **Confirmación de recepción:** distinguir mensaje enviado, fragmento
    recibido y archivo reconstruido.
7.  **Tamaño máximo de vídeo:** definirlo mediante pruebas de CPU, RAM,
    almacenamiento y ancho de banda.
8.  **Concurrencia:** comenzar con una operación simultánea o un límite
    pequeño y aumentarlo solo después de medir.

## 16. Resumen ejecutivo

La arquitectura acordada utiliza dos servicios en Render y una base de
datos/almacenamiento temporal compartido. **Todo el sistema se implementará en
C++**, incluidos los servidores A y B y la lógica de transferencia del cliente.
A descarga el vídeo y transforma cada byte directamente en un carácter de una
tabla fija de 256 símbolos, sin conversiones intermedias a binario y hexadecimal.
B recupera los datos, los fragmenta y los transporta mediante una integración
ToDus real y verificada. El teléfono reúne los fragmentos, convierte los
caracteres a bytes y verifica el archivo con SHA-256. Una vez confirmada la
reconstrucción, se eliminan los datos temporales.

C++ es la decisión de implementación, no una garantía de una mejora porcentual
concreta: el rendimiento debe medirse con benchmarks del sistema completo.

A y B intentarán enviarse solicitudes HTTP mutuamente cada 10 minutos
para reducir la suspensión por inactividad, pero esto no garantiza
disponibilidad permanente.

**La condición principal de éxito es demostrar una transferencia
integral y una reconstrucción byte por byte idéntica.** Los puntos
críticos que deben verificarse antes de prometer que el sistema
funcionará son la interfaz real de ToDus, la tabla concreta de 256
caracteres, los límites de los mensajes y las restricciones vigentes de
Render gratuito.
