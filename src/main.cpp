#include "network/websocket/websocket_client.hpp"

#include <atomic>

int main() {
  std::atomic<bool> running{ true };
	
  WebsocketClient client{running};

  client.start();

  return 0;
}
