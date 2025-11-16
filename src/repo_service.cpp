#include "repo_service.hpp"

#include "hashing.hpp"
#include "utils.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using gitlite::util::split;
using gitlite::util::timestamp;
using gitlite::util::trim;

RepoService::RepoService(StorageManager &storage)
    : storage_(storage) {}

bool RepoService::isPublic(const std::string &owner, const std::string &repo) const {
    return storage_.getVisibility(owner, repo) == "public";
}

std::string RepoService::currentBranch(const fs::path &repoRoot) const {
    std::ifstream in(repoRoot / ".glite" / "HEAD");
    std::string line;
    if (!std::getline(in, line)) {
        return "main";
    }
    line = trim(line);
    if (line.rfind("ref:", 0) == 0) {
        return trim(line.substr(4));
    }
    return "main";
}

void RepoService::setCurrentBranch(const fs::path &repoRoot, const std::string &branch) const {
    std::ofstream out(repoRoot / ".glite" / "HEAD", std::ios::binary | std::ios::trunc);
    out << "ref: " << branch << "\n";
}

std::string RepoService::branchHead(const fs::path &repoRoot, const std::string &branch) const {
    std::ifstream in(repoRoot / ".glite" / "refs" / "heads" / branch);
    std::string line;
    if (!std::getline(in, line)) {
        return {};
    }
    return trim(line);
}

bool RepoService::updateBranchHead(const fs::path &repoRoot,
                                   const std::string &branch,
                                   const std::string &commitId) const {
    std::ofstream out(repoRoot / ".glite" / "refs" / "heads" / branch, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << commitId << "\n";
    return true;
}

std::vector<std::pair<std::string, std::string>> RepoService::listBranchesWithHead(const fs::path &repoRoot) const {
    std::vector<std::pair<std::string, std::string>> branches;
    fs::path dir = repoRoot / ".glite" / "refs" / "heads";
    if (!fs::exists(dir)) {
        return branches;
    }
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            std::string branch = entry.path().filename().string();
            branches.emplace_back(branch, branchHead(repoRoot, branch));
        }
    }
    std::sort(branches.begin(), branches.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
    });
    return branches;
}

std::vector<std::pair<std::string, std::string>> RepoService::readIndex(const fs::path &repoRoot) const {
    std::vector<std::pair<std::string, std::string>> entries;
    std::ifstream in(repoRoot / ".glite" / "index");
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto parts = split(line, '\t');
        if (parts.size() == 2) {
            entries.emplace_back(parts[0], parts[1]);
        }
    }
    return entries;
}

void RepoService::writeIndex(const fs::path &repoRoot,
                             const std::vector<std::pair<std::string, std::string>> &entries) const {
    std::ofstream out(repoRoot / ".glite" / "index", std::ios::binary | std::ios::trunc);
    for (const auto &entry : entries) {
        out << entry.first << '\t' << entry.second << "\n";
    }
}

bool RepoService::addFile(const fs::path &repoRoot,
                          const std::string &relativePath,
                          std::string &message) const {
    fs::path workspace = repoRoot / "workspace";
    fs::path source = workspace / relativePath;
    if (!fs::exists(source)) {
        message = "File not found in workspace.";
        return false;
    }
    std::string blobId;
    try {
        blobId = hashing::sha256File(source);
    } catch (const std::exception &ex) {
        message = ex.what();
        return false;
    }
    fs::path objectPath = repoRoot / ".glite" / "objects" / blobId;
    try {
        if (!fs::exists(objectPath)) {
            fs::copy_file(source, objectPath);
        }
        auto entries = readIndex(repoRoot);
        bool replaced = false;
        for (auto &entry : entries) {
            if (entry.first == relativePath) {
                entry.second = blobId;
                replaced = true;
            }
        }
        if (!replaced) {
            entries.emplace_back(relativePath, blobId);
        }
        writeIndex(repoRoot, entries);
    } catch (const std::exception &ex) {
        message = ex.what();
        return false;
    }
    message = "File staged: " + relativePath;
    return true;
}

