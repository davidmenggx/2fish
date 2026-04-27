#pragma once

#include "2fish/models/price_change.h"

namespace market {
	struct Trade {
		double size_{};
		int price_{}; // cents;
		Side side_{};
	};
}
