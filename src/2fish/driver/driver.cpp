#include "2fish/driver/driver.h"

#include <string>
#include <utility>

Driver::Driver(std::string target_asset_id_raw)
	: target_asset_id_raw_{ std::move(target_asset_id_raw) }
	, client_{ market_queue_, buffer_pool_, running_, target_asset_id_raw_ }
	, engine_{ market_queue_, buffer_pool_, market_snapshot_buffer_, running_, target_asset_id_raw_ }
	, renderer_{ market_snapshot_buffer_, "2fish", 800, 800, running_ } // TODO: magic numbers!!
{
}

void Driver::start() {
	running_.store(true);

	engine_.start();

	client_.start();

	// SDL3 generally needs to run on the main thread
	renderer_.run();
}
