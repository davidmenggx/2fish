#pragma once

#include <array>
#include <cstdint>

namespace renderer {
	struct RGBA {
		uint8_t red_{};
		uint8_t green_{};
		uint8_t blue_{};
		uint8_t alpha_{ 255 };
	};

	class HeatmapLookup {
	public:
		HeatmapLookup();

		[[nodiscard]] RGBA getColor(uint8_t weight);

	private:
		[[nodiscard]] RGBA lerpColor(const RGBA & first_color, const RGBA & second_color,
			float transition_index);

		std::array<RGBA, 101> lookup_table_{};
	};
}
