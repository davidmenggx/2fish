add_library(3fishCore STATIC
  websocket/websocket_client.cpp
)

target_link_libraries(3fishCore PUBLIC
  OpenSSL::SSL
  OpenSSL::Crypto
  Boost::system
  simdjson::simdjson
)

add_executable(3fish main.cpp)
target_link_libraries(3fish PRIVATE 3fishCore)
