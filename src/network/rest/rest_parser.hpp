#pragma once

#include "common/core/rest_data_types.hpp"

#include "moodycamel/concurrentqueue.h"

#include <simdjson.h>

class RestParser {
public:
  RestParser(moodycamel::ConcurrentQueue<RestMessage> &output_data_queue);

  void parseAndPush(simdjson::padded_string_view padded_json);

private:
  moodycamel::ConcurrentQueue<RestMessage> &output_data_queue_;
};
