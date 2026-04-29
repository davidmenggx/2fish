#pragma once

#include "2fish/models/trade.h"

#include <algorithm>
#include <cstdint>

class Candlestick {
public:
    int64_t start_time_{};
    int open_{ 50 };
    int high_{ 50 };
    int low_{ 50 };
    int close_{ 50 };
    double volume_{};

    void apply_trade(const market::Trade& trade) {
        if (trade.size_ <= 0.0) {
            return;
        }

        if (volume_ == 0.0) {
            open_ = trade.price_;
            high_ = trade.price_;
            low_ = trade.price_;
            close_ = trade.price_;
        }
        else {
            high_ = std::max(high_, trade.price_);
            low_ = std::min(low_, trade.price_);

            close_ = trade.price_;
        }

        volume_ += trade.size_;
    }

    void reset() {
        open_ = close_;
        high_ = close_;
        low_ = close_;
        volume_ = 0;
    }
};
