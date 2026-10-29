cmake_minimum_required(VERSION 3.22)
project(ing_inversa VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)

# BUILD_TESTING lo define CTest (ON por defecto). En la imagen de producción se
# desactiva (`-DBUILD_TESTING=OFF`) para no exigir GoogleTest.
include(CTest)
if(BUILD_TESTING)
  find_package(GTest REQUIRED)
endif()

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
find_package(ZLIB REQUIRED)

# Drogon de Ubuntu trae un DrogonConfig que exige cabeceras de MySQL, Brotli y
# Hiredis que no necesitamos. Enlazamos contra el paquete instalado sin pasar
# por ese config, manteniendo el conjunto de dependencias mínimo.
find_path(DROGON_INCLUDE_DIR drogon/drogon.h REQUIRED)
find_library(DROGON_LIBRARY drogon REQUIRED)
find_path(TRANTOR_INCLUDE_DIR trantor/net/EventLoop.h REQUIRED)
find_library(TRANTOR_LIBRARY trantor REQUIRED)
find_path(JSONCPP_INCLUDE_DIR json/json.h PATH_SUFFIXES jsoncpp REQUIRED)
find_library(JSONCPP_LIBRARY jsoncpp REQUIRED)
add_library(drogon_local UNKNOWN IMPORTED)
set_target_properties(drogon_local PROPERTIES
  IMPORTED_LOCATION "${DROGON_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${DROGON_INCLUDE_DIR};${TRANTOR_INCLUDE_DIR};${JSONCPP_INCLUDE_DIR}"
  INTERFACE_LINK_LIBRARIES "${TRANTOR_LIBRARY};${JSONCPP_LIBRARY};ZLIB::ZLIB"
)
add_library(Drogon::Drogon ALIAS drogon_local)

# ---------------------------------------------------------------------------
# Bibliotecas y ejecutables
# ---------------------------------------------------------------------------
add_subdirectory(shared)
add_subdirectory(storage)
add_subdirectory(server_a)
add_subdirectory(server_b)
add_subdirectory(client)
add_subdirectory(benchmarks)
