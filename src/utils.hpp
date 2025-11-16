#pragma once

#include <string>
#include <vector>

namespace gitlite::util {

std::vector<std::string> split(const std::string &text, char delim);

std::string trim(const std::string &text);

std::string timestamp();

bool isValidIdentifier(const std::string &value);

} // namespace gitlite::util


