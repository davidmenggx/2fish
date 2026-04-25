#include "2fish/renderer/heatmap_lookup.h"

#include <format>
#include <stdexcept>

renderer::HeatmapLookup::HeatmapLookup() {
	// TODO: pass these in or make them a global constant
	RGBA blue = { 0, 0, 255 };
	RGBA cyan = { 0, 255, 255 };
	RGBA green = { 0, 255, 0 };
	RGBA yellow = { 255, 255, 0 };
	RGBA red = { 255, 0, 0 };

	for (int i{ 0 }; i < 101; ++i) {
		if (i < 25) {
			// blue to cyan
			lookup_table_[i] = lerpColor(blue, cyan, i / 25.0f);
		}
		else if (i < 50) {
			// cyan to green
			lookup_table_[i] = lerpColor(cyan, green, (i - 25) / 25.0f);
		}
		else if (i < 75) {
			// green to yellow
			lookup_table_[i] = lerpColor(green, yellow, (i - 50) / 25.0f);
		}
		else {
			// yellow to red
			lookup_table_[i] = lerpColor(yellow, red, (i - 75) / 25.0f);
		}
	}
}

renderer::RGBA renderer::HeatmapLookup::lerpColor(const RGBA& first_color,
	const RGBA& second_color, float transition_index) {
	return {
		static_cast<uint8_t>(first_color.red_ + transition_index * 
			(second_color.red_ - first_color.red_)),
		static_cast<uint8_t>(first_color.green_ + transition_index * 
			(second_color.green_ - first_color.green_)),
		static_cast<uint8_t>(first_color.blue_ + transition_index * 
			(second_color.blue_ - first_color.blue_))
	};
}

renderer::RGBA renderer::HeatmapLookup::getColor(uint8_t weight) {
	if (weight > 100) {
		throw std::runtime_error(std::format("Heatmap percentage out of bounds: {}", weight));
	}

	return lookup_table_[weight];
}
