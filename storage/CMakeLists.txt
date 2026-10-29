add_library(tds_storage STATIC src/pg_store.cpp src/maintenance.cpp)

target_include_directories(tds_storage PUBLIC
  ${CMAKE_SOURCE_DIR}/storage/include
)

target_link_libraries(tds_storage
  PUBLIC tds_shared PkgConfig::PQXX PostgreSQL::PostgreSQL
)

if(BUILD_TESTING)
  add_executable(tds_tests_storage tests/test_storage.cpp)
  target_link_libraries(tds_tests_storage PRIVATE tds_storage GTest::gtest_main)
  target_compile_definitions(tds_tests_storage PRIVATE
    TDS_MIGRATIONS_DIR="${CMAKE_SOURCE_DIR}/database/migrations"
  )
  add_test(NAME storage COMMAND tds_tests_storage)
  set_tests_properties(storage PROPERTIES
    ENVIRONMENT "TEST_DATABASE_URL=postgresql://tds:tds@127.0.0.1:5432/tds_test")
endif()
