#pragma once

#include "utils.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct User {
    std::string username;
    std::string passwordHash;
    std::string role; // "admin" or "user"
};

class StorageManager {
public:
    StorageManager();

    const std::filesystem::path &root() const;

    std::vector<User> loadUsers();
    void saveUsers(const std::vector<User> &users);

    std::unordered_map<std::string, std::set<std::string>> loadPermissions();
    void savePermissions(const std::unordered_map<std::string, std::set<std::string>> &perms);

    void ensureUserFolder(const std::string &username);

    std::vector<std::string> listUserRepos(const std::string &username);
    std::vector<std::pair<std::string, std::string>> listAllRepos();

    std::filesystem::path repoPath(const std::string &owner, const std::string &repo) const;
    bool repoExists(const std::string &owner, const std::string &repo) const;

    bool createRepo(const std::string &owner, const std::string &repo, std::string &error);

    bool setVisibility(const std::string &owner, const std::string &repo, bool isPublic);
    std::string getVisibility(const std::string &owner, const std::string &repo);

private:
    std::filesystem::path root_;

    static void ensureDirectory(const std::filesystem::path &path);
    static void ensureFile(const std::filesystem::path &path);
    static void writeFile(const std::filesystem::path &path, const std::string &content);

    static std::map<std::string, std::string> parseKeyValueFile(const std::filesystem::path &path);
    static bool writeKeyValueFile(const std::filesystem::path &path,
                                  const std::map<std::string, std::string> &kv);
};


