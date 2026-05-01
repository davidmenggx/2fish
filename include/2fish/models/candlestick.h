#pragma once

#include <cstdint>

struct Candlestick {
	int64_t start_timestamp_{};
	int open_{ 50 };
	int high_{ 50 };
	int low_{ 50 };
	int close_{ 50 };
	double volume_{};
};
