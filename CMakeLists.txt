cmake_minimum_required(VERSION 3.21)

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

project("2fish" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Debug information
if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<CONFIG:Debug,RelWithDebInfo>,EditAndContinue,ProgramDatabase>")
endif()

include(FetchContent)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    uWebSockets
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG v20.77.0
    GIT_SHALLOW ON
    GIT_PROGRESS ON
)

FetchContent_Declare(
    zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.2
    GIT_SHALLOW ON
    GIT_PROGRESS ON
)

FetchContent_Declare(
    libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG v1.49.2
    GIT_SHALLOW ON
    GIT_PROGRESS ON
)

FetchContent_MakeAvailable(uWebSockets zlib libuv)

if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic) 
endif()

find_package(Threads REQUIRED)

set(USOCKETS_EVENTING
    "${uwebsockets_SOURCE_DIR}/uSockets/src/eventing/libuv.c"
)

file(GLOB USOCKETS_SOURCES
    "${uwebsockets_SOURCE_DIR}/uSockets/src/*.c"
)

add_library(uSockets STATIC ${USOCKETS_SOURCES} ${USOCKETS_EVENTING})

target_compile_definitions(uSockets PUBLIC
    LIBUS_NO_SSL
    LIBUS_USE_LIBUV
)

target_include_directories(uSockets PUBLIC
    "${uwebsockets_SOURCE_DIR}/src"
    "${uwebsockets_SOURCE_DIR}/uSockets/src"
    "${libuv_SOURCE_DIR}/include"
)

target_link_libraries(uSockets PUBLIC
    Threads::Threads
    ZLIB::ZLIB
    uv_a
)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/*.h")

add_executable(2fish ${SOURCES} ${HEADERS})

target_include_directories(2fish PRIVATE include)

target_link_libraries(2fish PRIVATE uSockets)

if(WIN32 AND MSVC)
    target_link_options(2fish PRIVATE $<$<CONFIG:RelWithDebInfo>:/PROFILE>)
endif()
