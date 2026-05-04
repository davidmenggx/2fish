#include "2fish/aggregator/aggregator.h"
#include "2fish/constants.h"
#include "2fish/models/candlestick.h"
#include "2fish/models/orderbook_snapshot.h"
#include "2fish/models/trade.h"
#include "2fish/renderer/renderer_slice.h"
#include "2fish/utils/seq_lock_ring_buffer.h"
#include "2fish/utils/timeseries_cache.h"
#include "2fish/utils/triple_buffer.h"

#include <moodycamel/readerwriterqueue.h>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <cpp-httplib/httplib.h>

#include <simdjson.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <intrin0.inl.h>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <iostream>

// TODO: these std make uniques are way too long
Aggregator::Aggregator(moodycamel::ReaderWriterQueue<market::Trade>& trade_queue,
	TripleBuffer<OrderbookSnapshot>& orderbook_snapshot_buffer,
	std::atomic<bool>& running)
	: trade_queue_{ trade_queue }, orderbook_snapshot_buffer_{ orderbook_snapshot_buffer }
	, running_{ running }
	, orderbook_snapshot_live_{ std::make_unique<SeqLockRingBuffer<OrderbookSnapshot, constants::HISTORY_STEPS * 2 * constants::ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK>>() }
	, candlestick_live_{ std::make_unique<SeqLockRingBuffer<Candlestick, constants::HISTORY_STEPS * 2>>() }
	, orderbook_snapshot_history_{ std::make_unique<TimeseriesCache<OrderbookSnapshot, constants::HISTORY_STEPS * 4 * constants::ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK,
		constants::HISTORICAL_ORDERBOOK_GRANULARITY>>() }
	, candlestick_history_{ std::make_unique<TimeseriesCache<Candlestick, constants::HISTORY_STEPS * 4,
		constants::HISTORICAL_CANDLESTICK_GRANULARITY>>() }
	, inflight_orderbook_requests_{ std::make_unique<TimeseriesCache<int64_t, constants::HISTORY_STEPS * 64 * constants::ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK,
		constants::HISTORICAL_ORDERBOOK_GRANULARITY>>() }
	, inflight_candlestick_requests_{ std::make_unique<TimeseriesCache<int64_t, constants::HISTORY_STEPS * 64,
		constants::HISTORICAL_CANDLESTICK_GRANULARITY>>() }
	, thread_pool_{ 2 * std::thread::hardware_concurrency() }//, cli_{"https://clob.polymarket.com"}
{
	fetch_orderbook_snapshot_buffer_.reserve(constants::HISTORY_STEPS * 2 * constants::ORDERBOOK_SNAPSHOTS_PER_CANDLESTICK);
	fetch_candlestick_buffer_.reserve(constants::HISTORY_STEPS * 2);
}

Aggregator::~Aggregator() {
	thread_pool_.join();
}

void Aggregator::start() {
	thread_ = std::jthread(&Aggregator::run, this);
}

