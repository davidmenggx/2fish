#pragma once

#include "common/core/rest_data_types.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

class RestParser {
public:
  RestParser(moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue);

  void parseAndPush(simdjson::padded_string_view padded_json);

private:
  simdjson::ondemand::parser parser_{};
  moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue_;
};
