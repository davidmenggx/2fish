#pragma once

#include "2fish/constants.h"
#include "2fish/models/candlestick.h"
#include "2fish/models/orderbook_snapshot.h"
#include "2fish/models/trade.h"
#include "2fish/renderer/renderer_slice.h"
#include "2fish/utils/seq_lock_ring_buffer.h"
#include "2fish/utils/timeseries_cache.h"
#include "2fish/utils/triple_buffer.h"

#include <moodycamel/concurrentqueue.h>
#include <moodycamel/readerwriterqueue.h>

#include <boost/asio/thread_pool.hpp>
#include <cpp-httplib/httplib.h>

#include <simdjson.h>

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
	~Aggregator();

	void start();

	void extractCandles(std::vector<RendererSlice<Candlestick>>& candlesticks, double end_time_ms, bool is_live, int64_t current_time_ms);
	void extractOrderbook(std::vector< RendererSlice<OrderbookSnapshot>>& orderbook_snapshots, double end_time_ms, bool is_live, int64_t current_time_ms);

	int64_t getLocalReceiptTime() const { return local_receipt_time_.load(std::memory_order_acquire); }
	int64_t getLatestExchangeTimestamp() const { return latest_exchange_timestamp_.load(std::memory_order_acquire); }

private:
	void run();

	void tryFetchHistoricalCandlestick(int64_t target_time, int64_t current_time_ms, int64_t end_time_s);
	std::optional<Candlestick> constructHistoricalCandlestick(std::string_view raw_message, int64_t end_time_s);

	void tryFetchHistoricalOrderbook(int64_t target_time, int64_t current_time);

	// External data feeds for live streaming from the engine.
	moodycamel::ReaderWriterQueue<market::Trade>& trade_queue_;
	TripleBuffer<OrderbookSnapshot>& orderbook_snapshot_buffer_;

	// Internal data feeds for historical fetch results.
	moodycamel::ConcurrentQueue<Candlestick> historical_candlestick_queue_{};
	moodycamel::ConcurrentQueue<OrderbookSnapshot> historical_orderbook_snapshot_queue_{};

	// Intneral representations of the historical state, based on the
	// external data feeds. The data is split into two sections: a live
	// ring buffer feed for the past HISTORY_STEPS intervals, and an level 2
	// timeseries cache if the user decides to query old results.
	std::unique_ptr<SeqLockRingBuffer<OrderbookSnapshot, constants::HISTORY_STEPS * 2
		* constants::ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK>> orderbook_snapshot_live_;
	std::unique_ptr<SeqLockRingBuffer<Candlestick, constants::HISTORY_STEPS * 2>> candlestick_live_;

	std::unique_ptr<TimeseriesCache<OrderbookSnapshot, constants::HISTORY_STEPS * 4 * constants::ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK,
		constants::HISTORICAL_ORDERBOOK_GRANULARITY>> orderbook_snapshot_history_;
	std::unique_ptr<TimeseriesCache<Candlestick, constants::HISTORY_STEPS * 4,
		constants::HISTORICAL_CANDLESTICK_GRANULARITY>> candlestick_history_;
	
	std::atomic<int64_t> latest_exchange_timestamp_{};
	std::atomic<int64_t> local_receipt_time_{};

	// Allocate these vectors just once and re-use them for future data fetches.
	std::vector<Candlestick> fetch_candlestick_buffer_{};
	std::vector<OrderbookSnapshot> fetch_orderbook_snapshot_buffer_{};

	boost::asio::thread_pool thread_pool_;

	// Map the requested time (historical) to the time the request was made (live)
	// to avoid spamming the API.
	std::unique_ptr<TimeseriesCache<int64_t, constants::HISTORY_STEPS * 64 * constants::ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK,
		constants::HISTORICAL_ORDERBOOK_GRANULARITY>> inflight_orderbook_requests_;
	std::unique_ptr<TimeseriesCache<int64_t, constants::HISTORY_STEPS * 64,
		constants::HISTORICAL_CANDLESTICK_GRANULARITY>> inflight_candlestick_requests_;

	std::atomic<bool>& running_;
	std::jthread thread_;
};
