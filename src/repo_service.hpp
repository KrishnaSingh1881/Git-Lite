#pragma once

#include "storage_manager.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct CommitRecord {
    std::string id;
    std::string parent;
    std::string author;
    std::string timestamp;
    std::string message;
    std::string branch;
    std::vector<std::pair<std::string, std::string>> files;
};

class RepoService {
public:
    explicit RepoService(StorageManager &storage);

    bool isPublic(const std::string &owner, const std::string &repo) const;

    std::string currentBranch(const std::filesystem::path &repoRoot) const;
    void setCurrentBranch(const std::filesystem::path &repoRoot, const std::string &branch) const;

    std::string branchHead(const std::filesystem::path &repoRoot, const std::string &branch) const;
    bool updateBranchHead(const std::filesystem::path &repoRoot,
                          const std::string &branch,
                          const std::string &commitId) const;

    std::vector<std::pair<std::string, std::string>> listBranchesWithHead(const std::filesystem::path &repoRoot) const;

    std::vector<std::pair<std::string, std::string>> readIndex(const std::filesystem::path &repoRoot) const;
    void writeIndex(const std::filesystem::path &repoRoot,
                    const std::vector<std::pair<std::string, std::string>> &entries) const;

    bool addFile(const std::filesystem::path &repoRoot,
                 const std::string &relativePath,
                 std::string &message) const;

    bool commit(const std::filesystem::path &repoRoot,
                const std::string &author,
                const std::string &message,
                CommitRecord &record,
                std::string &error) const;

    bool createBranch(const std::filesystem::path &repoRoot,
                      const std::string &branchName,
                      std::string &error) const;

    bool push(const std::filesystem::path &repoRoot,
              const std::filesystem::path &remoteRoot,
              std::string &error) const;

    bool pull(const std::filesystem::path &repoRoot,
              const std::filesystem::path &remoteRoot,
              std::string &error) const;

    std::vector<CommitRecord> history(const std::filesystem::path &repoRoot,
                                      const std::string &branch,
                                      std::size_t limit = 50) const;

    bool mergeBranch(const std::filesystem::path &repoRoot,
                     const std::string &branch,
                     std::string &error) const;
    
    bool rebaseBranch(const std::filesystem::path &repoRoot,
                      const std::string &branch,
                      std::string &error) const;
    
    bool renameBranch(const std::filesystem::path &repoRoot,
                      const std::string &oldName,
                      const std::string &newName,
                      std::string &error) const;
    
    bool deleteBranch(const std::filesystem::path &repoRoot,
                      const std::string &branchName,
                      std::string &error) const;
    
    bool removeFile(const std::filesystem::path &repoRoot,
                    const std::string &relativePath,
                    std::string &error) const;
    
    std::string getDiff(const std::filesystem::path &repoRoot) const;
    
    bool resetFile(const std::filesystem::path &repoRoot,
                   const std::string &relativePath,
                   std::string &error) const;
    
    bool addIgnorePattern(const std::filesystem::path &repoRoot,
                          const std::string &pattern,
                          std::string &error) const;
    
    bool createTag(const std::filesystem::path &repoRoot,
                   const std::string &tagName,
                   std::string &error) const;
    
    std::vector<std::string> listTags(const std::filesystem::path &repoRoot) const;
    
    CommitRecord getCommit(const std::filesystem::path &repoRoot,
                           const std::string &commitId) const;
    
    bool revertCommit(const std::filesystem::path &repoRoot,
                      const std::string &commitId,
                      const std::string &author,
                      std::string &error) const;

private:
    StorageManager &storage_;

    static void appendLog(const std::filesystem::path &repoRoot, const CommitRecord &record);
    static CommitRecord readCommit(const std::filesystem::path &repoRoot, const std::string &commitId);
    static bool commitExists(const std::filesystem::path &repoRoot, const std::string &commitId);
    static void copyDirectory(const std::filesystem::path &from, const std::filesystem::path &to);
};


