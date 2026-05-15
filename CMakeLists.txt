cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)
project(3fish VERSION 1.0.0 LANGUAGES CXX C)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<CONFIG:Debug,RelWithDebInfo>,EditAndContinue,ProgramDatabase>")
  add_compile_options(/EHsc)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(Boost_USE_STATIC_LIBS ON)
set(OPENSSL_USE_STATIC_LIBS TRUE)

find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(simdjson CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(implot CONFIG REQUIRED)

add_subdirectory(src)

option(BUILD_TESTS "Build the unit tests" ON)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
