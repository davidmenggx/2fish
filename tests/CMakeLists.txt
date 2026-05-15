find_package(GTest CONFIG REQUIRED)

add_executable(2fishTests
  test_websocket_parser.cpp
  test_rest_parser.cpp
)

target_link_libraries(2fishTests PRIVATE
  GTest::gtest
  GTest::gtest_main
  2fishCore
)

include(GoogleTest)
gtest_discover_tests(2fishTests)
