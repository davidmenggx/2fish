cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW) 
if (POLICY CMP0141)    
    cmake_policy(SET CMP0141 NEW)
endif()

project("2fish" LANGUAGES C CXX)

if(MSVC)    
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<CONFIG:Debug,RelWithDebInfo>,EditAndContinue,ProgramDatabase>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost REQUIRED COMPONENTS system filesystem)

set(OPENSSL_USE_STATIC_LIBS ON)
find_package(OpenSSL REQUIRED)

find_package(simdjson REQUIRED)

add_subdirectory(src)

option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
