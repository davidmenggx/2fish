#include "2fish/engine/engine.h"

#include <chrono>
#include <string>
#include <thread>
#include <utility>

market::Engine::Engine(std::string target_asset_id_raw)
	: target_asset_id_raw_{ std::move(target_asset_id_raw) }
	, client_{ market_queue_, buffer_pool_, running_, target_asset_id_raw_ }
	, dispatcher_{ market_queue_, buffer_pool_, running_, target_asset_id_raw_ }
{
}

void market::Engine::start() {
	running_.store(true);

	client_.start();

	dispatcher_.start();

	using namespace std::chrono_literals;
	std::this_thread::sleep_for(100s);

	running_.store(false);
}
