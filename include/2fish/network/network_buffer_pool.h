#pragma once

#include <simdjson.h>

#include <array>
#include <cstdint>
#include <memory>
#include <stack>

// TODO: move these to some constants place
// TODO: 8192 came from some very minimal test readings, perhaps
// find a more robust way to profile the size
constexpr std::size_t kMaxMessageSize{ 16384 };
constexpr std::size_t kBufferCount{ 2048 }; // TODO: perhaps not constexpr so i can set in ctor
constexpr size_t kBufferSize{ kMaxMessageSize + simdjson::SIMDJSON_PADDING };

namespace market {
	struct MessageBuffer {
		char data_[kBufferSize]{};
		uint32_t message_size_{}; // bytes
	};

	class NetworkBufferPool {
	public:
		NetworkBufferPool();

		MessageBuffer* acquire();

		void release(MessageBuffer* buffer);

	private:
		std::unique_ptr<std::array<MessageBuffer, kBufferCount>> buffers_;
		
		// TODO: profile average buffer usage
		std::stack<std::size_t> free_buffers_{};
	};
}
