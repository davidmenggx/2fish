#pragma once

#include "common/core/rest_data_types.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

class RestParser {
public:
  RestParser(moodycamel::ReaderWriterQueue<RestMessage> &output_data_queue);

  void parseAndPush(simdjson::padded_string_view padded_json);

private:
  moodycamel::ReaderWriterQueue<RestMessage> &output_data_queue_;
};
