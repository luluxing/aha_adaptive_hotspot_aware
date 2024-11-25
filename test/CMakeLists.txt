# ADD executables
enable_testing()

# Hello-world
add_executable(
  gtest_hw
  gtest_hello_world.cc
)
target_link_libraries(
  gtest_hw
  GTest::gtest_main
)
# Test page functions
add_executable(
  gtest_page
  gtest_page.cc
)
target_link_libraries(
  gtest_page
  GTest::gtest_main WotAdapt
)
# Test page functions
add_executable(
  gtest_page_olc
  gtest_page_olc.cc
)
target_link_libraries(
  gtest_page_olc
  GTest::gtest_main WotAdapt
)
# Test buffer manager
add_executable(
  gtest_buffer_mgr
  gtest_buffer_mgr.cc
)
target_link_libraries(
  gtest_buffer_mgr
  GTest::gtest_main WotAdapt
)
# Test tree function
add_executable(
  gtest_btree
  gtest_btree.cc
)
target_link_libraries(
  gtest_btree
  GTest::gtest_main WotAdapt
)
# Test btreeOLC function
add_executable(
  gtest_btreeolc
  gtest_btreeolc.cc
)
target_link_libraries(
  gtest_btreeolc
  GTest::gtest_main WotAdapt
)
# Test page split function
add_executable(
  gtest_page_split
  gtest_page_split_policy.cc
)
target_link_libraries(
  gtest_page_split
  GTest::gtest_main WotAdapt
)
# Test page split function
add_executable(
  gtest_priority_queue
  gtest_priority_queue.cc
)
target_link_libraries(
  gtest_priority_queue
  GTest::gtest_main WotAdapt
)

# Test LSMT function
# add_executable(
#   gtest_lsmt
#   gtest_lsmt.cc
# )
# target_link_libraries(
#   gtest_lsmt
#   GTest::gtest_main WotAdapt
# )
# Test wot function
# add_executable(
#   gtest_wot
#   gtest_wot.cc
# )
# target_link_libraries(
#   gtest_wot
#   GTest::gtest_main WotAdapt
# )
# Test lsmt to wot function
# add_executable(
#   gtest_lsmt2wot
#   gtest_lsmt2wot.cc
# )
# target_link_libraries(
#   gtest_lsmt2wot
#   GTest::gtest_main WotAdapt
# )
# Test Multithreaded function
# add_executable(
#   gtest_multi_wot
#   gtest_multi_wot.cc
# )
# target_link_libraries(
#   gtest_multi_wot
#   GTest::gtest_main WotAdapt pthread
# )

include(GoogleTest)
gtest_discover_tests(gtest_hw)
gtest_discover_tests(gtest_page)
gtest_discover_tests(gtest_buffer_mgr)
gtest_discover_tests(gtest_btree)
# gtest_discover_tests(gtest_lsmt)
# gtest_discover_tests(gtest_wot)
# gtest_discover_tests(gtest_lsmt2wot)
# gtest_discover_tests(gtest_multi_wot)