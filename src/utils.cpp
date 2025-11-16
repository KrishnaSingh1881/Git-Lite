#include "utils.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace gitlite::util {

std::vector<std::string> split(const std::string &text, char delim) {
    std::vector<std::string> tokens;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        tokens.push_back(item);
    }
    return tokens;
}

std::string trim(const std::string &text) {
    const char *ws = " \t\r\n";
    std::size_t start = text.find_first_not_of(ws);
    if (start == std::string::npos) {
        return {};
    }
    std::size_t end = text.find_last_not_of(ws);
    return text.substr(start, end - start + 1);
}

namespace {

std::tm localTime(std::time_t tt) {
#if defined(_WIN32) || defined(_MSC_VER)
    std::tm tm {};
    localtime_s(&tm, &tt);
    return tm;
#else
    std::tm tm {};
    localtime_r(&tt, &tm);
    return tm;
#endif
}

} // namespace

std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm = localTime(tt);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

bool isValidIdentifier(const std::string &value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.';
    });
}

} // namespace gitlite::util