bool RepoService::commit(const fs::path &repoRoot,
                         const std::string &author,
                         const std::string &message,
                         CommitRecord &record,
                         std::string &error) const {
    auto indexEntries = readIndex(repoRoot);
    if (indexEntries.empty()) {
        error = "Nothing to commit (index empty).";
        return false;
    }
    std::string branch = currentBranch(repoRoot);
    std::string parent = branchHead(repoRoot, branch);

    std::string ts = timestamp();

    std::ostringstream body;
    body << "author=" << author << "\n";
    body << "timestamp=" << ts << "\n";
    body << "branch=" << branch << "\n";
    body << "parent=" << (parent.empty() ? "null" : parent) << "\n";
    body << "message=" << message << "\n";
    body << "files:\n";
    for (const auto &entry : indexEntries) {
        body << entry.first << '\t' << entry.second << "\n";
    }

    std::string bodyContent = body.str();
    std::string commitId = hashing::sha256String(bodyContent);

    std::ostringstream commitFile;
    commitFile << "id=" << commitId << "\n" << bodyContent;

    fs::path objectPath = repoRoot / ".glite" / "objects" / commitId;
    try {
        std::ofstream out(objectPath, std::ios::binary | std::ios::trunc);
        out << commitFile.str();
        out.flush();

        updateBranchHead(repoRoot, branch, commitId);
        writeIndex(repoRoot, {});
    } catch (const std::exception &ex) {
        error = ex.what();
        return false;
    }

    record = {commitId, parent, author, ts, message, branch, indexEntries};
    appendLog(repoRoot, record);
    return true;
}

bool RepoService::createBranch(const fs::path &repoRoot,
                               const std::string &branchName,
                               std::string &error) const {
    fs::path path = repoRoot / ".glite" / "refs" / "heads" / branchName;
    if (fs::exists(path)) {
        error = "Branch already exists.";
        return false;
    }
    std::string current = currentBranch(repoRoot);
    std::string head = branchHead(repoRoot, current);
    try {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << head << "\n";
    } catch (const std::exception &ex) {
        error = ex.what();
        return false;
    }
    return true;
}

bool RepoService::push(const fs::path &repoRoot,
                       const fs::path &remoteRoot,
                       std::string &error) const {
    try {
        if (fs::exists(remoteRoot)) {
            fs::remove_all(remoteRoot);
        }
        fs::create_directories(remoteRoot);
        copyDirectory(repoRoot / ".glite", remoteRoot / ".glite");
        copyDirectory(repoRoot / "workspace", remoteRoot / "workspace");
    } catch (const std::exception &ex) {
        error = ex.what();
        return false;
    }
    return true;
}

bool RepoService::pull(const fs::path &repoRoot,
                       const fs::path &remoteRoot,
                       std::string &error) const {
    try {
        if (!fs::exists(remoteRoot)) {
            error = "Remote not found.";
            return false;
        }
        copyDirectory(remoteRoot / ".glite", repoRoot / ".glite");
        copyDirectory(remoteRoot / "workspace", repoRoot / "workspace");
    } catch (const std::exception &ex) {
        error = ex.what();
        return false;
    }
    return true;
}

std::vector<CommitRecord> RepoService::history(const fs::path &repoRoot,
                                               const std::string &branch,
                                               std::size_t limit) const {
    std::vector<CommitRecord> result;
    std::string head = branchHead(repoRoot, branch);
    std::string current = head;
    while (!current.empty() && result.size() < limit) {
        if (!commitExists(repoRoot, current)) {
            break;
        }
        CommitRecord record = readCommit(repoRoot, current);
        if (record.id.empty()) {
            break;
        }
        result.push_back(record);
        current = record.parent;
    }
    return result;
}

void RepoService::appendLog(const fs::path &repoRoot, const CommitRecord &record) {
    std::ofstream out(repoRoot / ".glite" / "log", std::ios::binary | std::ios::app);
    out << record.id << '\t' << record.branch << '\t' << record.timestamp << '\t' << record.message << "\n";
}

CommitRecord RepoService::readCommit(const fs::path &repoRoot, const std::string &commitId) {
    CommitRecord record;
    record.id = commitId;
    std::ifstream in(repoRoot / ".glite" / "objects" / commitId);
    if (!in) {
        return record;
    }
    std::string line;
    bool filesSection = false;
    while (std::getline(in, line)) {
        if (line.rfind("id=", 0) == 0) {
            record.id = line.substr(3);
            continue;
        }
        if (line == "files:") {
            filesSection = true;
            continue;
        }
        if (!filesSection) {
            auto pos = line.find('=');
            if (pos == std::string::npos) {
                continue;
            }
            auto key = line.substr(0, pos);
            auto value = line.substr(pos + 1);
            if (key == "author") {
                record.author = value;
            } else if (key == "timestamp") {
                record.timestamp = value;
            } else if (key == "branch") {
                record.branch = value;
            } else if (key == "parent") {
                record.parent = value == "null" ? std::string{} : value;
            } else if (key == "message") {
                record.message = value;
            }
        } else {
            auto parts = split(line, '\t');
            if (parts.size() == 2) {
                record.files.emplace_back(parts[0], parts[1]);
            }
        }
    }
    return record;
}

bool RepoService::commitExists(const fs::path &repoRoot, const std::string &commitId) {
    return fs::exists(repoRoot / ".glite" / "objects" / commitId);
}

