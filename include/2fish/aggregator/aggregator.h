#pragma once

#include "2fish/constants.h"
#include "2fish/models/candlestick.h"
#include "2fish/models/orderbook_snapshot.h"
#include "2fish/models/trade.h"
#include "2fish/utils/seq_lock_ring_buffer.h"
#include "2fish/utils/timeseries_cache.h"
#include "2fish/utils/triple_buffer.h"

#include <moodycamel/readerwriterqueue.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

class Aggregator {
public:
	Aggregator(moodycamel::ReaderWriterQueue<market::Trade>& trade_queue, 
		TripleBuffer<OrderbookSnapshot>& orderbook_snapshot_buffer, 
		std::atomic<bool>& running);

	void start();

	void extractCandles(std::vector<Candlestick>& candlesticks, double end_time);
	void extractOrderbook(std::vector<OrderbookSnapshot>& orderbook_snapshots, double end_time);

	int64_t getLocalReceiptTime() const { return local_receipt_time_.load(std::memory_order_acquire); }
	int64_t getLatestExchangeTimestamp() const { return latest_exchange_timestamp_.load(std::memory_order_acquire); }

private:
	void run();

	// External data feeds that are being aggregated
	moodycamel::ReaderWriterQueue<market::Trade>& trade_queue_;
	TripleBuffer<OrderbookSnapshot>& orderbook_snapshot_buffer_;

	// Intneral representations of the historical state, based on the
	// external data feeds. The data is split into two sections: a live
	// ring buffer feed for the past HISTORY_STEPS intervals, and an level 2
	// LRU cache if the user decides to query old results
	std::unique_ptr<SeqLockRingBuffer<OrderbookSnapshot, constants::HISTORY_STEPS * 2
		* constants::ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK>> orderbook_snapshot_live_;
	std::unique_ptr<SeqLockRingBuffer<Candlestick, constants::HISTORY_STEPS * 2>> candlestick_live_;

	std::unique_ptr<TimeseriesCache<OrderbookSnapshot, constants::HISTORY_STEPS * 4 * constants::ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK,
		constants::HISTORICAL_ORDERBOOK_GRANULARITY>> orderbook_snapshot_history_;
	std::unique_ptr<TimeseriesCache<Candlestick, constants::HISTORY_STEPS * 4,
		constants::HISTORICAL_CANDLESTICK_GRANULARITY>> candlestick_history_;
	
	std::atomic<int64_t> latest_exchange_timestamp_{};
	std::atomic<int64_t> local_receipt_time_{};

	std::vector<Candlestick> fetch_candlestick_buffer_{};
	std::vector<OrderbookSnapshot> fetch_orderbook_snapshot_buffer_{};

	std::atomic<bool>& running_;
	std::jthread thread_;
};
