#include "2fish/engine/engine.h"

market::Engine::Engine() 
	: client_{ buffer_pool_, market_queue_, running_ }
	, books_{ market_queue_, running_ }
{
}

void market::Engine::start() {
	running_.store(true);

	client_.start();

	// TODO: start the book too!
}