void RepoService::copyDirectory(const fs::path &from, const fs::path &to) {
    if (!fs::exists(from)) {
        return;
    }
    if (fs::exists(to)) {
        fs::remove_all(to);
    }
    fs::create_directories(to);
    for (const auto &entry : fs::recursive_directory_iterator(from)) {
        const auto relative = fs::relative(entry.path(), from);
        const auto target = to / relative;
        if (entry.is_directory()) {
            fs::create_directories(target);
        } else if (entry.is_regular_file()) {
            fs::create_directories(target.parent_path());
            fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
        }
    }
}

bool RepoService::mergeBranch(const fs::path &repoRoot, const std::string &branch, std::string &error) const {
    std::string current = currentBranch(repoRoot);
    if (current == branch) {
        error = "Cannot merge branch into itself.";
        return false;
    }
    
    std::string branchHeadId = branchHead(repoRoot, branch);
    if (branchHeadId.empty()) {
        error = "Branch '" + branch + "' has no commits.";
        return false;
    }
    
    std::string currentHeadId = branchHead(repoRoot, current);
    
    // Create merge commit
    CommitRecord mergeRecord;
    mergeRecord.id = hashing::sha256String(branchHeadId + currentHeadId + timestamp());
    mergeRecord.parent = currentHeadId;
    mergeRecord.author = "merge";
    mergeRecord.timestamp = timestamp();
    mergeRecord.message = "Merge branch '" + branch + "' into '" + current + "'";
    mergeRecord.branch = current;
    
    // Copy files from both branches (simplified merge)
    auto branchCommit = readCommit(repoRoot, branchHeadId);
    for (const auto &file : branchCommit.files) {
        mergeRecord.files.push_back(file);
    }
    
    // Write merge commit
    fs::path commitPath = repoRoot / ".glite" / "objects" / mergeRecord.id;
    std::ofstream out(commitPath);
    out << "id: " << mergeRecord.id << "\n";
    out << "parent: " << (mergeRecord.parent.empty() ? "null" : mergeRecord.parent) << "\n";
    out << "author: " << mergeRecord.author << "\n";
    out << "timestamp: " << mergeRecord.timestamp << "\n";
    out << "branch: " << mergeRecord.branch << "\n";
    out << "message: " << mergeRecord.message << "\n";
    for (const auto &file : mergeRecord.files) {
        out << file.first << "\t" << file.second << "\n";
    }
    
    updateBranchHead(repoRoot, current, mergeRecord.id);
    appendLog(repoRoot, mergeRecord);
    
    return true;
}

bool RepoService::rebaseBranch(const fs::path &repoRoot, const std::string &branch, std::string &error) const {
    std::string current = currentBranch(repoRoot);
    if (current == branch) {
        error = "Cannot rebase branch onto itself.";
        return false;
    }
    
    std::string branchHeadId = branchHead(repoRoot, branch);
    if (branchHeadId.empty()) {
        error = "Branch '" + branch + "' has no commits.";
        return false;
    }
    
    // Simplified rebase: just update current branch head to point to branch head
    updateBranchHead(repoRoot, current, branchHeadId);
    return true;
}

bool RepoService::renameBranch(const fs::path &repoRoot, const std::string &oldName, const std::string &newName, std::string &error) const {
    fs::path oldRef = repoRoot / ".glite" / "refs" / "heads" / oldName;
    fs::path newRef = repoRoot / ".glite" / "refs" / "heads" / newName;
    
    if (!fs::exists(oldRef)) {
        error = "Branch '" + oldName + "' not found.";
        return false;
    }
    
    if (fs::exists(newRef)) {
        error = "Branch '" + newName + "' already exists.";
        return false;
    }
    
    try {
        fs::rename(oldRef, newRef);
        
        // Update HEAD if it points to old branch
        std::string current = currentBranch(repoRoot);
        if (current == oldName) {
            setCurrentBranch(repoRoot, newName);
        }
        
        return true;
    } catch (const std::exception &ex) {
        error = ex.what();
        return false;
    }
}

bool RepoService::deleteBranch(const fs::path &repoRoot, const std::string &branchName, std::string &error) const {
    fs::path branchRef = repoRoot / ".glite" / "refs" / "heads" / branchName;
    
    if (!fs::exists(branchRef)) {
        error = "Branch '" + branchName + "' not found.";
        return false;
    }
    
    std::string current = currentBranch(repoRoot);
    if (current == branchName) {
        error = "Cannot delete current branch.";
        return false;
    }
    
    try {
        fs::remove(branchRef);
        return true;
    } catch (const std::exception &ex) {
        error = ex.what();
        return false;
    }
}

