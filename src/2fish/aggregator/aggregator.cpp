#include "2fish/aggregator/aggregator.h"
#include "2fish/constants.h"
#include "2fish/models/candlestick.h"
#include "2fish/models/orderbook_snapshot.h"
#include "2fish/models/trade.h"
#include "2fish/utils/lru_cache.h"
#include "2fish/utils/ring_buffer.h"
#include "2fish/utils/triple_buffer.h"

#include <moodycamel/readerwriterqueue.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <intrin0.inl.h>
#include <memory>
#include <thread>

// TODO: these std make uniques are way too long
Aggregator::Aggregator(moodycamel::ReaderWriterQueue<market::Trade>& trade_queue,
	TripleBuffer<OrderbookSnapshot>& orderbook_snapshot_buffer,
	std::atomic<bool>& running)
	: trade_queue_{ trade_queue }, orderbook_snapshot_buffer_{ orderbook_snapshot_buffer }
	, running_{ running }
	, orderbook_snapshot_live_{ std::make_unique<RingBuffer<OrderbookSnapshot, constants::HISTORY_STEPS * 2 * constants::ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK>>() }
	, candlestick_live_{ std::make_unique<RingBuffer<Candlestick, constants::HISTORY_STEPS * 2>>() }
	, orderbook_snapshot_history_{ std::make_unique<LRUCache<int64_t, OrderbookSnapshot, constants::HISTORY_STEPS * 8>>() }
	, candlestick_history_{ std::make_unique<LRUCache<int64_t, Candlestick, constants::HISTORY_STEPS * 8>>() }
{
}

void Aggregator::start() {
	thread_ = std::jthread(&Aggregator::run, this);
}

static int64_t getLocalTimeMs() {
	auto now = std::chrono::system_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

void Aggregator::run() {
	market::Trade latest_trade{};
	Candlestick latest_candlestick{};

	int64_t current_ob_bucket_start{ 0 };
	int64_t current_candle_bucket_start{ 0 };

	// since the orderbook updates unconditionally, it is likely that
	// the snapshot is read before any change has happened. but we don't
	// want to naively write this, as this causes contention with the seqlock
	// in the ring buffer. so keep track of the last update to only change
	// when needed
	int64_t last_processed_ob_ts{ 0 };

	while (running_.load(std::memory_order_relaxed)) {
		bool processed_work{ false };

		const OrderbookSnapshot* latest_orderbook_snapshot{ orderbook_snapshot_buffer_.getReaderBuffer() };

		if (latest_orderbook_snapshot != nullptr) {
			int64_t current_ts{ latest_orderbook_snapshot->timestamp_ };

			if (current_ts > last_processed_ob_ts) {
				last_processed_ob_ts = current_ts;

				if (current_ts > latest_exchange_timestamp_.load(std::memory_order_relaxed)) {
					latest_exchange_timestamp_.store(current_ts, std::memory_order_release);
					local_receipt_time_.store(getLocalTimeMs(), std::memory_order_release);
				}

				// compute which bucket the orderbook and candlestick belong to
				int64_t ob_bucket_start{ static_cast<int64_t>((current_ts / constants::ORDERBOOK_INTERVAL) * constants::ORDERBOOK_INTERVAL) };
				int64_t candle_bucket_start{ static_cast<int64_t>((current_ts / constants::CANDLESTICK_INTERVAL) * constants::CANDLESTICK_INTERVAL) };

				if (current_ob_bucket_start == 0) {
					current_ob_bucket_start = ob_bucket_start;
					orderbook_snapshot_live_->push(*latest_orderbook_snapshot);
				}
				else if (ob_bucket_start > current_ob_bucket_start) {
					// if there is a gap in time of silence, fill in the gap appropriately
					while (current_ob_bucket_start < ob_bucket_start) {
						current_ob_bucket_start += constants::ORDERBOOK_INTERVAL;
						orderbook_snapshot_live_->push(*latest_orderbook_snapshot);
					}
				}
				else if (ob_bucket_start == current_ob_bucket_start) {
					orderbook_snapshot_live_->update_back(*latest_orderbook_snapshot);
				}

				processed_work = true;

				// even if no trade happens, the orderbook will continue to tick on
				if (current_candle_bucket_start != 0 && candle_bucket_start > current_candle_bucket_start) {
					while (current_candle_bucket_start < candle_bucket_start) {
						current_candle_bucket_start += constants::CANDLESTICK_INTERVAL;

						latest_candlestick.start_timestamp_ = current_candle_bucket_start;
						latest_candlestick.open_ = latest_candlestick.close_;
						latest_candlestick.high_ = latest_candlestick.close_;
						latest_candlestick.low_ = latest_candlestick.close_;
						latest_candlestick.volume_ = 0;

						candlestick_live_->push(latest_candlestick);
					}
				}
			}
		}

		while (trade_queue_.try_dequeue(latest_trade)) {
			processed_work = true;
			int64_t trade_ts{ latest_trade.timestamp_ };
			int64_t candle_bucket_start{ static_cast<int64_t>((trade_ts / constants::CANDLESTICK_INTERVAL) * constants::CANDLESTICK_INTERVAL) };

			if (current_candle_bucket_start == 0) {
				// at initialization (first ever snapshot), just push directly
				current_candle_bucket_start = candle_bucket_start;
				latest_candlestick.start_timestamp_ = candle_bucket_start;
				latest_candlestick.open_ = latest_trade.price_;
				latest_candlestick.high_ = latest_trade.price_;
				latest_candlestick.low_ = latest_trade.price_;
				latest_candlestick.close_ = latest_trade.price_;
				latest_candlestick.volume_ = latest_trade.size_;
				candlestick_live_->push(latest_candlestick);
			}
			else if (candle_bucket_start > current_candle_bucket_start) {
				// if there is a gap in time of silence, fill in the gap appropriately
				while (current_candle_bucket_start < candle_bucket_start) {
					current_candle_bucket_start += constants::CANDLESTICK_INTERVAL;
					latest_candlestick.start_timestamp_ = current_candle_bucket_start;
					latest_candlestick.open_ = latest_candlestick.close_;
					latest_candlestick.high_ = latest_candlestick.close_;
					latest_candlestick.low_ = latest_candlestick.close_;
					latest_candlestick.volume_ = 0;
					candlestick_live_->push(latest_candlestick);
				}

				latest_candlestick.start_timestamp_ = candle_bucket_start;
				latest_candlestick.open_ = latest_trade.price_;
				latest_candlestick.high_ = latest_trade.price_;
				latest_candlestick.low_ = latest_trade.price_;
				latest_candlestick.close_ = latest_trade.price_;
				latest_candlestick.volume_ = latest_trade.size_;
				candlestick_live_->push(latest_candlestick);
			}
			else if (candle_bucket_start == current_candle_bucket_start) {
				latest_candlestick.high_ = std::max(latest_candlestick.high_, latest_trade.price_);
				latest_candlestick.low_ = std::min(latest_candlestick.low_, latest_trade.price_);
				latest_candlestick.close_ = latest_trade.price_;
				latest_candlestick.volume_ += latest_trade.size_;
				candlestick_live_->update_back(latest_candlestick);
			}
		}

		if (!processed_work) {
			// spin wait
			#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
				_mm_pause();
			#elif defined(__aarch64__) || defined(_M_ARM64)
				__asm__ volatile("yield" ::: "memory");
			#endif
		}
	}
}
