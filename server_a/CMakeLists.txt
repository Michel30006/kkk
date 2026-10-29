add_executable(tds_server_a
  src/main.cpp
  src/api.cpp
  src/pipeline.cpp
  src/downloader.cpp
  src/url_guard.cpp
)

target_link_libraries(tds_server_a PRIVATE tds_storage Drogon::Drogon)
target_compile_definitions(tds_server_a PRIVATE
  TDS_MIGRATIONS_DIR="${CMAKE_SOURCE_DIR}/database/migrations"
)

if(BUILD_TESTING)
  add_executable(tds_tests_server_a
    tests/test_url_guard.cpp
    src/url_guard.cpp
  )
  target_link_libraries(tds_tests_server_a PRIVATE tds_shared GTest::gtest_main)
  target_include_directories(tds_tests_server_a PRIVATE src)
  add_test(NAME server_a_units COMMAND tds_tests_server_a)
endif()
