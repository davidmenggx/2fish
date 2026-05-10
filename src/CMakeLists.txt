add_library(3fishCore STATIC
  common/core/types.hpp
  common/core/websocket_data_types.hpp
  config.hpp
  driver/driver.hpp
  driver/driver.cpp
  engine/store/candlestick_store.hpp
  engine/store/candlestick_store.cpp
  engine/store/orderbook_store.hpp
  engine/store/orderbook_store.cpp
  engine/engine.hpp
  engine/engine.cpp
  network/auth/websocket_headers.hpp
  network/auth/websocket_headers.cpp
  network/websocket/websocket_client.hpp
  network/websocket/websocket_client.cpp
  network/websocket/websocket_parser.hpp
  network/websocket/websocket_parser.cpp
)

target_include_directories(3fishCore PUBLIC 
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../external
)

target_link_libraries(3fishCore PUBLIC
  OpenSSL::SSL
  OpenSSL::Crypto
  Boost::system
  Boost::filesystem
  simdjson::simdjson
)

if(WIN32)
  target_link_libraries(3fishCore PUBLIC ws2_32 mswsock bcrypt)
  target_compile_definitions(3fishCore PUBLIC -D_WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(3fish main.cpp)
target_link_libraries(3fish PRIVATE 3fishCore)
