#include "2fish/engine/engine.h"

#include <chrono>
#include <thread>

market::Engine::Engine() 
	: client_{ market_queue_, buffer_pool_, running_ }
	, books_{ market_queue_, buffer_pool_, running_ }
{
}

void market::Engine::start() {
	running_.store(true);

	client_.start();

	books_.start();

	using namespace std::chrono_literals;
	std::this_thread::sleep_for(100s);

	running_.store(false);
}
