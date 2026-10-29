add_executable(tds_bench src/bench.cpp)
target_link_libraries(tds_bench PRIVATE tds_shared)
