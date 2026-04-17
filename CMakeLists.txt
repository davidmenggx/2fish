cmake_minimum_required(VERSION 3.21)

cmake_policy(SET CMP0091 NEW) 
if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

project("2fish" LANGUAGES C CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<CONFIG:Debug,RelWithDebInfo>,EditAndContinue,ProgramDatabase>")
endif()

find_package(Threads REQUIRED)

find_package(Boost REQUIRED COMPONENTS system filesystem) 
find_package(OpenSSL REQUIRED) 

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "include/*.h")

add_executable(2fish ${SOURCES} ${HEADERS})

target_include_directories(2fish PRIVATE include)

target_link_libraries(2fish PRIVATE
    Boost::boost
    Boost::system 
    Boost::filesystem
    Threads::Threads
    OpenSSL::SSL
)

if(WIN32)
    target_link_libraries(2fish PRIVATE ws2_32 mswsock bcrypt)
    add_definitions(-D_WIN32_WINNT=0x0A00)

    if (MSVC)
        target_link_options(2fish PRIVATE $<$<CONFIG:RelWithDebInfo>:/PROFILE>)

    endif()

endif()
