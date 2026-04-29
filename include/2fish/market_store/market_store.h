#pragma once

#include "2fish/constants.h"
#include "2fish/models/candlestick.h"
#include "2fish/models/orderbook_snapshot.h"
#include "2fish/models/trade.h"
#include "2fish/utils/lru_cache.h"
#include "2fish/utils/ring_buffer.h"
#include "2fish/utils/triple_buffer.h"

#include "moodycamel/readerwriterqueue.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_set>

struct MarketSnapshot {
	int64_t timestamp_{};
	Candlestick candlestick_{};
	OrderbookSnapshot orderbook_snapshot_{};
};

struct QueryResult {
	std::array<MarketSnapshot, constants::HISTORY_STEPS> snapshots_{};
	bool all_data_loaded_{ true };
};

class MarketStore {
public:
	MarketStore(moodycamel::ReaderWriterQueue<market::Trade>& trade_queue,
		TripleBuffer<OrderbookSnapshot>& orderbook_snapshot_buffer, std::atomic<bool>& running);

	void start();

	// called by the renderer
	void query(int64_t end_timestamp, QueryResult& out_result); // ms

	int64_t getLatestTimestamp() const;

private:
	void run();

	// not sure if you're needed
	// bool is_in_live_bounds(int64_t timestamp_ms) const;

	bool fetchFromLiveBuffer(int64_t timestamp_ms, MarketSnapshot& out_snapshot) const;

	void trigger_historical_fetch(int64_t missing_timestamp_ms);

	moodycamel::ReaderWriterQueue<market::Trade>& trade_queue_;
	TripleBuffer<OrderbookSnapshot>& orderbook_snapshot_buffer_;

	RingBuffer<MarketSnapshot, constants::HISTORY_STEPS * 2> live_market_buffer_{};
	LRUCache<int64_t, MarketSnapshot, constants::HISTORY_STEPS * 8> historical_market_buffer_;

	std::atomic<bool>& running_;

	int64_t latest_level_timestamp_{};

	// keep track of what timestamps we have already fetched, to avoid excessively spamming
	// the api endpoint
	std::unordered_set<int64_t> active_fetches_;
	std::mutex fetches_mutex_;

	std::jthread thread_;
};