bool RepoService::removeFile(const fs::path &repoRoot, const std::string &relativePath, std::string &error) const {
    auto entries = readIndex(repoRoot);
    auto it = std::find_if(entries.begin(), entries.end(),
                          [&](const auto &e) { return e.first == relativePath; });
    
    if (it == entries.end()) {
        error = "File not in index.";
        return false;
    }
    
    entries.erase(it);
    writeIndex(repoRoot, entries);
    
    // Remove from workspace
    fs::path filePath = repoRoot / "workspace" / relativePath;
    if (fs::exists(filePath)) {
        fs::remove(filePath);
    }
    
    return true;
}

std::string RepoService::getDiff(const fs::path &repoRoot) const {
    auto entries = readIndex(repoRoot);
    if (entries.empty()) {
        return "No changes staged.";
    }
    
    std::string result = "Staged changes:\n";
    for (const auto &entry : entries) {
        result += "  " + entry.first + "\n";
    }
    return result;
}

bool RepoService::resetFile(const fs::path &repoRoot, const std::string &relativePath, std::string &error) const {
    auto entries = readIndex(repoRoot);
    auto it = std::find_if(entries.begin(), entries.end(),
                          [&](const auto &e) { return e.first == relativePath; });
    
    if (it == entries.end()) {
        error = "File not in index.";
        return false;
    }
    
    entries.erase(it);
    writeIndex(repoRoot, entries);
    return true;
}

bool RepoService::addIgnorePattern(const fs::path &repoRoot, const std::string &pattern, std::string &error) const {
    fs::path ignoreFile = repoRoot / ".gliteignore";
    std::ofstream out(ignoreFile, std::ios::app);
    if (!out) {
        error = "Could not write to .gliteignore.";
        return false;
    }
    out << pattern << "\n";
    return true;
}

bool RepoService::createTag(const fs::path &repoRoot, const std::string &tagName, std::string &error) const {
    fs::path tagsDir = repoRoot / ".glite" / "refs" / "tags";
    fs::create_directories(tagsDir);
    
    fs::path tagFile = tagsDir / tagName;
    if (fs::exists(tagFile)) {
        error = "Tag '" + tagName + "' already exists.";
        return false;
    }
    
    std::string currentHead = branchHead(repoRoot, currentBranch(repoRoot));
    if (currentHead.empty()) {
        error = "No commits to tag.";
        return false;
    }
    
    std::ofstream out(tagFile);
    out << currentHead << "\n";
    return true;
}

std::vector<std::string> RepoService::listTags(const fs::path &repoRoot) const {
    std::vector<std::string> tags;
    fs::path tagsDir = repoRoot / ".glite" / "refs" / "tags";
    
    if (fs::exists(tagsDir)) {
        for (const auto &entry : fs::directory_iterator(tagsDir)) {
            if (entry.is_regular_file()) {
                tags.push_back(entry.path().filename().string());
            }
        }
    }
    
    return tags;
}

CommitRecord RepoService::getCommit(const fs::path &repoRoot, const std::string &commitId) const {
    if (!commitExists(repoRoot, commitId)) {
        return CommitRecord{};
    }
    return readCommit(repoRoot, commitId);
}

bool RepoService::revertCommit(const fs::path &repoRoot, const std::string &commitId, const std::string &author, std::string &error) const {
    if (!commitExists(repoRoot, commitId)) {
        error = "Commit not found.";
        return false;
    }
    
    CommitRecord originalCommit = readCommit(repoRoot, commitId);
    std::string current = currentBranch(repoRoot);
    std::string currentHead = branchHead(repoRoot, current);
    
    // Create revert commit
    CommitRecord revertRecord;
    revertRecord.id = hashing::sha256String(commitId + currentHead + timestamp());
    revertRecord.parent = currentHead;
    revertRecord.author = author;
    revertRecord.timestamp = timestamp();
    revertRecord.message = "Revert: " + originalCommit.message;
    revertRecord.branch = current;
    
    // Copy files from parent commit (simplified revert)
    if (!originalCommit.parent.empty()) {
        CommitRecord parentCommit = readCommit(repoRoot, originalCommit.parent);
        revertRecord.files = parentCommit.files;
    }
    
    // Write revert commit
    fs::path commitPath = repoRoot / ".glite" / "objects" / revertRecord.id;
    std::ofstream out(commitPath);
    out << "id: " << revertRecord.id << "\n";
    out << "parent: " << (revertRecord.parent.empty() ? "null" : revertRecord.parent) << "\n";
    out << "author: " << revertRecord.author << "\n";
    out << "timestamp: " << revertRecord.timestamp << "\n";
    out << "branch: " << revertRecord.branch << "\n";
    out << "message: " << revertRecord.message << "\n";
    for (const auto &file : revertRecord.files) {
        out << file.first << "\t" << file.second << "\n";
    }
    
    updateBranchHead(repoRoot, current, revertRecord.id);
    appendLog(repoRoot, revertRecord);
    
    return true;
}


