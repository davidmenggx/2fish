#pragma once

#include <string_view>

namespace market {
	// https://docs.polymarket.com/market-data/websocket/market-channel
	enum class EventType {
		kBookSnapshot, // really this is event_type "book"
		kPriceChange,

		// THE FOLLOWING ARE NOT IMPLEMENTED YET, THEY WILL BE SILENTLY DROPPED
		// you actually dont need most of these as long as dont enable custom features
		kTickSizeChange,
		kLastTradePrice,
		kBestBidAsk,
		kNewMarket,
		kMarketResolved,

		kUnknown
	};

	[[nodiscard]] inline EventType stringToEventType(std::string_view s) {
		if (s == "book") return EventType::kBookSnapshot;
		if (s == "price_change") return EventType::kPriceChange;
		if (s == "tick_size_change") return EventType::kTickSizeChange;
		if (s == "last_trade_price") return EventType::kLastTradePrice;
		if (s == "best_bid_ask") return EventType::kBestBidAsk;
		if (s == "new_market") return EventType::kNewMarket;
		if (s == "market_resolved") return EventType::kMarketResolved;

		return EventType::kUnknown;
	}
}
