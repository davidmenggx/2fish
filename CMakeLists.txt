cmake_minimum_required(VERSION 3.24)

project(3fish VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(Boost_USE_STATIC_LIBS ON)
set(OPENSSL_USE_STATIC_LIBS TRUE)

find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(simdjson CONFIG REQUIRED)

add_subdirectory(src)

option(BUILD_TESTS "Build the unit tests" ON)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
