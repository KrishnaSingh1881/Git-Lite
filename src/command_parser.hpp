#pragma once

#include "storage_manager.hpp"
#include "repo_service.hpp"
#include "terminal_ui.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>


struct CommandResult {
    bool success;
    std::string message;
    bool shouldExit = false;
};

class CommandParser {
public:
    CommandParser(StorageManager &storage, RepoService &repoService, TerminalUI &ui);
    
    CommandResult execute(const std::string &command, std::optional<User> &session);
    
private:
    StorageManager &storage_;
    RepoService &repoService_;
    TerminalUI &ui_;
    
    // Command handlers
    CommandResult handleSignup(const std::vector<std::string> &args);
    CommandResult handleLogin(const std::vector<std::string> &args, std::optional<User> &session);
    CommandResult handleLogout(std::optional<User> &session);
    CommandResult handleWhoami(const std::optional<User> &session);
    CommandResult handleChangepass(const std::vector<std::string> &args, std::optional<User> &session);
    CommandResult handleUsersList(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleUsersDelete(const std::vector<std::string> &args, const std::optional<User> &session);
    
    CommandResult handleInit(const std::vector<std::string> &args);
    CommandResult handleCreate(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleClone(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleDelete(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleSetPublic(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleSetPrivate(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleList(const std::optional<User> &session);
    CommandResult handleLsUsers();
    CommandResult handleLsRepos(const std::vector<std::string> &args);
    CommandResult handleView(const std::vector<std::string> &args);
    
    CommandResult handleAdd(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleRm(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleStatus(const std::optional<User> &session);
    CommandResult handleDiff(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleReset(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleIgnore(const std::vector<std::string> &args, const std::optional<User> &session);
    
    CommandResult handleCommit(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleLog(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleShow(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleRevert(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleTag(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleTags(const std::optional<User> &session);
    CommandResult handleCheckout(const std::vector<std::string> &args, const std::optional<User> &session);
    
    CommandResult handleBranch(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleMerge(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleRebase(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleRenameBranch(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleDeleteBranch(const std::vector<std::string> &args, const std::optional<User> &session);
    
    CommandResult handlePush(const std::optional<User> &session);
    CommandResult handlePull(const std::optional<User> &session);
    CommandResult handleFetch(const std::optional<User> &session);
    CommandResult handleRemote(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleSync(const std::optional<User> &session);
    
    CommandResult handlePermAdd(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handlePermRm(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handlePermList(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleTransfer(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleFork(const std::vector<std::string> &args, const std::optional<User> &session);
    
    CommandResult handleMakeAdmin(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleRemoveAdmin(const std::vector<std::string> &args, const std::optional<User> &session);
    CommandResult handleReposAll(const std::optional<User> &session);
    
    CommandResult handleMenu(const std::optional<User> &session);
    CommandResult handleHelp(const std::vector<std::string> &args);
    CommandResult handleClear();
    CommandResult handleVersion();
    CommandResult handleConfig(const std::vector<std::string> &args);
    CommandResult handleHistory();
    
    // Helper functions
    std::filesystem::path getCurrentRepoPath(const std::optional<User> &session);
    bool isInRepo();
    bool hasWriteAccess(const std::string &owner, const std::string &repo, const std::optional<User> &session);
    std::vector<std::string> splitCommand(const std::string &command);
};

