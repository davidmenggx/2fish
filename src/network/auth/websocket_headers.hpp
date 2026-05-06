#pragma once

#include <string>

std::string getSigningTimestampMs();

std::string generateKalshiSignature(const std::string &message,
                                    const std::string &key_path);
