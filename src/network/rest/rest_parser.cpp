#include "rest_parser.hpp"
#include "common/core/rest_data_types.hpp"

#include "moodycamel/readerwriterqueue.h"

#include <simdjson.h>

#include <iostream>

RestParser::RestParser(
    moodycamel::ReaderWriterQueue<RestMessage> &rest_patch_queue)
    : rest_patch_queue_{rest_patch_queue} {}

void RestParser::parseAndPush(simdjson::padded_string_view padded_json) {
  RestMessage message{};

  std::cout << "Got: " << padded_json << '\n';

  // TODO: Parse out the message here

  // rest_patch_queue_.try_emplace(message);
}
