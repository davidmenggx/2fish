find_package(GTest CONFIG REQUIRED)

add_executable(3fishTests
  placeholder_test.cpp
)

target_link_libraries(3fishTests PRIVATE
  GTest::gtest
  GTest::gtest_main
  3fishCore
)

include(GoogleTest)
gtest_discover_tests(3fishTests)
