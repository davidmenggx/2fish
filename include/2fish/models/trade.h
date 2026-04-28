#pragma once

#include "2fish/models/types.h"

namespace market {
	struct Trade {
		double size_{};
		int price_{}; // cents;
		Side side_{};
	};
}
