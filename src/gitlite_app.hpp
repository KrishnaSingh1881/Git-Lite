#pragma once

#include "repo_service.hpp"
#include "storage_manager.hpp"
#include "terminal_ui.hpp"

#include <filesystem>
#include <optional>
#include <string>

class GitLiteApp {
public:
    GitLiteApp();

    void run();

private:
    StorageManager storage_;
    RepoService repoService_;
    TerminalUI ui_;
    std::optional<User> session_;
    std::filesystem::path currentDir_;

    void showLanding();
    void handleSignup();
    void handleLogin();
    void terminalMode();
    void dashboard();

    void createRepository();
    void showMyRepos();
    void browsePublicRepos();
    void showHelp();
    std::string getHelpCategories();
    std::string getHelpForCategory(const std::string &category);
    void updateSidebar();
    
    // Command handlers
    std::string handleInitCommand();
    std::string handleCreateCommand(const std::string &repoName);
    std::string handleListCommand();
    std::string handleLsUsersCommand();
    std::string handleLsReposCommand(const std::string &username);
    std::string handleStatusCommand(const std::optional<std::string> &repoOverride = std::nullopt);
    std::string handleAddCommand(const std::string &file, const std::optional<std::string> &repoOverride = std::nullopt);
    std::string handleCommitCommand(const std::string &message);
    std::string handleLogCommand(const std::optional<std::string> &repoOverride = std::nullopt);
    std::string handleBranchListCommand(const std::optional<std::string> &repoOverride = std::nullopt);
    std::string handleBranchCreateCommand(const std::string &branchName,
                                          const std::optional<std::string> &repoOverride = std::nullopt);
    std::string handleCheckoutCommand(const std::string &branch,
                                      const std::optional<std::string> &repoOverride = std::nullopt);
    std::string handleMergeCommand(const std::string &branch,
                                   const std::optional<std::string> &repoOverride = std::nullopt);
    std::string handleRebaseCommand(const std::string &branch,
                                    const std::optional<std::string> &repoOverride = std::nullopt);
    std::string handleRenameBranchCommand(const std::string &oldName,
                                          const std::string &newName,
                                          const std::optional<std::string> &repoOverride = std::nullopt);
    std::string handleDeleteBranchCommand(const std::string &branchName,
                                          const std::optional<std::string> &repoOverride = std::nullopt);
    
    // Collaboration commands
    std::string handlePermAddCommand(const std::string &repo, const std::string &user);
    std::string handlePermRmCommand(const std::string &repo, const std::string &user);
    std::string handlePermListCommand(const std::string &repo);
    std::string handleForkCommand(const std::string &userRepo);
    std::string handleTransferCommand(const std::string &repo, const std::string &newOwner);
    
    // Syncing commands
    std::string handlePushCommand();
    std::string handlePullCommand();
    std::string handleFetchCommand();
    std::string handleSyncCommand();
    std::string handleCloneCommand(const std::string &userRepo);
    
    // Repository management
    std::string handleDeleteCommand(const std::string &repo);
    std::string handleSetPublicCommand(const std::string &repo);
    std::string handleSetPrivateCommand(const std::string &repo);
    std::string handleVisibilityCommand(const std::optional<std::string> &repoOverride,
                                        std::optional<bool> newState);
    std::string handleViewCommand(const std::string &userRepo);
    
    // File operations
    std::string handleRmCommand(const std::string &file);
    std::string handleDiffCommand();
    std::string handleResetCommand(const std::string &file);
    std::string handleIgnoreCommand(const std::string &pattern);
    
    // Commit operations
    std::string handleShowCommand(const std::string &commitHash);
    std::string handleRevertCommand(const std::string &commitHash);
    
    // Tagging
    std::string handleTagCommand(const std::string &tagName);
    std::string handleTagCommand(const std::string &tagName,
                                 const std::optional<std::string> &repoOverride = std::nullopt);
    std::string handleTagsCommand(const std::optional<std::string> &repoOverride = std::nullopt);
    
    // Admin commands
    std::string handleMakeAdminCommand(const std::string &username);
    std::string handleRemoveAdminCommand(const std::string &username);
    std::string handleReposAllCommand();
    
    // Navigation commands
    std::string handleCdCommand(const std::string &path);
    std::string handlePwdCommand();
    std::string handleLsCommand();
    
    // Utility
    std::string handleVersionCommand();
    std::string handleConfigCommand(const std::vector<std::string> &args);
    
    void addMultiLineToTerminal(const std::string &text);
    std::filesystem::path getCurrentRepoPath();

    void manageRepository(const std::string &owner, const std::string &repo, bool isOwner);
    void showStatus(const std::filesystem::path &repoRoot);
    void addFileToRepo(const std::filesystem::path &repoRoot, bool canWrite);
    void commitRepo(const std::filesystem::path &repoRoot, bool canWrite);
    void branchMenu(const std::filesystem::path &repoRoot, bool canWrite);
    void checkoutBranch(const std::filesystem::path &repoRoot, bool canWrite);
    void pushRepo(const std::filesystem::path &repoRoot, const std::string &owner, const std::string &repo);
    void pullRepo(const std::filesystem::path &repoRoot, const std::string &owner, const std::string &repo, bool canWrite);
    void viewCommitHistory(const std::filesystem::path &repoRoot);
    void manageCollaborators(const std::string &owner, const std::string &repo, bool canManage);
    void toggleVisibility(const std::string &owner, const std::string &repo, bool canToggle);

    bool userExists(const std::string &username);
    bool hasWriteAccess(const std::string &owner, const std::string &repo);

    struct RepoContext {
        std::string owner;
        std::string name;
        std::filesystem::path root;
    };

    bool parseRepoIdentifier(const std::string &raw, std::string &owner, std::string &repo) const;
    bool isRepoIdentifier(const std::string &value) const;
    std::pair<std::string, std::string> readRepoIdentity(const std::filesystem::path &repoRoot) const;
    bool resolveRepoContext(const std::optional<std::string> &repoOverride,
                            bool requireWriteAccess,
                            RepoContext &ctx,
                            std::string &error);
};


