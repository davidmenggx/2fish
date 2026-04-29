#include "2fish/driver/driver.h"

#include <string>
#include <utility>

Driver::Driver(std::string target_asset_id)
	: client_{ network_queue_, network_buffer_pool_, running_, target_asset_id }
	, parser_{ network_queue_, engine_queue_, trade_queue_, network_buffer_pool_, parser_buffer_pool_, running_, target_asset_id}
	, engine_{ engine_queue_, parser_buffer_pool_, orderbook_snapshot_buffer_, running_, target_asset_id }
	, market_store_{ trade_queue_, orderbook_snapshot_buffer_, running_ }
	, renderer_{ market_store_, "2fish", 1920, 1080, running_ } // TODO: magic numbers!!
{
}

void Driver::start() {
	running_.store(true);

	engine_.start();

	parser_.start();

	client_.start();

	market_store_.start();

	// SDL3 generally needs to run on the main thread
	renderer_.run();
}
