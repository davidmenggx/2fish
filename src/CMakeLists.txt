file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "../include/*.h")

add_library(2fish_core STATIC ${SOURCES} ${HEADERS})
target_include_directories(2fish_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(2fish_core PUBLIC
    Boost::boost
    Boost::system 
    Boost::filesystem
    Threads::Threads
    OpenSSL::SSL
)

if(WIN32)    
    target_link_libraries(2fish_core PUBLIC ws2_32 mswsock bcrypt)    
    target_compile_definitions(2fish_core PUBLIC -D_WIN32_WINNT=0x0A00)    
endif()

add_executable(2fish 2fish/main.cpp)

target_link_libraries(2fish PRIVATE 2fish_core)

if(WIN32 AND MSVC)
    target_link_options(2fish PRIVATE $<$<CONFIG:RelWithDebInfo>:/PROFILE>)
endif()
