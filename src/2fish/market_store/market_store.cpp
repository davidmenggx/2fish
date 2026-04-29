#include "2fish/constants.h"
#include "2fish/market_store/market_store.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <thread>

MarketStore::MarketStore(moodycamel::ReaderWriterQueue<market::Trade>& trade_queue,
	TripleBuffer<OrderbookSnapshot>& orderbook_snapshot_buffer, std::atomic<bool>& running) 
	: trade_queue_{ trade_queue }, orderbook_snapshot_buffer_{ orderbook_snapshot_buffer }
	, running_{ running }
{
}

void MarketStore::start() {
	thread_ = std::jthread(&MarketStore::run, this);
}

void MarketStore::run() {
	try {
		// TO ASK: is there a better way to do this without explicitly asking for time?
		auto get_current_ms = []() {
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()
			).count();
			};

		int64_t current_bucket_ms{ (get_current_ms() / constants::TIME_PER_HISTORY_LEVEL) * constants::TIME_PER_HISTORY_LEVEL };
		MarketSnapshot active_snapshot{ current_bucket_ms, {}, {} };

		while (running_) {
			bool did_work{ false };

			int64_t now_ms{ get_current_ms() };
            int64_t now_bucket_ms = (now_ms / constants::TIME_PER_HISTORY_LEVEL) * constants::TIME_PER_HISTORY_LEVEL;

            while (now_bucket_ms > current_bucket_ms) {
                // this means the time has advanced to the next slice, so push the data
				// to the buffer
                live_market_buffer_.push(active_snapshot);
                std::cout << "Pushing a buffer, now the market buffer has " << live_market_buffer_.size() << " elements\n";

                current_bucket_ms += constants::TIME_PER_HISTORY_LEVEL;

                active_snapshot.timestamp_ = current_bucket_ms;

                active_snapshot.candlestick_.reset();

                did_work = true;
            }
            const OrderbookSnapshot* orderbook_snapshot{ orderbook_snapshot_buffer_.getReaderBuffer() };
            if (orderbook_snapshot) {
                active_snapshot.orderbook_snapshot_ = *orderbook_snapshot;
                latest_level_timestamp_ = orderbook_snapshot->timestamp_;
                did_work = true;
            }

            market::Trade trade;
            while (trade_queue_.try_dequeue(trade)) {
                std::cout << "Writing a trade\n";
                int64_t trade_bucket_ms = (trade.timestamp_ / constants::TIME_PER_HISTORY_LEVEL) * constants::TIME_PER_HISTORY_LEVEL;

                while (trade_bucket_ms > current_bucket_ms) {
                    live_market_buffer_.push(active_snapshot);
                    current_bucket_ms += constants::TIME_PER_HISTORY_LEVEL;
                    active_snapshot.timestamp_ = current_bucket_ms;
                    
                    active_snapshot.candlestick_.reset();
                }

                 active_snapshot.candlestick_.apply_trade(trade);

                did_work = true;
            }

            // avoid excessive cpu utilization
            if (!did_work) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
		}
	}
	catch (const std::exception& e) {
		std::cerr << std::format("CRITICAL: Unexpected error in market store: {}\n", e.what());
		throw;
	}
}

void MarketStore::query(int64_t end_timestamp_ms, QueryResult& out_result) {
    std::cout << "Got a query\n";
    out_result.all_data_loaded_ = true;

    int64_t end_bucket_ms{ (end_timestamp_ms / constants::TIME_PER_HISTORY_LEVEL) * constants::TIME_PER_HISTORY_LEVEL };

    for (std::size_t i{ 0 }; i < constants::HISTORY_STEPS; ++i) {
        int64_t current_ts = end_bucket_ms - (i * constants::TIME_PER_HISTORY_LEVEL);

        std::size_t out_idx{ constants::HISTORY_STEPS - 1 - i };

        if (fetchFromLiveBuffer(current_ts, out_result.snapshots_[out_idx])) {
            continue;
        }

        if (historical_market_buffer_.fetch(current_ts, out_result.snapshots_[out_idx])) {
            continue;
        }

        // lru cache miss = show that the data is not loaded yet to the renderer
        out_result.all_data_loaded_ = false;

        out_result.snapshots_[out_idx] = MarketSnapshot{};
        out_result.snapshots_[out_idx].timestamp_ = current_ts;

        trigger_historical_fetch(current_ts);
    }
}

bool MarketStore::fetchFromLiveBuffer(int64_t target_ts_ms, MarketSnapshot& out_snapshot) const {
    if (live_market_buffer_.empty()) {
        return false;
    }

    int64_t bucket_end_ms = target_ts_ms + constants::TIME_PER_HISTORY_LEVEL;

    for (int i = static_cast<int>(live_market_buffer_.size()) - 1; i >= 0; --i) {
        const auto& snap = live_market_buffer_[i];

        if (snap.timestamp_ >= bucket_end_ms) {
            continue;
        }

        out_snapshot = snap;

        if (snap.timestamp_ < target_ts_ms) {
            out_snapshot.timestamp_ = target_ts_ms;
            out_snapshot.candlestick_.open_ = snap.candlestick_.close_;
            out_snapshot.candlestick_.high_ = snap.candlestick_.close_;
            out_snapshot.candlestick_.low_ = snap.candlestick_.close_;

            out_snapshot.candlestick_.volume_ = 0;
        }

        return true;
    }
    return false;
}

void MarketStore::trigger_historical_fetch(int64_t missing_timestamp_ms) {
    //std::cout << "Historical fetch triggered\n";
}

int64_t MarketStore::getLatestTimestamp() const {
    if (live_market_buffer_.empty()) {
        return 0; // Or return system clock as a fallback if no data exists yet
    }
    std::size_t newest_index{ live_market_buffer_.size() - 1 };
    return live_market_buffer_[newest_index].timestamp_;
}
