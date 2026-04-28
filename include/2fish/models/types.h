#pragma once

#include <string_view>

namespace market {
	enum class EventType {
		BOOK_SNAPSHOT, // really this is event_type "book"
		PRICE_CHANGE,
		LAST_TRADE_PRICE,

		// THE FOLLOWING ARE NOT IMPLEMENTED YET, THEY WILL BE SILENTLY DROPPED
		// you actually dont need most of these as long as dont enable custom features
		TICK_SIZE_CHANGE,
		BEST_BID_ASK,
		NEW_MARKET,
		MARKET_RESOLVED,

		UNKNOWN
	};

	[[nodiscard]] inline EventType stringToEventType(std::string_view s) {
		if (s == "book") return EventType::BOOK_SNAPSHOT;
		if (s == "price_change") return EventType::PRICE_CHANGE;
		if (s == "tick_size_change") return EventType::TICK_SIZE_CHANGE;
		if (s == "last_trade_price") return EventType::LAST_TRADE_PRICE;
		if (s == "best_bid_ask") return EventType::BEST_BID_ASK;
		if (s == "new_market") return EventType::NEW_MARKET;
		if (s == "market_resolved") return EventType::MARKET_RESOLVED;

		return EventType::UNKNOWN;
	}

	enum class Side { BUY, SELL };
}
