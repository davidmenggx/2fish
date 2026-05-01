#pragma once

#include "2fish/models/types.h"

#include <cstdint>

namespace market {
	struct Trade {
		double size_{};
		int price_{}; // cents;
		Side side_{};

		int64_t timestamp_{};
	};
}