static int64_t getLocalTimeMs() {
	auto now = std::chrono::system_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

void Aggregator::run() {
	// Re-usable objects for the live data feed.
	market::Trade latest_live_trade{};
	Candlestick latest_live_candlestick{};

	int64_t current_ob_bucket_start{ 0 };
	int64_t current_candle_bucket_start{ 0 };

	// Since the orderbook updates unconditionally, it is likely that
	// the snapshot is read before any change has happened. But we don't
	// want to naively write this, as this causes contention with the seqlock
	// in the ring buffer. Keep track of the last update to only change
	// when needed.
	int64_t last_processed_ob_ts{ 0 };

	// Re-usable objects for the historical data feed.
	Candlestick latest_historical_candlestick{};
	OrderbookSnapshot latest_historical_ob_snapshot{};

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

				// Compute which bucket the orderbook and candlestick belong to.
				int64_t ob_bucket_start{ static_cast<int64_t>((current_ts / constants::ORDERBOOK_INTERVAL) * constants::ORDERBOOK_INTERVAL) };
				int64_t candle_bucket_start{ static_cast<int64_t>((current_ts / constants::CANDLESTICK_INTERVAL) * constants::CANDLESTICK_INTERVAL) };

				if (current_ob_bucket_start == 0) {
					current_ob_bucket_start = ob_bucket_start;
					orderbook_snapshot_live_->push(*latest_orderbook_snapshot);
				}
				else if (ob_bucket_start > current_ob_bucket_start) {
					// If there is a gap in time of silence, fill in the gap appropriately.
					while (current_ob_bucket_start < ob_bucket_start) {
						current_ob_bucket_start += constants::ORDERBOOK_INTERVAL;
						orderbook_snapshot_live_->push(*latest_orderbook_snapshot);
					}
				}
				else if (ob_bucket_start == current_ob_bucket_start) {
					orderbook_snapshot_live_->update_back(*latest_orderbook_snapshot);
				}

				processed_work = true;

				// Even if no trade happens, the orderbook will continue to tick on.
				if (current_candle_bucket_start != 0 && candle_bucket_start > current_candle_bucket_start) {
					while (current_candle_bucket_start < candle_bucket_start) {
						current_candle_bucket_start += constants::CANDLESTICK_INTERVAL;

						latest_live_candlestick.start_timestamp_ = current_candle_bucket_start;
						latest_live_candlestick.open_ = latest_live_candlestick.close_;
						latest_live_candlestick.high_ = latest_live_candlestick.close_;
						latest_live_candlestick.low_ = latest_live_candlestick.close_;
						latest_live_candlestick.volume_ = 0;

						candlestick_live_->push(latest_live_candlestick);
					}
				}
			}
		}

		while (trade_queue_.try_dequeue(latest_live_trade)) {
			processed_work = true;
			int64_t trade_ts{ latest_live_trade.timestamp_ };
			int64_t candle_bucket_start{ static_cast<int64_t>((trade_ts / constants::CANDLESTICK_INTERVAL) * constants::CANDLESTICK_INTERVAL) };

			if (current_candle_bucket_start == 0) {
				// At initialization (first ever snapshot), just push directly.
				current_candle_bucket_start = candle_bucket_start;
				latest_live_candlestick.start_timestamp_ = candle_bucket_start;
				latest_live_candlestick.open_ = latest_live_trade.price_;
				latest_live_candlestick.high_ = latest_live_trade.price_;
				latest_live_candlestick.low_ = latest_live_trade.price_;
				latest_live_candlestick.close_ = latest_live_trade.price_;
				latest_live_candlestick.volume_ = latest_live_trade.size_;
				candlestick_live_->push(latest_live_candlestick);
			}
			else if (candle_bucket_start > current_candle_bucket_start) {
				// If there is a gap in time of silence, fill in the gap appropriately.
				while (current_candle_bucket_start < candle_bucket_start) {
					current_candle_bucket_start += constants::CANDLESTICK_INTERVAL;
					latest_live_candlestick.start_timestamp_ = current_candle_bucket_start;
					latest_live_candlestick.open_ = latest_live_candlestick.close_;
					latest_live_candlestick.high_ = latest_live_candlestick.close_;
					latest_live_candlestick.low_ = latest_live_candlestick.close_;
					latest_live_candlestick.volume_ = 0;
					candlestick_live_->push(latest_live_candlestick);
				}

				latest_live_candlestick.start_timestamp_ = candle_bucket_start;
				latest_live_candlestick.open_ = latest_live_trade.price_;
				latest_live_candlestick.high_ = latest_live_trade.price_;
				latest_live_candlestick.low_ = latest_live_trade.price_;
				latest_live_candlestick.close_ = latest_live_trade.price_;
				latest_live_candlestick.volume_ = latest_live_trade.size_;
				candlestick_live_->push(latest_live_candlestick);
			}
			else if (candle_bucket_start == current_candle_bucket_start) {
				latest_live_candlestick.high_ = std::max(latest_live_candlestick.high_, latest_live_trade.price_);
				latest_live_candlestick.low_ = std::min(latest_live_candlestick.low_, latest_live_trade.price_);
				latest_live_candlestick.close_ = latest_live_trade.price_;
				latest_live_candlestick.volume_ += latest_live_trade.size_;
				candlestick_live_->update_back(latest_live_candlestick);
			}
		}
		
		while (historical_candlestick_queue_.try_dequeue(latest_historical_candlestick)) {
			processed_work = true;
			candlestick_history_->put(latest_historical_candlestick.start_timestamp_, latest_historical_candlestick);
		}

		while (historical_orderbook_snapshot_queue_.try_dequeue(latest_historical_ob_snapshot)) {
			processed_work = true;
			orderbook_snapshot_history_->put(latest_historical_ob_snapshot.timestamp_, latest_historical_ob_snapshot);
		}

		if (!processed_work) {
			// Spin wait
			#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
				_mm_pause();
			#elif defined(__aarch64__) || defined(_M_ARM64)
				__asm__ volatile("yield" ::: "memory");
			#endif
		}
	}
}

