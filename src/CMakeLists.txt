set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/2fish/main.cpp")

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/include/*.h")

add_library(2fish_core STATIC ${SOURCES} ${HEADERS})

target_include_directories(2fish_core PUBLIC "${PROJECT_SOURCE_DIR}/include")

target_link_libraries(2fish_core PUBLIC
    Boost::system 
    Boost::filesystem
    Threads::Threads
    OpenSSL::SSL
    simdjson::simdjson
)

if(WIN32)
    target_link_libraries(2fish_core PUBLIC ws2_32 mswsock bcrypt)
    target_compile_definitions(2fish_core PUBLIC -D_WIN32_WINNT=0x0A00)
endif()

add_executable(2fish 2fish/main.cpp) 
target_link_libraries(2fish PRIVATE 2fish_core)

target_include_directories(2fish PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
