add_subdirectory(core)
add_subdirectory(db)

add_executable(ycsb ycsbc.cc)
# add_executable(correctness correctness.cc)
add_executable(plot_wl plot_workload.cc)
add_executable(test_cc test_concurrent_map.cc)
add_executable(test_cpu_usage test_cpu_usage.cc)

find_package(TBB REQUIRED PATHS "/home/xingl/software/my_tbb/" NO_DEFAULT_PATH) 

target_link_libraries(ycsb pthread core_workload db_factory)
# target_link_libraries(correctness pthread core_workload db_factory)
target_link_libraries(plot_wl core_workload db_factory)
target_link_libraries(test_cc pthread TBB::tbb)
target_link_libraries(test_cpu_usage pthread)