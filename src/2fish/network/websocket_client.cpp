#include "2fish/network/network_buffer_pool.h"
#include "2fish/network/websocket_client.h"

market::WebsocketClient::WebsocketClient(NetworkBufferPool& buffer_pool, 
	moodycamel::ReaderWriterQueue<MessageBuffer>& market_queue)
	: buffer_pool_{ buffer_pool }, market_queue_{ market_queue }
{
}
