add_library(2fishCore STATIC
  common/containers/object_pool.hpp
  common/containers/ring_buffer.hpp
  common/containers/seqlock_wrapper.hpp
  common/containers/swmr_map.hpp
  common/core/rest_data_types.hpp
  common/core/types.hpp
  common/core/websocket_data_types.hpp
  common/utils/compute_time_bucket.hpp
  common/utils/cpu_relax.hpp
  common/utils/parse_json_double.hpp
  common/utils/price_round.hpp
  common/utils/set_query_params.hpp
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
  renderer/application.hpp
  renderer/application.cpp
  renderer/component/component.hpp
  renderer/component/component_manager.hpp
  renderer/component/component_manager.cpp
  renderer/component/chart_view.hpp
  renderer/component/chart_view.cpp
  renderer/component/orderbook_view.hpp
  renderer/component/orderbook_view.cpp
)

target_include_directories(2fishCore PUBLIC 
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../external
)

target_link_libraries(2fishCore PUBLIC
  OpenSSL::SSL
  OpenSSL::Crypto
  Boost::system
  Boost::filesystem
  simdjson::simdjson
  Vulkan::Vulkan
  SDL3::SDL3-static
  imgui::imgui
  implot::implot
  vk-bootstrap::vk-bootstrap
)

if(WIN32)
  target_link_libraries(2fishCore PUBLIC ws2_32 mswsock bcrypt)
  target_compile_definitions(2fishCore PUBLIC -D_WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(2fish main.cpp)
target_link_libraries(2fish PRIVATE 2fishCore)