void Aggregator::extractCandles(std::vector<RendererSlice<Candlestick>>& candlesticks, double end_time_ms, 
	bool is_live, int64_t current_time_ms) {
	candlesticks.clear();

	fetch_candlestick_buffer_.clear();
	candlestick_live_->copy_to(fetch_candlestick_buffer_);

	if (is_live && candlestick_live_->full()) {
		std::transform(
			fetch_candlestick_buffer_.begin(),
			fetch_candlestick_buffer_.end(),
			std::back_inserter(candlesticks),
			[](const auto& buffer_item) -> RendererSlice<Candlestick> {
				return { .data_ = buffer_item, .is_loaded_ = true };
			}
		);
		return;
	}

	double start_time_ms{ end_time_ms - constants::WINDOW_DURATION };
	int64_t start_time_s{ static_cast<int64_t>(start_time_ms / 1000) };

	int64_t end_time_s{ static_cast<int64_t>(end_time_ms / 1000) };

	// For safety, these are both considered as complete cache miss.
	// The second conditional is a miss is because trying to concatenate 
	// the live feed with the historical feed will cause weird bugs 
	// when the live feed rolls.
	if (fetch_candlestick_buffer_.empty() || (!is_live && fetch_candlestick_buffer_.front().start_timestamp_ > start_time_ms)) {
		// Fall back to querying the historical cache
		int64_t query_start_time_s{ static_cast<int64_t>((start_time_s / constants::HISTORICAL_CANDLESTICK_GRANULARITY)
			* constants::HISTORICAL_CANDLESTICK_GRANULARITY) };
		int64_t query_end_time_s{ static_cast<int64_t>((end_time_s / constants::HISTORICAL_CANDLESTICK_GRANULARITY)
			* constants::HISTORICAL_CANDLESTICK_GRANULARITY) };

		for (int64_t target_time_s{ query_start_time_s }; target_time_s <= query_end_time_s; target_time_s += constants::HISTORICAL_CANDLESTICK_GRANULARITY) {
			std::optional<Candlestick> query_result{ candlestick_history_->get(target_time_s) };
			if (query_result) {
				// std::cout << "Query hit\n";

				auto res = RendererSlice{ .data_ = *query_result, .is_loaded_ = true };
				if (res.data_.start_timestamp_ < 30000000000LL) {
					res.data_.start_timestamp_ *= 1000; // Normalize timestamp to milliseconds
				}

				candlesticks.push_back(res);
				// CRITICAL TODO: Handle the edge case where this interval does not have any trade activity
			}
			else {
				// std::cout << "Query miss\n";
				Candlestick placeholder{};
				placeholder.start_timestamp_ = target_time_s * 1000; // Normalize timestamp to milliseconds

				candlesticks.emplace_back(RendererSlice{ .data_ = placeholder, .is_loaded_ = false });
				tryFetchHistoricalCandlestick(target_time_s, current_time_ms, query_end_time_s);
			}
		}

		return;
	}

	int64_t earliest_live_timestamp_ms{ fetch_candlestick_buffer_.front().start_timestamp_ };
	int64_t earliest_live_timestamp_s{ earliest_live_timestamp_ms / 1000 };

	if (is_live && earliest_live_timestamp_ms > start_time_ms) {
		// The user is live but at application startup. We need to concatenate
		// the live feed and historical cache.
		int64_t query_start_time_s{ static_cast<int64_t>((start_time_s / constants::HISTORICAL_CANDLESTICK_GRANULARITY)
			* constants::HISTORICAL_CANDLESTICK_GRANULARITY) };
		int64_t query_end_time_s{ static_cast<int64_t>((earliest_live_timestamp_s / constants::HISTORICAL_CANDLESTICK_GRANULARITY)
			* constants::HISTORICAL_CANDLESTICK_GRANULARITY) };

		for (int64_t target_time_s{ query_start_time_s }; target_time_s <= query_end_time_s; target_time_s += constants::HISTORICAL_CANDLESTICK_GRANULARITY) {
			std::optional<Candlestick> query_result{ candlestick_history_->get(target_time_s) };
			if (query_result) {
				// std::cout << "Query hit\n";

				auto res = RendererSlice{ .data_ = *query_result, .is_loaded_ = true };
				if (res.data_.start_timestamp_ < 30000000000LL) {
					res.data_.start_timestamp_ *= 1000; // Normalize timestamp to milliseconds
				}

				candlesticks.push_back(res);
				// CRITICAL TODO: Handle the edge case where this interval does not have any trade activity
			}
			else {
				// std::cout << "Query miss\n";
				Candlestick placeholder{};
				placeholder.start_timestamp_ = target_time_s * 1000; // Normalize timestamp to milliseconds

				candlesticks.emplace_back(RendererSlice{ .data_ = placeholder, .is_loaded_ = false });
				tryFetchHistoricalCandlestick(target_time_s, current_time_ms, query_end_time_s);
			}
		}

		std::transform(
			fetch_candlestick_buffer_.begin(),
			fetch_candlestick_buffer_.end(),
			std::back_inserter(candlesticks),
			[](const auto& buffer_item) -> RendererSlice<Candlestick> {
				return { .data_ = buffer_item, .is_loaded_ = true };
			}
		);

		return;
	}

	auto it_start = std::lower_bound(
		fetch_candlestick_buffer_.begin(),
		fetch_candlestick_buffer_.end(),
		start_time_ms,
		[](const Candlestick& candle, double time) { return candle.start_timestamp_ < time; }
	);

	auto it_end = std::upper_bound(
		it_start,
		fetch_candlestick_buffer_.end(),
		end_time_ms,
		[](double time, const Candlestick& candle) { return time < candle.start_timestamp_; }
	);

	// Just a sanity check
	if (it_start != it_end) {
		candlesticks.clear();
		std::transform(
			it_start,
			it_end,
			std::back_inserter(candlesticks),
			[](const auto& buffer_item) -> RendererSlice<Candlestick> {
				return { .data_ = buffer_item, .is_loaded_ = true };
			}
		);
		return;
	}

	// Possible final failback
}

