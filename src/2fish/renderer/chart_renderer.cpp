#include "2fish/models/market_snapshot.h"
#include "2fish/renderer/chart_renderer.h"

#include <iostream>

void renderer::ChartRenderer::updateAndDraw(const MarketSnapshot* snapshot) {
	heatmap_history_.push(*snapshot);

	std::cout << "Bids\tAsks\n";

	for (std::size_t i{ snapshot->bids_.size() }; i-- > 0;) {
		std::cout << i << '\t' << snapshot->bids_[i] << '\t' << snapshot->asks_[i] << '\n';
	}
	std::cout << "\n\n\n";
}
