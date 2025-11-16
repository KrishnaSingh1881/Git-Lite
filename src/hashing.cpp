#include "hashing.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <sodium.h>

namespace {

std::string toHex(const unsigned char *data, std::size_t length) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < length; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
    }
    return oss.str();
}

} // namespace

namespace hashing {

void ensureSodium() {
    if (sodium_init() < 0) {
        throw std::runtime_error("Failed to initialize libsodium.");
    }
}

std::string sha256Bytes(const unsigned char *data, std::size_t length) {
    unsigned char hash[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(hash, data, length);
    return toHex(hash, crypto_hash_sha256_BYTES);
}

std::string sha256String(const std::string &text) {
    return sha256Bytes(reinterpret_cast<const unsigned char *>(text.data()), text.size());
}

std::string sha256File(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Unable to open file for hashing: " + path.string());
    }
    std::vector<unsigned char> buffer(std::istreambuf_iterator<char>(in), {});
    return sha256Bytes(buffer.data(), buffer.size());
}

std::string hashPassword(const std::string &password) {
    char hash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hash,
                          password.c_str(),
                          password.size(),
                          crypto_pwhash_OPSLIMIT_MODERATE,
                          crypto_pwhash_MEMLIMIT_MODERATE) != 0) {
        throw std::runtime_error("Password hashing failed (insufficient resources).");
    }
    return std::string(hash);
}

bool verifyPassword(const std::string &hash, const std::string &password) {
    return crypto_pwhash_str_verify(hash.c_str(), password.c_str(), password.size()) == 0;
}

} // namespace hashing