void Aggregator::extractOrderbook(std::vector<RendererSlice<OrderbookSnapshot>>& orderbook_snapshots, double end_time_ms, 
	bool is_live, int64_t current_time_ms) {
	fetch_orderbook_snapshot_buffer_.clear();
	orderbook_snapshot_live_->copy_to(fetch_orderbook_snapshot_buffer_);

	if (is_live && candlestick_live_->full()) {
		std::transform(
			fetch_orderbook_snapshot_buffer_.begin(),
			fetch_orderbook_snapshot_buffer_.end(),
			orderbook_snapshots.begin(),
			[](const auto& buffer_item) -> RendererSlice<OrderbookSnapshot> {
				return { .data_ = buffer_item, .is_loaded_ = true };
			}
		);
		return;
	}

	double start_time_ms{ end_time_ms - constants::WINDOW_DURATION };
	int64_t start_time_s{ static_cast<int64_t>(start_time_ms / 1000) };

	if (fetch_candlestick_buffer_.empty() || fetch_candlestick_buffer_.front().start_timestamp_ > start_time_ms) {
		// TODO
		return;
	}

	auto it_start = std::lower_bound(
		fetch_orderbook_snapshot_buffer_.begin(),
		fetch_orderbook_snapshot_buffer_.end(),
		start_time_ms,
		[](const OrderbookSnapshot& snapshot, double time) { return snapshot.timestamp_ < time; }
	);

	auto it_end = std::upper_bound(
		it_start,
		fetch_orderbook_snapshot_buffer_.end(),
		end_time_ms,
		[](double time, const OrderbookSnapshot& snapshot) { return time < snapshot.timestamp_; }
	);

	// the interval we were looking for is recent, so we're good
	if (it_start != it_end) {
		std::transform(
			it_start,
			it_end,
			orderbook_snapshots.begin(),
			[](const auto& buffer_item) -> RendererSlice<OrderbookSnapshot> {
				return { .data_ = buffer_item, .is_loaded_ = true };
			}
		);
		return;
	}

	// FALLBACK TO QUERYING THE HISTORICAL CACHE
}

