add_executable(tds_receiver src/main.cpp src/receiver_core.cpp)

target_link_libraries(tds_receiver PRIVATE tds_shared)

if(BUILD_TESTING)
  add_executable(tds_tests_client tests/test_receiver.cpp src/receiver_core.cpp)
  target_include_directories(tds_tests_client PRIVATE src)
  target_link_libraries(tds_tests_client PRIVATE tds_shared GTest::gtest_main)
  add_test(NAME client_units COMMAND tds_tests_client)
endif()
