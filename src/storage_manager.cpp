#include "storage_manager.hpp"

#include "utils.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using gitlite::util::split;
using gitlite::util::timestamp;

StorageManager::StorageManager() {
    root_ = fs::current_path() / "storage";
    ensureDirectory(root_);
    ensureFile(root_ / "users.tsv");
    ensureFile(root_ / "permissions.tsv");
}

const fs::path &StorageManager::root() const {
    return root_;
}

std::vector<User> StorageManager::loadUsers() {
    std::vector<User> users;
    std::ifstream in(root_ / "users.tsv");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto parts = split(line, '\t');
        if (parts.size() >= 3) {
            users.push_back({parts[0], parts[1], parts[2]});
        }
    }
    return users;
}

void StorageManager::saveUsers(const std::vector<User> &users) {
    std::ofstream out(root_ / "users.tsv", std::ios::binary | std::ios::trunc);
    for (const auto &user : users) {
        out << user.username << '\t' << user.passwordHash << '\t' << user.role << "\n";
    }
}

std::unordered_map<std::string, std::set<std::string>> StorageManager::loadPermissions() {
    std::unordered_map<std::string, std::set<std::string>> perms;
    std::ifstream in(root_ / "permissions.tsv");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto parts = split(line, '\t');
        if (parts.empty()) {
            continue;
        }
        std::set<std::string> collaborators;
        if (parts.size() >= 2) {
            auto names = split(parts[1], ',');
            for (auto &name : names) {
                if (!name.empty()) {
                    collaborators.insert(name);
                }
            }
        }
        perms[parts[0]] = std::move(collaborators);
    }
    return perms;
}

void StorageManager::savePermissions(const std::unordered_map<std::string, std::set<std::string>> &perms) {
    std::ofstream out(root_ / "permissions.tsv", std::ios::binary | std::ios::trunc);
    for (const auto &entry : perms) {
        out << entry.first << '\t';
        bool first = true;
        for (const auto &collab : entry.second) {
            if (!first) {
                out << ',';
            }
            out << collab;
            first = false;
        }
        out << "\n";
    }
}

void StorageManager::ensureUserFolder(const std::string &username) {
    ensureDirectory(root_ / username);
}

std::vector<std::string> StorageManager::listUserRepos(const std::string &username) {
    std::vector<std::string> repos;
    fs::path userPath = root_ / username;
    if (!fs::exists(userPath)) {
        return repos;
    }
    for (const auto &entry : fs::directory_iterator(userPath)) {
        if (entry.is_directory()) {
            repos.push_back(entry.path().filename().string());
        }
    }
    std::sort(repos.begin(), repos.end());
    return repos;
}

std::vector<std::pair<std::string, std::string>> StorageManager::listAllRepos() {
    std::vector<std::pair<std::string, std::string>> repos;
    if (!fs::exists(root_)) {
        return repos;
    }
    for (const auto &userEntry : fs::directory_iterator(root_)) {
        if (!userEntry.is_directory()) {
            continue;
        }
        const auto &userName = userEntry.path().filename().string();
        if (!userName.empty() && userName.front() == '_') {
            continue;
        }
        for (const auto &repoEntry : fs::directory_iterator(userEntry.path())) {
            if (repoEntry.is_directory()) {
                repos.emplace_back(userName, repoEntry.path().filename().string());
            }
        }
    }
    std::sort(repos.begin(), repos.end());
    return repos;
}

fs::path StorageManager::repoPath(const std::string &owner, const std::string &repo) const {
    return root_ / owner / repo;
}

bool StorageManager::repoExists(const std::string &owner, const std::string &repo) const {
    return fs::exists(repoPath(owner, repo));
}

bool StorageManager::createRepo(const std::string &owner, const std::string &repo, std::string &error) {
    fs::path repoRoot = repoPath(owner, repo);
    if (fs::exists(repoRoot)) {
        error = "Repository already exists.";
        return false;
    }
    try {
        fs::create_directories(repoRoot / ".glite" / "objects");
        fs::create_directories(repoRoot / ".glite" / "refs" / "heads");
        fs::create_directories(repoRoot / "workspace");
        writeFile(repoRoot / ".glite" / "HEAD", "ref: main\n");
        writeFile(repoRoot / ".glite" / "refs" / "heads" / "main", "");
        writeFile(repoRoot / ".glite" / "index", "");
        writeFile(repoRoot / ".glite" / "config",
                  "name=" + repo + "\nowner=" + owner + "\nvisibility=private\ncreated=" + timestamp() + "\n");
        writeFile(repoRoot / ".glite" / "log", "");
    } catch (const std::exception &ex) {
        error = std::string("Failed to create repository: ") + ex.what();
        return false;
    }
    return true;
}

bool StorageManager::setVisibility(const std::string &owner, const std::string &repo, bool isPublic) {
    fs::path cfg = repoPath(owner, repo) / ".glite" / "config";
    if (!fs::exists(cfg)) {
        return false;
    }
    auto kv = parseKeyValueFile(cfg);
    kv["visibility"] = isPublic ? "public" : "private";
    return writeKeyValueFile(cfg, kv);
}

std::string StorageManager::getVisibility(const std::string &owner, const std::string &repo) {
    fs::path cfg = repoPath(owner, repo) / ".glite" / "config";
    if (!fs::exists(cfg)) {
        return "private";
    }
    auto kv = parseKeyValueFile(cfg);
    auto it = kv.find("visibility");
    if (it == kv.end()) {
        return "private";
    }
    return it->second;
}

void StorageManager::ensureDirectory(const fs::path &path) {
    if (!fs::exists(path)) {
        fs::create_directories(path);
    }
}

void StorageManager::ensureFile(const fs::path &path) {
    if (!fs::exists(path)) {
        std::ofstream out(path, std::ios::binary);
    }
}

void StorageManager::writeFile(const fs::path &path, const std::string &content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

std::map<std::string, std::string> StorageManager::parseKeyValueFile(const fs::path &path) {
    std::map<std::string, std::string> kv;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        auto key = line.substr(0, pos);
        auto value = line.substr(pos + 1);
        kv[key] = value;
    }
    return kv;
}

bool StorageManager::writeKeyValueFile(const fs::path &path,
                                       const std::map<std::string, std::string> &kv) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const auto &entry : kv) {
        out << entry.first << "=" << entry.second << "\n";
    }
    return true;
}


