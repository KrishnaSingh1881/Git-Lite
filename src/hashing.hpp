#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace hashing {

void ensureSodium();

std::string sha256Bytes(const unsigned char *data, std::size_t length);

std::string sha256String(const std::string &text);

std::string sha256File(const std::filesystem::path &path);

std::string hashPassword(const std::string &password);

bool verifyPassword(const std::string &hash, const std::string &password);

} // namespace hashing