std::optional<Candlestick> Aggregator::constructHistoricalCandlestick(std::string_view raw_message, int64_t end_time_s) {
	thread_local simdjson::ondemand::parser parser_;

	simdjson::padded_string padded_message(raw_message);
	simdjson::ondemand::document doc;

	if (auto error = parser_.iterate(padded_message).get(doc); error) {
		std::cerr << "Failed to parse JSON: " << error << '\n';
		return std::nullopt;
	}

	simdjson::ondemand::array history;
	if (auto error = doc["history"].get_array().get(history); error) {
		std::cerr << "Failed to find or parse 'history' array: " << error << '\n';
		return std::nullopt;
	}

	Candlestick output{};

	std::cout << raw_message << "\n\n";

	// We need to extract timestamp since it is not guaranteed that Polymarket
	// gives us the market prices in chronological order.
	int64_t earliest_timestamp_ms{ std::numeric_limits<int64_t>::max() };
	int opening_price_cents{};
	int64_t latest_timestamp_ms{ std::numeric_limits<int64_t>::min() };
	int closing_price_cents{};

	bool first_iteration{ true };
	bool trade_found_in_interval{ false };
	int trades_found_in_time{ 0 };

	for (auto item_result : history) {
		simdjson::ondemand::object item;
		if (auto error = item_result.get_object().get(item); error) {
			std::cerr << "Array item is not an object: " << error << '\n';
			return std::nullopt;
		}

		int64_t timestamp_s{ -1 };
		double price{ -1.0 };

		for (auto field : item) {
			std::string_view key = field.unescaped_key();
			if (key == "t") {
				field.value().get_int64().get(timestamp_s);
			}
			else if (key == "p") {
				field.value().get_double().get(price);
			}
		}

		if (timestamp_s == -1 || price < 0.0) {
			std::cerr << "Missing 't' or 'p' fields in object.\n";
			return std::nullopt;
		}

		if (timestamp_s >= end_time_s) {
			continue;
		}

		trade_found_in_interval = true;
		++trades_found_in_time;

		int price_cents{ static_cast<int>(std::round(price * 100.0)) };
		int64_t timestamp_ms{ timestamp_s * 1000 };

		if (first_iteration) {
			output.high_ = price_cents;
			output.low_ = price_cents;
			first_iteration = false;
		}

		if (timestamp_ms < earliest_timestamp_ms) {
			earliest_timestamp_ms = timestamp_ms;
			opening_price_cents = price_cents;
		}

		if (timestamp_ms >= latest_timestamp_ms) {
			latest_timestamp_ms = timestamp_ms;
			closing_price_cents = price_cents;
		}

		output.high_ = std::max(output.high_, price_cents);
		output.low_ = std::min(output.low_, price_cents);
	}

	if (!trade_found_in_interval) {
		return std::nullopt;
	}

	std::cout << "Found " << trades_found_in_time << " valid trades in interval with ending timestamp " 
		<< end_time_s << "\n\n";

	output.start_timestamp_ = earliest_timestamp_ms;
	output.open_ = opening_price_cents;
	output.close_ = closing_price_cents;

	return output;
}

void Aggregator::tryFetchHistoricalCandlestick(int64_t target_time_s, int64_t current_time_ms, int64_t end_time_s) {
	std::optional<int64_t> last_try_fetch_time{ inflight_candlestick_requests_->get(target_time_s) };
	if (last_try_fetch_time && *last_try_fetch_time >= current_time_ms - constants::HTTP_REQUEST_COOLDOWN) {
		return;
	}

	inflight_candlestick_requests_->put(target_time_s, current_time_ms);

	boost::asio::post(thread_pool_, [this, target_time_s, end_time_s]() {
		std::string market_id{ "112199164406805486549439306159838835258340225041002461170314819242438095325546" };

		httplib::Params params = {
			{"market", market_id},
			{"startTs", std::to_string(target_time_s)},
			{"endTs", std::to_string(target_time_s + constants::HISTORICAL_CANDLESTICK_GRANULARITY)},
			{"fidelity", "1"}
		};
		std::string path_with_query{ httplib::append_query_params("/prices-history", params) };

		thread_local httplib::Client cli_{ "https://clob.polymarket.com" };

		auto res = cli_.Get(path_with_query);

		if (res) {
			if (res->status == 200) {
				std::optional<Candlestick> historical_candlestick{ this->constructHistoricalCandlestick(res->body, end_time_s) };
				if (historical_candlestick) {
					historical_candlestick->start_timestamp_ = target_time_s;
					this->historical_candlestick_queue_.enqueue(*historical_candlestick);
				}
			}
			else {
				std::cerr << std::format("HTTP request (candlestick) failed with code {}: {}\n", res->status, res->body);
			}
		}
		else {
			std::cerr << std::format("HTTP request (candlestick) failed, query was {}\n", path_with_query);
		}
	});
}
