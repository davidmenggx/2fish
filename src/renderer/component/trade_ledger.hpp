#pragma once

#include "common/containers/simple_deque.hpp"
#include "common/core/types.hpp"
#include "component.hpp"
#include "engine/engine.hpp"

#include <cstdint>
#include <string>

struct TradeLedgerItem {
  std::string market_ticker_{};
  std::string trade_id_{};
  uint8_t yes_price_cents_{};
  uint8_t no_price_cents_{};
  double contracts_traded_{}; // The "count_fp" field
  Side taker_side_{Side::Unknown};
  int64_t timestamp_ms_{};
};

class TradeLedger : public Component {
public:
  TradeLedger(Engine &engine);

  void draw() override;

  std::string getId() override { return "Trade Ledger"; }

private:
  void fetchData();

  Engine &engine_;

  SimpleDeque<TradeLedgerItem, 128> trade_ledger_{};
};
