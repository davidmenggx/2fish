#include "common/websocket_data_types.hpp"
#include "config.h"
#include "engine.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <atomic>
#include <thread>

Engine::Engine(moodycamel::ReaderWriterQueue<WebsocketMessage> &websocket_queue,
               Config config, std::atomic<bool> &running)
    : websocket_queue_{websocket_queue}
    , running_{running} {}

void Engine::start() { thread_ = std::jthread(&Engine::run, this); }

void Engine::run() {

}
