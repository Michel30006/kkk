add_executable(tds_server_b
  src/main.cpp
  src/api.cpp
  src/worker.cpp
  src/todus_transport.cpp
)

target_link_libraries(tds_server_b PRIVATE tds_storage Drogon::Drogon)

if(BUILD_TESTING)
  add_executable(tds_tests_server_b
    tests/test_worker_logic.cpp
    src/worker.cpp
    src/todus_transport.cpp
  )
  target_include_directories(tds_tests_server_b PRIVATE src)
  target_link_libraries(tds_tests_server_b PRIVATE tds_storage GTest::gtest_main)
  target_compile_definitions(tds_tests_server_b PRIVATE
    TDS_MIGRATIONS_DIR="${CMAKE_SOURCE_DIR}/database/migrations"
  )
  add_test(NAME server_b_units COMMAND tds_tests_server_b)
  set_tests_properties(server_b_units PROPERTIES
    ENVIRONMENT "TEST_DATABASE_URL=postgresql://tds:tds@127.0.0.1:5432/tds_test")
endif()
