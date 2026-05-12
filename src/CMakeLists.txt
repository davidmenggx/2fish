add_library(3fishCore STATIC
  common/containers/ring_buffer.hpp
  common/containers/seqlock_wrapper.hpp
  common/containers/swmr_map.hpp
  common/core/rest_data_types.hpp
  common/core/types.hpp
  common/core/websocket_data_types.hpp
  common/utils/compute_time_bucket.hpp
  common/utils/cpu_relax.hpp
  config.hpp
  constants.hpp
  driver/driver.hpp
  driver/driver.cpp
  engine/store/candlestick_store.hpp
  engine/store/candlestick_store.cpp
  engine/store/orderbook_store.hpp
  engine/store/orderbook_store.cpp
  engine/engine.hpp
  engine/engine.cpp
  network/auth/generate_headers.hpp
  network/auth/generate_headers.cpp
  network/rest/https_session.hpp
  network/rest/https_session.cpp
  network/rest/rest_client.hpp
  network/rest/rest_client.cpp
  network/rest/rest_parser.hpp
  network/rest/rest_parser.cpp
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
