add_library(tds_shared STATIC
  src/encoding.cpp
  src/sha256.cpp
  src/crc32.cpp
  src/codec.cpp
  src/job.cpp
  src/framing.cpp
  src/util.cpp
  src/reassembler.cpp
  src/transport.cpp
  src/config.cpp
  src/http.cpp
  src/log.cpp
  src/pinger.cpp
  src/protobuf_auth.cpp
  src/xmpp.cpp
)

target_include_directories(tds_shared
  PUBLIC ${CMAKE_SOURCE_DIR}/shared/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tds_shared
  PUBLIC OpenSSL::Crypto
         OpenSSL::SSL
         CURL::libcurl
         spdlog::spdlog
         nlohmann_json::nlohmann_json
         Threads::Threads
)

target_compile_definitions(tds_shared PRIVATE
  TDS_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/shared/resources"
)

if(BUILD_TESTING)
  add_executable(tds_tests_shared
    tests/test_encoding.cpp
    tests/test_framing.cpp
    tests/test_protocol.cpp
    tests/test_xmpp.cpp
  )
  target_link_libraries(tds_tests_shared PRIVATE tds_shared GTest::gtest_main)
  target_compile_definitions(tds_tests_shared PRIVATE
    TDS_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/shared/resources"
  )
  add_test(NAME shared COMMAND tds_tests_shared)
endif()
