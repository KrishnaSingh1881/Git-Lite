#include "gitlite_app.hpp"

#include "hashing.hpp"
#include "utils.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ncurses.h>
#include <sstream>
#include <system_error>
#include <string>
#include <vector>
#include <optional>


using gitlite::util::isValidIdentifier;
using gitlite::util::split;
using gitlite::util::timestamp;
using gitlite::util::trim;

namespace fs = std::filesystem;

GitLiteApp::GitLiteApp()
    : storage_(),
      repoService_(storage_),
      currentDir_(fs::current_path()) {}

void GitLiteApp::run() {
    hashing::ensureSodium();
    showLanding();
}

void GitLiteApp::showLanding() {
    while (true) {
        clear();
        refresh();
        std::vector<std::string> menuItems = {"Sign Up", "Log In", "Exit"};
        int choice = ui_.menu("⚡ GitLite ⚡", menuItems);
        if (choice == 0) {
            handleSignup();
        } else if (choice == 1) {
            handleLogin();
        } else if (choice == 2 || choice == -1) {
            break;
        }
    }
}

void GitLiteApp::handleSignup() {
    auto username = ui_.prompt("Choose a username (3-32 chars):", false, 32);
    if (username.empty()) {
        return;
    }
    username = trim(username);
    if (username.size() < 3) {
        ui_.message("Signup Failed", {"Username too short."}, 3);
        return;
    }
    if (!isValidIdentifier(username)) {
        ui_.message("Signup Failed", {"Use only letters, digits, ., -, _."}, 3);
        return;
    }
    auto password = ui_.prompt("Choose a password (6+ chars):", true, 64);
    if (password.size() < 6) {
        ui_.message("Signup Failed", {"Password too short."}, 3);
        return;
    }

    auto users = storage_.loadUsers();
    if (std::any_of(users.begin(), users.end(), [&](const User &u) { return u.username == username; })) {
        ui_.message("Signup Failed", {"Username already exists."}, 3);
        return;
    }

    std::string hash;
    try {
        hash = hashing::hashPassword(password);
    } catch (const std::exception &ex) {
        ui_.message("Signup Failed", {ex.what()}, 3);
        return;
    }

    std::string role = users.empty() ? "admin" : "user";
    users.push_back({username, hash, role});
    storage_.saveUsers(users);
    storage_.ensureUserFolder(username);
    ui_.message("Signup Successful", {username + " created.", "Role: " + role});
}

void GitLiteApp::handleLogin() {
    auto username = ui_.prompt("Username:", false, 32);
    if (username.empty()) {
        return;
    }
    auto password = ui_.prompt("Password:", true, 64);
    auto users = storage_.loadUsers();
    auto it = std::find_if(users.begin(), users.end(), [&](const User &u) { return u.username == username; });
    if (it == users.end()) {
        ui_.message("Login Failed", {"Unknown username."}, 3);
        return;
    }
    if (!hashing::verifyPassword(it->passwordHash, password)) {
        ui_.message("Login Failed", {"Incorrect password."}, 3);
        return;
    }
    session_ = *it;
    ui_.message("Welcome", {"Login successful.", "Hello " + session_->username + "!"});
    terminalMode();
    session_.reset();
}

void GitLiteApp::terminalMode() {
    // Initialize split-screen
    ui_.initSplitScreen();
    ui_.addTerminalLine("GitLite Terminal - User: " + session_->username + " (" + session_->role + ")");
    ui_.addTerminalLine("Type 'help' for commands, 'menu' for dashboard, 'exit' to logout");
    ui_.addTerminalLine("");
    
    // Update sidebar with public repos
    updateSidebar();
    
    while (session_) {
        // Get command with current directory in prompt
        std::string prompt = "lite [" + currentDir_.string() + "]> ";
        std::string command = ui_.getTerminalCommand(prompt);
        command = trim(command);
        
        if (command.empty()) {
            continue;
        }
        
        // Parse and execute command
        std::vector<std::string> args = split(command, ' ');
        std::string cmd = args.empty() ? "" : args[0];
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
        
        std::string result;
        
        if (cmd == "menu") {
            ui_.addTerminalLine("Opening dashboard...");
            dashboard();
            updateSidebar();
        } else if (cmd == "help") {
            if (args.size() >= 2) {
                // help/<category> format
                std::string category = args[1];
                result = getHelpForCategory(category);
                addMultiLineToTerminal(result);
            } else if (command.find('/') != std::string::npos) {
                // help/repo format (single word with slash)
                size_t pos = command.find('/');
                std::string category = command.substr(pos + 1);
                result = getHelpForCategory(category);
                addMultiLineToTerminal(result);
            } else {
                // Just "help" - show categories
                result = getHelpCategories();
                addMultiLineToTerminal(result);
            }
        } else if (cmd == "logout" || cmd == "exit" || cmd == "quit") {
            ui_.addTerminalLine("Logging out...");
            break;
        } else if (cmd == "whoami") {
            result = "User: " + session_->username + " (Role: " + session_->role + ")";
            ui_.addTerminalLine(result);
        } else if (cmd == "clear") {
            ui_.clearTerminal();
            ui_.addTerminalLine("Terminal cleared.");
        } else if (cmd == "init") {
            result = handleInitCommand();
            ui_.addTerminalLine(result);
        } else if (cmd == "create") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: create <repo-name>");
            } else {
                result = handleCreateCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "list") {
            result = handleListCommand();
            addMultiLineToTerminal(result);
        } else if (cmd == "ls-users") {
            result = handleLsUsersCommand();
            addMultiLineToTerminal(result);
        } else if (cmd == "ls-repos") {
            std::string username = args.size() >= 2 ? args[1] : session_->username;
            result = handleLsReposCommand(username);
            addMultiLineToTerminal(result);
        } else if (cmd == "status") {
            std::optional<std::string> repoOverride;
            if (args.size() >= 2) {
                repoOverride = args[1];
            }
            result = handleStatusCommand(repoOverride);
            addMultiLineToTerminal(result);
        } else if (cmd == "add") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: add <file> [repo]");
            } else {
                std::optional<std::string> repoOverride;
                if (args.size() >= 3) {
                    repoOverride = args[2];
                }
                result = handleAddCommand(args[1], repoOverride);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "commit") {
            std::string message;
            if (args.size() >= 3 && args[1] == "-m") {
                message = args[2];
                for (size_t i = 3; i < args.size(); i++) {
                    message += " " + args[i];
                }
            } else {
                message = ui_.prompt("Commit message:", false, 128);
            }
            if (!message.empty()) {
                result = handleCommitCommand(message);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "log") {
            std::optional<std::string> repoOverride;
            if (args.size() >= 2) {
                repoOverride = args[1];
            }
            result = handleLogCommand(repoOverride);
            addMultiLineToTerminal(result);
        } else if (cmd == "branch") {
            if (args.size() == 1) {
                result = handleBranchListCommand();
                addMultiLineToTerminal(result);
            } else {
                std::string subcommand = args[1];
                if (subcommand == "list") {
                    std::optional<std::string> repoOverride;
                    if (args.size() >= 3) {
                        repoOverride = args[2];
                    }
                    result = handleBranchListCommand(repoOverride);
                    addMultiLineToTerminal(result);
                } else if (subcommand.find('/') != std::string::npos || isRepoIdentifier(subcommand)) {
                    result = handleBranchListCommand(subcommand);
                    addMultiLineToTerminal(result);
                } else {
                    std::optional<std::string> repoOverride;
                    if (args.size() >= 3) {
                        repoOverride = args[2];
                    }
                    result = handleBranchCreateCommand(subcommand, repoOverride);
                    ui_.addTerminalLine(result);
                }
            }
        } else if (cmd == "checkout") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: checkout <branch>");
            } else {
                std::optional<std::string> repoOverride;
                if (args.size() >= 3) {
                    repoOverride = args[2];
                }
                result = handleCheckoutCommand(args[1], repoOverride);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "merge") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: merge <branch>");
            } else {
                std::optional<std::string> repoOverride;
                if (args.size() >= 3) {
                    repoOverride = args[2];
                }
                result = handleMergeCommand(args[1], repoOverride);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "rebase") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: rebase <branch>");
            } else {
                std::optional<std::string> repoOverride;
                if (args.size() >= 3) {
                    repoOverride = args[2];
                }
                result = handleRebaseCommand(args[1], repoOverride);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "rename-branch") {
            if (args.size() < 3) {
                ui_.addTerminalLine("Error: Usage: rename-branch <old> <new>");
            } else {
                std::optional<std::string> repoOverride;
                if (args.size() >= 4) {
                    repoOverride = args[3];
                }
                result = handleRenameBranchCommand(args[1], args[2], repoOverride);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "delete-branch") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: delete-branch <name>");
            } else {
                std::optional<std::string> repoOverride;
                if (args.size() >= 3) {
                    repoOverride = args[2];
                }
                result = handleDeleteBranchCommand(args[1], repoOverride);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "perm") {
            if (args.size() >= 3 && args[1] == "add") {
                result = handlePermAddCommand(args[2], args[3]);
                ui_.addTerminalLine(result);
            } else if (args.size() >= 3 && args[1] == "rm") {
                result = handlePermRmCommand(args[2], args[3]);
                ui_.addTerminalLine(result);
            } else if (args.size() >= 2 && args[1] == "list") {
                result = handlePermListCommand(args[2]);
                addMultiLineToTerminal(result);
            } else {
                ui_.addTerminalLine("Error: Usage: perm add|rm|list <repo> [user]");
            }
        } else if (cmd == "fork") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: fork <user>/<repo>");
            } else {
                result = handleForkCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "transfer") {
            if (args.size() < 3) {
                ui_.addTerminalLine("Error: Usage: transfer <repo> <new-owner>");
            } else {
                result = handleTransferCommand(args[1], args[2]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "push") {
            result = handlePushCommand();
            ui_.addTerminalLine(result);
        } else if (cmd == "pull") {
            result = handlePullCommand();
            ui_.addTerminalLine(result);
        } else if (cmd == "fetch") {
            result = handleFetchCommand();
            ui_.addTerminalLine(result);
        } else if (cmd == "sync") {
            result = handleSyncCommand();
            ui_.addTerminalLine(result);
        } else if (cmd == "clone") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: clone <user>/<repo>");
            } else {
                result = handleCloneCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "delete") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: delete <repo>");
            } else {
                result = handleDeleteCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "set-public") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: set-public <repo>");
            } else {
                result = handleSetPublicCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "set-private") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: set-private <repo>");
            } else {
                result = handleSetPrivateCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "visibility") {
            auto toLower = [](std::string value) {
                std::transform(value.begin(), value.end(), value.begin(), ::tolower);
                return value;
            };
            std::optional<std::string> repoOverride;
            std::optional<bool> newState;
            bool firstArgIsState = false;

            if (args.size() >= 2) {
                std::string first = toLower(args[1]);
                if (first == "public") {
                    newState = true;
                    firstArgIsState = true;
                } else if (first == "private") {
                    newState = false;
                    firstArgIsState = true;
                } else {
                    repoOverride = args[1];
                }
            }

            if (args.size() >= 3) {
                if (firstArgIsState) {
                    repoOverride = args[2];
                } else {
                    std::string second = toLower(args[2]);
                    if (second == "public") {
                        newState = true;
                    } else if (second == "private") {
                        newState = false;
                    } else {
                        ui_.addTerminalLine("Error: Usage: visibility [repo] [public|private]");
                        continue;
                    }
                }
            }

            if (args.size() >= 4) {
                ui_.addTerminalLine("Error: Usage: visibility [repo] [public|private]");
                continue;
            }

            result = handleVisibilityCommand(repoOverride, newState);
            ui_.addTerminalLine(result);
        } else if (cmd == "view") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: view <user>/<repo>");
            } else {
                result = handleViewCommand(args[1]);
                addMultiLineToTerminal(result);
            }
        } else if (cmd == "rm") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: rm <file>");
            } else {
                result = handleRmCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "diff") {
            result = handleDiffCommand();
            addMultiLineToTerminal(result);
        } else if (cmd == "reset") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: reset <file>");
            } else {
                result = handleResetCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "ignore") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: ignore <pattern>");
            } else {
                result = handleIgnoreCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "show") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: show <commit-hash>");
            } else {
                result = handleShowCommand(args[1]);
                addMultiLineToTerminal(result);
            }
        } else if (cmd == "revert") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: revert <commit-hash>");
            } else {
                result = handleRevertCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "tag") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: tag <name>");
            } else {
                std::optional<std::string> repoOverride;
                if (args.size() >= 3) {
                    repoOverride = args[2];
                }
                result = handleTagCommand(args[1], repoOverride);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "tags") {
            std::optional<std::string> repoOverride;
            if (args.size() >= 2) {
                repoOverride = args[1];
            }
            result = handleTagsCommand(repoOverride);
            addMultiLineToTerminal(result);
        } else if (cmd == "make-admin") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: make-admin <user>");
            } else {
                result = handleMakeAdminCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "remove-admin") {
            if (args.size() < 2) {
                ui_.addTerminalLine("Error: Usage: remove-admin <user>");
            } else {
                result = handleRemoveAdminCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "repos") {
            if (args.size() >= 1 && args[0] == "all") {
                result = handleReposAllCommand();
                addMultiLineToTerminal(result);
            } else {
                ui_.addTerminalLine("Error: Usage: repos all");
            }
        } else if (cmd == "version") {
            result = handleVersionCommand();
            ui_.addTerminalLine(result);
        } else if (cmd == "config") {
            result = handleConfigCommand(args);
            ui_.addTerminalLine(result);
        } else if (cmd == "cd") {
            if (args.size() < 2) {
                // cd without args goes to home (workspace root)
                currentDir_ = storage_.root().parent_path();
                ui_.addTerminalLine("Changed to: " + currentDir_.string());
            } else {
                result = handleCdCommand(args[1]);
                ui_.addTerminalLine(result);
            }
        } else if (cmd == "pwd") {
            result = handlePwdCommand();
            ui_.addTerminalLine(result);
        } else if (cmd == "ls" || cmd == "dir") {
            result = handleLsCommand();
            addMultiLineToTerminal(result);
        } else {
            result = "Unknown command: " + cmd + ". Type 'help' for available commands.";
            ui_.addTerminalLine(result);
        }
        
        updateSidebar();
    }
}

void GitLiteApp::updateSidebar() {
    std::vector<std::string> sidebarContent;

    if (!session_) {
        sidebarContent.push_back("Not logged in.");
        ui_.drawSidebar(sidebarContent, "Repositories");
        return;
    }

    auto repos = storage_.listUserRepos(session_->username);
    sidebarContent.push_back("My repositories:");

    if (repos.empty()) {
        sidebarContent.push_back("  (none)");
    } else {
        std::error_code ec;
        auto currentCanonical = fs::weakly_canonical(currentDir_, ec);
        for (size_t i = 0; i < repos.size() && i < 15; ++i) {
            const auto &name = repos[i];
            fs::path repoRoot = storage_.repoPath(session_->username, name);
            std::string visibility = storage_.getVisibility(session_->username, name);

            std::string prefix = "  ";
            std::error_code repoEc;
            auto repoCanonical = fs::weakly_canonical(repoRoot, repoEc);
            if (!ec && !repoEc) {
                fs::path workspacePath = repoRoot / "workspace";
                std::error_code wsEc;
                auto workspaceCanonical = fs::weakly_canonical(workspacePath, wsEc);
                if (currentCanonical == repoCanonical ||
                    (!wsEc && currentCanonical == workspaceCanonical)) {
                    prefix = "> ";
                }
            }

            sidebarContent.push_back(prefix + name + " [" + visibility + "]");
        }
        if (repos.size() > 15) {
            sidebarContent.push_back("  ...");
        }
    }

    sidebarContent.push_back("");
    sidebarContent.push_back("Tips:");
    sidebarContent.push_back("  create <name>");
    sidebarContent.push_back("  cd " + (storage_.root() / session_->username).string());

    ui_.drawSidebar(sidebarContent, session_->username + "'s Repos");
}

std::string GitLiteApp::getHelpCategories() {
    std::string result = "GitLite Help - Available Categories:\n";
    result += "  Use 'help/<category>' to see commands for that category\n\n";
    result += "  * Commands marked with an asterisk must be run inside the target repository folder\n\n";
    result += "Categories:\n";
    result += "  1. auth       - User & Authentication\n";
    result += "  2. repo       - Repository Management\n";
    result += "  3. files      - File Tracking\n";
    result += "  4. commit     - Commit System\n";
    result += "  5. branch     - Branching & Merging\n";
    result += "  6. sync       - Syncing & Collaboration\n";
    result += "  7. collab     - Collaboration & Permissions\n";
    result += "  8. admin      - Admin & Role Management\n";
    result += "  9. utility    - UI & Utility Commands\n\n";
    result += "Quick Start:\n";
    result += "  1. cd <path>               - Navigate to folder\n";
    result += "  2. ls                      - List files in current directory\n";
    result += "  3. init                    - Initialize repo in current directory\n";
    result += "  4. add <file>              - Stage files (from current dir)\n";
    result += "  5. commit -m \"message\"     - Commit changes\n";
    result += "  6. branch <name>           - Create branch\n";
    result += "  7. push                    - Push to remote\n\n";
    result += "Example: help/repo  (to see repository commands)";
    return result;
}

std::string GitLiteApp::getHelpForCategory(const std::string &category) {
    std::string cat = category;
    std::transform(cat.begin(), cat.end(), cat.begin(), ::tolower);
    
    if (cat == "auth" || cat == "1") {
        return "User & Authentication Commands:\n"
               "  logout              - End current user session\n"
               "  whoami              - Display current user and role\n"
               "  ls-users            - List all users\n"
               "  (Note: signup/login done from main menu)";
    } else if (cat == "repo" || cat == "2") {
        return "Repository Management Commands:\n"
               "  init                - Initialize repository in current folder\n"
               "  create <repo>       - Create new repository\n"
               "  clone <user>/<repo> - Clone existing repository\n"
               "  delete <repo>       - Delete repository\n"
               "  set-public <repo>   - Mark repo as public\n"
               "  set-private <repo>  - Make repo private\n"
               "  visibility [repo] [public|private] - Toggle or set repo visibility\n"
               "  list                - List all your repositories\n"
               "  ls-repos <user>     - Show user's repositories\n"
               "  view <user>/<repo>  - View repository contents\n\n"
               "Workflow:\n"
               "  1. cd <path> - Navigate to your project folder\n"
               "  2. ls - List files in directory\n"
               "  3. init - Create .glite repository\n"
               "  4. add <file> - Stage files from current directory\n"
               "  5. commit -m \"message\" - Commit changes";
    } else if (cat == "files" || cat == "3") {
        return "File Tracking Commands:\n"
               "  add <file> [repo]   - Stage file for commit (optionally target repo)\n"
               "  status [repo]       - Show staged files\n"
               "  rm <file>*          - Remove file from staging and workspace\n"
               "  diff*               - Show changes since last commit\n"
               "  reset <file>*       - Unstage a file\n"
               "  ignore <pattern>*   - Add pattern to .gliteignore\n\n"
               "Examples:\n"
               "  add workspace/main.cpp\n"
               "  status tejas/assignments";
    } else if (cat == "commit" || cat == "4") {
        return "Commit System Commands:\n"
               "  commit -m \"message\"* - Create commit with message\n"
               "  log [repo]            - Show commit history\n"
               "  show <commit-hash>*   - Show commit details\n"
               "  revert <commit-hash>* - Undo a commit\n"
               "  tag <name> [repo]     - Tag current commit\n"
               "  tags [repo]           - List all tags\n\n"
               "Examples:\n"
               "  commit -m \"Initial commit\"\n"
               "  log tejas/assignments";
    } else if (cat == "branch" || cat == "5") {
        return "Branching & Merging Commands:\n"
               "  branch [repo]             - List all branches\n"
               "  branch list [repo]        - Alias for listing branches\n"
               "  branch <name> [repo]      - Create new branch\n"
               "  checkout <branch> [repo]  - Switch to branch\n"
               "  merge <branch> [repo]     - Merge branch into current\n"
               "  rebase <branch> [repo]    - Rebase current branch onto another\n"
               "  rename-branch <old> <new> [repo] - Rename branch\n"
               "  delete-branch <name> [repo]- Delete branch\n\n"
               "Examples:\n"
               "  branch feature-x\n"
               "  branch list tejas/assignments\n"
               "  checkout main tejas/assignments";
    } else if (cat == "sync" || cat == "6") {
        return "Syncing Commands:\n"
               "  push*               - Push commits to remote mirror\n"
               "  pull*               - Pull from remote mirror\n"
               "  fetch*              - Fetch remote branches\n"
               "  sync*               - Fetch + merge automatically\n"
               "  clone <user>/<repo> - Clone repository to current directory\n\n"
               "Note: Remotes are stored in storage/_remotes/";
    } else if (cat == "collab" || cat == "7") {
        return "Collaboration & Permissions Commands:\n"
               "  perm add <repo> <user>    - Grant collaborator access\n"
               "  perm rm <repo> <user>     - Revoke collaborator access\n"
               "  perm list <repo>          - List all collaborators\n"
               "  transfer <repo> <new-owner> - Transfer repository ownership\n"
               "  fork <user>/<repo>        - Fork repository to your account\n\n"
               "Example:\n"
               "  perm add myrepo alice\n"
               "  perm list myrepo";
    } else if (cat == "admin" || cat == "8") {
        return "Admin Commands (Admin only):\n"
               "  make-admin <user>   - Promote user to admin\n"
               "  remove-admin <user> - Demote admin to user\n"
               "  repos all           - List all repositories\n\n"
               "Note: Only admins can use these commands";
    } else if (cat == "utility" || cat == "9") {
        return "UI & Utility Commands:\n"
               "  menu                - Show dashboard menu\n"
               "  help                - Show help categories\n"
               "  help/<category>     - Show commands for category\n"
               "  clear               - Clear terminal\n"
               "  version             - Show version\n"
               "  config set <key> <value> - Set configuration\n"
               "  config get <key>    - Get configuration\n"
               "  config list         - List all configurations\n\n"
               "Navigation Commands:\n"
               "  cd <path>           - Change directory\n"
               "  cd ..               - Go to parent directory\n"
               "  cd ~                - Go to home directory\n"
               "  pwd                 - Show current directory\n"
               "  ls / dir            - List directory contents";
    } else {
        return "Unknown category: " + category + "\n"
               "Use 'help' to see available categories";
    }
}

void GitLiteApp::dashboard() {
    while (session_) {
        clear();
        refresh();
        std::string header = "Welcome, " + session_->username + "! (" + session_->role + ")";
        std::vector<std::string> options = {
            "Create Repository",
            "View My Repos",
            "Browse Public Repos",
            "Help",
            "Logout"};
        int choice = ui_.menu(header, options, "↑↓ Navigate | ↵ Select | Q Back");
        if (choice == 0) {
            createRepository();
        } else if (choice == 1) {
            showMyRepos();
        } else if (choice == 2) {
            browsePublicRepos();
        } else if (choice == 3) {
            showHelp();
        } else if (choice == 4 || choice == -1) {
            ui_.message("Logout", {"Signed out."});
            break;
        }
    }
}

void GitLiteApp::createRepository() {
    auto name = ui_.prompt("Repository name (letters/digits/._-):", false, 48);
    if (name.empty()) {
        return;
    }
    if (!isValidIdentifier(name)) {
        ui_.message("Create Repository", {"Invalid repository name."}, 3);
        return;
    }
    std::string error;
    if (storage_.createRepo(session_->username, name, error)) {
        fs::path repoRoot = storage_.repoPath(session_->username, name);
        currentDir_ = repoRoot;
        ui_.message("Create Repository",
                    {"Repository created.",
                     "Location: " + repoRoot.string(),
                     "Terminal directory set to repo root.",
                     "Add project files under workspace/."});
    } else {
        ui_.message("Create Repository", {error}, 3);
    }
}

void GitLiteApp::showMyRepos() {
    auto repos = storage_.listUserRepos(session_->username);
    if (repos.empty()) {
        ui_.message("My Repos", {"No repositories yet. Create one first!"});
        return;
    }
    std::vector<std::string> items;
    for (const auto &name : repos) {
        std::string visibility = storage_.getVisibility(session_->username, name);
        items.push_back(name + " [" + visibility + "]");
    }
    int choice = ui_.list("My Repositories", items);
    if (choice >= 0 && choice < static_cast<int>(repos.size())) {
        manageRepository(session_->username, repos[choice], true);
    }
}

void GitLiteApp::browsePublicRepos() {
    auto all = storage_.listAllRepos();
    std::vector<std::pair<std::string, std::string>> publicRepos;
    for (const auto &pair : all) {
        if (repoService_.isPublic(pair.first, pair.second)) {
            publicRepos.push_back(pair);
        }
    }
    if (publicRepos.empty()) {
        ui_.message("Public Repos", {"No public repositories available."});
        return;
    }
    std::vector<std::string> options;
    for (const auto &entry : publicRepos) {
        options.push_back(entry.first + "/" + entry.second);
    }
    int choice = ui_.list("Public Repositories", options, "↑↓ Navigate | ↵ Select | Q Back");
    if (choice >= 0 && choice < static_cast<int>(publicRepos.size())) {
        bool owner = publicRepos[choice].first == session_->username;
        manageRepository(publicRepos[choice].first, publicRepos[choice].second, owner);
    }
}

void GitLiteApp::showHelp() {
    std::vector<std::string> lines = {
        "================================================",
        "         GitLite Quick Start Guide             ",
        "================================================",
        "",
        "NAVIGATION:",
        "  - Arrow Keys (Up/Down) or Mouse: Navigate menus",
        "  - Enter or Mouse Click: Select option",
        "  - Q or ESC: Go back/Exit",
        "",
        "GETTING STARTED:",
        "  1. Sign Up: Create your account (first user = admin)",
        "  2. Log In: Access your dashboard",
        "  3. Create Repository: Start a new project",
        "",
        "REPOSITORY WORKFLOW:",
        "  - Add File: Stage files from workspace/ folder",
        "  - Commit: Save changes with a message",
        "  - Status: View what's staged for commit",
        "  - Branches: Create/switch between branches",
        "  - Checkout: Change active branch",
        "  - Push: Sync to local remote mirror",
        "  - Pull: Update from remote mirror",
        "  - Commit History: View commit timeline",
        "",
        "COLLABORATION:",
        "  - Repos are private by default",
        "  - Owners/Admins: Can add collaborators",
        "  - Collaborators: Can push, pull, commit",
        "  - Public Repos: Visible to all users",
        "",
        "STORAGE STRUCTURE:",
        "  storage/",
        "    - users.tsv (user accounts)",
        "    - permissions.tsv (collaborators)",
        "    - <username>/",
        "        - <repo>/",
        "            - .glite/ (repo metadata)",
        "            - workspace/ (your files)",
        "",
        "TIPS:",
        "  - Place files in workspace/ before adding",
        "  - Commit messages describe your changes",
        "  - Branches let you work on features separately",
        "  - Push/Pull syncs between local copies",
        "",
        "Press any key to continue..."
    };
    ui_.message("Help - Quick Guide", lines);
}

void GitLiteApp::manageRepository(const std::string &owner, const std::string &repo, bool isOwner) {
    while (session_) {
        fs::path repoRoot = storage_.repoPath(owner, repo);
        std::string visibility = storage_.getVisibility(owner, repo);
        std::string header = owner + "/" + repo + " [" + visibility + "]";
        bool canWrite = hasWriteAccess(owner, repo);
        bool canManage = isOwner || (session_->role == "admin");
        bool canToggle = canManage;
        bool canCollaborators = canManage;

        std::vector<std::string> options = {
            "Status",
            "Add File",
            "Commit",
            "Branches",
            "Checkout",
            "Push",
            "Pull",
            "Commit History",
            "Manage Collaborators",
            "Set Visibility",
            "Back"};
        int choice = ui_.menu(header, options);
        if (choice == -1 || choice == 10) {
            break;
        }
        switch (choice) {
        case 0:
            showStatus(repoRoot);
            break;
        case 1:
            addFileToRepo(repoRoot, canWrite);
            break;
        case 2:
            commitRepo(repoRoot, canWrite);
            break;
        case 3:
            branchMenu(repoRoot, canWrite);
            break;
        case 4:
            checkoutBranch(repoRoot, canWrite);
            break;
        case 5:
            pushRepo(repoRoot, owner, repo);
            break;
        case 6:
            pullRepo(repoRoot, owner, repo, canWrite);
            break;
        case 7:
            viewCommitHistory(repoRoot);
            break;
        case 8:
            manageCollaborators(owner, repo, canCollaborators);
            break;
        case 9:
            toggleVisibility(owner, repo, canToggle);
            break;
        default:
            break;
        }
    }
}

void GitLiteApp::showStatus(const fs::path &repoRoot) {
    auto entries = repoService_.readIndex(repoRoot);
    if (entries.empty()) {
        ui_.message("Status", {"Index empty. No staged files."});
        return;
    }
    std::vector<std::string> lines = {"Staged files:"};
    for (const auto &entry : entries) {
        lines.push_back("  " + entry.first + " -> " + entry.second.substr(0, 12) + "...");
    }
    ui_.message("Status", lines);
}

void GitLiteApp::addFileToRepo(const fs::path &repoRoot, bool canWrite) {
    if (!canWrite) {
        ui_.message("Add File", {"Read-only access. Request collaborator rights."});
        return;
    }
    auto path = ui_.prompt("Relative path under workspace/:", false, 96);
    if (path.empty()) {
        return;
    }
    std::string message;
    if (repoService_.addFile(repoRoot, path, message)) {
        ui_.message("Add File", {message}, message.find("Error") == 0 ? 3 : 0);
    } else {
        ui_.message("Add File", {message}, message.find("Error") == 0 ? 3 : 0);
    }
}

void GitLiteApp::commitRepo(const fs::path &repoRoot, bool canWrite) {
    if (!canWrite) {
        ui_.message("Commit", {"Read-only access. Request collaborator rights."});
        return;
    }
    auto msg = ui_.prompt("Commit message:", false, 96);
    if (msg.empty()) {
        return;
    }
    CommitRecord record;
    std::string err;
    if (repoService_.commit(repoRoot, session_->username, msg, record, err)) {
        ui_.message("Commit", {"Commit " + record.id.substr(0, 12) + "... recorded on " + record.branch + "."});
    } else {
        ui_.message("Commit Failed", {err}, 3);
    }
}

void GitLiteApp::branchMenu(const fs::path &repoRoot, bool canWrite) {
    auto branches = repoService_.listBranchesWithHead(repoRoot);
    std::string current = repoService_.currentBranch(repoRoot);
    std::vector<std::string> options;
    for (const auto &entry : branches) {
        std::string head = entry.second.empty() ? "—" : entry.second.substr(0, 12) + "...";
        std::string marker = (entry.first == current) ? " *" : "";
        options.push_back(entry.first + marker + "  (HEAD: " + head + ")");
    }
    if (canWrite) {
        options.push_back("[+] Create new branch");
    }
    if (options.empty()) {
        ui_.message("Branches", {"No branches found. Make a commit first."});
        return;
    }
    int choice = ui_.menu("Branches", options);
    if (choice == -1) {
        return;
    }
    if (canWrite && choice == static_cast<int>(options.size()) - 1) {
        auto name = ui_.prompt("New branch name:", false, 32);
        if (name.empty()) {
            return;
        }
        if (!isValidIdentifier(name)) {
            ui_.message("Branch", {"Invalid branch name."});
            return;
        }
        std::string err;
        if (repoService_.createBranch(repoRoot, name, err)) {
            ui_.message("Branch", {"Branch created: " + name});
        } else {
            ui_.message("Branch", {err}, 3);
        }
    }
}

void GitLiteApp::checkoutBranch(const fs::path &repoRoot, bool canWrite) {
    if (!canWrite) {
        ui_.message("Checkout", {"Read-only access. Request collaborator rights."});
        return;
    }
    auto branchPairs = repoService_.listBranchesWithHead(repoRoot);
    if (branchPairs.empty()) {
        ui_.message("Checkout", {"No branches available."});
        return;
    }
    std::vector<std::string> names;
    for (const auto &pair : branchPairs) {
        names.push_back(pair.first);
    }
    int choice = ui_.list("Checkout Branch", names);
    if (choice >= 0 && choice < static_cast<int>(names.size())) {
        repoService_.setCurrentBranch(repoRoot, names[choice]);
        ui_.message("Checkout", {"Switched to branch " + names[choice]});
    }
}

void GitLiteApp::pushRepo(const fs::path &repoRoot, const std::string &owner, const std::string &repo) {
    if (!hasWriteAccess(owner, repo)) {
        ui_.message("Push", {"You do not have permission to push."});
        return;
    }
    fs::path remoteRoot = storage_.root() / "_remotes" / owner / repo;
    std::string err;
    if (repoService_.push(repoRoot, remoteRoot, err)) {
        ui_.message("Push", {"Remote mirror updated."});
    } else {
        ui_.message("Push", {err}, 3);
    }
}

void GitLiteApp::pullRepo(const fs::path &repoRoot,
                          const std::string &owner,
                          const std::string &repo,
                          bool canWrite) {
    if (!canWrite && !repoService_.isPublic(owner, repo)) {
        ui_.message("Pull", {"You do not have permission to pull this repository."});
        return;
    }
    fs::path remoteRoot = storage_.root() / "_remotes" / owner / repo;
    std::string err;
    if (repoService_.pull(repoRoot, remoteRoot, err)) {
        ui_.message("Pull", {"Repository refreshed from mirror."});
    } else {
        ui_.message("Pull", {err}, 3);
    }
}

void GitLiteApp::viewCommitHistory(const fs::path &repoRoot) {
    auto branchPairs = repoService_.listBranchesWithHead(repoRoot);
    if (branchPairs.empty()) {
        ui_.message("Commit History", {"No branches available."});
        return;
    }
    std::vector<std::string> names;
    for (const auto &pair : branchPairs) {
        names.push_back(pair.first);
    }
    int branchChoice = ui_.list("Select Branch", names);
    if (branchChoice < 0 || branchChoice >= static_cast<int>(names.size())) {
        return;
    }
    auto records = repoService_.history(repoRoot, names[branchChoice], 20);
    if (records.empty()) {
        ui_.message("Commit History", {"No commits recorded yet."});
        return;
    }
    std::vector<std::string> lines;
    lines.push_back("Branch: " + names[branchChoice]);
    for (const auto &record : records) {
        lines.push_back(record.id.substr(0, 10) + " | " + record.timestamp + " | " + record.author);
        lines.push_back("  " + record.message);
    }
    ui_.message("Commit History", lines);
}

void GitLiteApp::manageCollaborators(const std::string &owner, const std::string &repo, bool canManage) {
    if (!canManage) {
        ui_.message("Collaborators", {"Only owners or admins can manage collaborators."});
        return;
    }
    auto perms = storage_.loadPermissions();
    std::string key = owner + "/" + repo;
    auto &collabs = perms[key];
    while (true) {
        std::vector<std::string> options;
        options.push_back("[+] Add collaborator");
        for (const auto &name : collabs) {
            options.push_back("[-] " + name);
        }
        options.push_back("Back");
        int choice = ui_.menu("Collaborators", options);
        if (choice == -1 || choice == static_cast<int>(options.size()) - 1) {
            break;
        }
        if (choice == 0) {
            auto username = ui_.prompt("Collaborator username:", false, 32);
            if (username.empty()) {
                continue;
            }
            if (!userExists(username)) {
                ui_.message("Collaborators", {"User not found."}, 3);
                continue;
            }
            if (username == owner) {
                ui_.message("Collaborators", {"Owner already has access."}, 3);
                continue;
            }
            collabs.insert(username);
            storage_.savePermissions(perms);
            ui_.message("Collaborators", {username + " added."});
        } else if (choice > 0 && choice < static_cast<int>(options.size()) - 1) {
            std::string username = options[choice].substr(4);
            if (ui_.confirm("Remove " + username + " from collaborators?")) {
                collabs.erase(username);
                storage_.savePermissions(perms);
                ui_.message("Collaborators", {username + " removed."});
            }
        }
    }
}

void GitLiteApp::toggleVisibility(const std::string &owner, const std::string &repo, bool canToggle) {
    if (!canToggle) {
        ui_.message("Visibility", {"Only owners or admins can change visibility."});
        return;
    }
    std::string current = storage_.getVisibility(owner, repo);
    bool newState = current != "public";
    storage_.setVisibility(owner, repo, newState);
    ui_.message("Visibility", {std::string("Repo visibility now ") + (newState ? "public" : "private") + "."});
}

bool GitLiteApp::userExists(const std::string &username) {
    auto users = storage_.loadUsers();
    return std::any_of(users.begin(), users.end(), [&](const User &u) { return u.username == username; });
}

bool GitLiteApp::hasWriteAccess(const std::string &owner, const std::string &repo) {
    if (!session_) {
        return false;
    }
    if (session_->role == "admin") {
        return true;
    }
    if (session_->username == owner) {
        return true;
    }
    auto perms = storage_.loadPermissions();
    std::string key = owner + "/" + repo;
    auto it = perms.find(key);
    if (it == perms.end()) {
        return false;
    }
    return it->second.count(session_->username) > 0;
}

bool GitLiteApp::parseRepoIdentifier(const std::string &raw,
                                     std::string &owner,
                                     std::string &repo) const {
    std::string value = trim(raw);
    if (value.empty()) {
        return false;
    }
    auto pos = value.find('/');
    if (pos == std::string::npos) {
        repo = value;
        return true;
    }
    std::string left = value.substr(0, pos);
    std::string right = value.substr(pos + 1);
    if (left.empty() || right.empty()) {
        return false;
    }
    owner = left;
    repo = right;
    return true;
}

bool GitLiteApp::isRepoIdentifier(const std::string &value) const {
    if (!session_) {
        return false;
    }
    std::string owner = session_->username;
    std::string repo;
    if (!parseRepoIdentifier(value, owner, repo)) {
        return false;
    }
    return storage_.repoExists(owner, repo);
}

std::pair<std::string, std::string> GitLiteApp::readRepoIdentity(const fs::path &repoRoot) const {
    std::pair<std::string, std::string> identity;
    std::ifstream cfg(repoRoot / ".glite" / "config");
    if (!cfg) {
        return identity;
    }
    std::string line;
    while (std::getline(cfg, line)) {
        if (line.empty()) {
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        if (key == "owner") {
            identity.first = value;
        } else if (key == "name") {
            identity.second = value;
        }
    }
    return identity;
}

bool GitLiteApp::resolveRepoContext(const std::optional<std::string> &repoOverride,
                                    bool requireWriteAccess,
                                    RepoContext &ctx,
                                    std::string &error) {
    if (!session_) {
        error = "Error: Not logged in.";
        return false;
    }

    std::string owner = session_->username;
    std::string repoName;
    fs::path repoRoot;
    bool trackedRepo = false;

    if (repoOverride) {
        std::string overrideOwner = owner;
        std::string overrideRepo;
        if (!parseRepoIdentifier(*repoOverride, overrideOwner, overrideRepo)) {
            error = "Error: Invalid repository identifier.";
            return false;
        }
        if (!storage_.repoExists(overrideOwner, overrideRepo)) {
            error = "Error: Repository '" + overrideOwner + "/" + overrideRepo + "' not found.";
            return false;
        }
        owner = overrideOwner;
        repoName = overrideRepo;
        repoRoot = storage_.repoPath(owner, repoName);
        trackedRepo = true;
    } else {
        fs::path glitePath = currentDir_ / ".glite";
        if (!fs::exists(glitePath)) {
            error = "Error: Not a GitLite repository. Run 'init' first or specify repository.";
            return false;
        }
        repoRoot = glitePath.parent_path();
        auto identity = readRepoIdentity(repoRoot);
        if (!identity.first.empty()) {
            owner = identity.first;
        }
        if (!identity.second.empty()) {
            repoName = identity.second;
        }
        if (repoName.empty()) {
            repoName = repoRoot.filename().string();
        }
        trackedRepo = storage_.repoExists(owner, repoName);
    }

    if (trackedRepo) {
        bool canWrite = hasWriteAccess(owner, repoName);
        bool canRead = canWrite || repoService_.isPublic(owner, repoName);
        if (requireWriteAccess && !canWrite) {
            error = "Error: You don't have permission to modify '" + owner + "/" + repoName + "'.";
            return false;
        }
        if (!requireWriteAccess && !canRead) {
            error = "Error: Repository '" + owner + "/" + repoName + "' is private.";
            return false;
        }
    }

    ctx = {owner, repoName, repoRoot};
    return true;
}

std::string GitLiteApp::handleInitCommand() {
    fs::path glitePath = currentDir_ / ".glite";
    
    if (fs::exists(glitePath)) {
        return "Error: Repository already initialized in this directory.";
    }
    
    try {
        fs::create_directories(glitePath / "objects");
        fs::create_directories(glitePath / "refs" / "heads");
        fs::create_directories(currentDir_ / "workspace");
        
        std::ofstream headFile(glitePath / "HEAD");
        headFile << "ref: main\n";
        headFile.close();
        
        std::ofstream mainRef(glitePath / "refs" / "heads" / "main");
        mainRef.close();
        
        std::ofstream indexFile(glitePath / "index");
        indexFile.close();
        
        return "Initialized empty GitLite repository in " + currentDir_.string();
    } catch (const std::exception &ex) {
        return "Error: " + std::string(ex.what());
    }
}

std::string GitLiteApp::handleCreateCommand(const std::string &repoName) {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    if (!isValidIdentifier(repoName)) {
        return "Error: Invalid repository name.";
    }
    
    std::string error;
    if (storage_.createRepo(session_->username, repoName, error)) {
        fs::path repoRoot = storage_.repoPath(session_->username, repoName);
        currentDir_ = repoRoot;
        return "Repository '" + repoName + "' created at " + repoRoot.string() + ". Terminal directory switched to repo root.";
    }
    return "Error: " + error;
}

std::string GitLiteApp::handleListCommand() {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    auto repos = storage_.listUserRepos(session_->username);
    if (repos.empty()) {
        return "No repositories found.";
    }
    
    std::string result = "Your repositories:\n";
    for (const auto &repo : repos) {
        std::string visibility = storage_.getVisibility(session_->username, repo);
        result += "  " + repo + " [" + visibility + "]\n";
    }
    return result;
}

std::string GitLiteApp::handleLsUsersCommand() {
    auto users = storage_.loadUsers();
    if (users.empty()) {
        return "No users found.";
    }
    
    std::string result = "Users:\n";
    for (const auto &user : users) {
        result += "  " + user.username + " (" + user.role + ")\n";
    }
    return result;
}

std::string GitLiteApp::handleLsReposCommand(const std::string &username) {
    auto repos = storage_.listUserRepos(username);
    if (repos.empty()) {
        return "No repositories found for user: " + username;
    }
    
    std::string result = "Repositories for " + username + ":\n";
    for (const auto &repo : repos) {
        result += "  " + repo + "\n";
    }
    return result;
}

std::string GitLiteApp::handleStatusCommand(const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, false, ctx, error)) {
        return error;
    }

    auto entries = repoService_.readIndex(ctx.root);
    if (entries.empty()) {
        return "No staged files.";
    }
    
    std::string result = "Staged files:\n";
    for (const auto &entry : entries) {
        result += "  " + entry.first + "\n";
    }
    return result;
}

std::string GitLiteApp::handleAddCommand(const std::string &file,
                                         const std::optional<std::string> &repoOverride) {
    auto sanitizeRelativePath = [](const fs::path &input) {
        fs::path clean;
        for (const auto &part : input) {
            std::string token = part.string();
            if (token.empty() || token == ".") {
                continue;
            }
            if (token == "..") {
                continue;
            }
            clean /= part;
        }
        return clean;
    };

    auto pathHasTraversal = [](const fs::path &input) {
        for (const auto &part : input) {
            if (part == "..") {
                return true;
            }
        }
        return false;
    };

    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, true, ctx, error)) {
        return error;
    }
    std::string repoLabel = ctx.owner.empty() ? ctx.name : ctx.owner + "/" + ctx.name;

    fs::path workspacePath = ctx.root / "workspace";
    if (!fs::exists(workspacePath)) {
        std::error_code dirEc;
        fs::create_directories(workspacePath, dirEc);
        if (dirEc) {
            return "Error: Unable to prepare workspace: " + dirEc.message();
        }
    }

    fs::path sourcePath;
    fs::path providedPath(file);
    if (providedPath.is_absolute()) {
        sourcePath = providedPath;
    } else {
        sourcePath = currentDir_ / providedPath;
    }

    std::error_code canonicalEc;
    fs::path canonicalSource = fs::weakly_canonical(sourcePath, canonicalEc);
    if (!canonicalEc) {
        sourcePath = canonicalSource;
    }

    if (!fs::exists(sourcePath)) {
        return "Error: File not found: " + sourcePath.string();
    }

    std::error_code relEc;
    fs::path relativeToWorkspace = fs::relative(sourcePath, workspacePath, relEc);
    bool alreadyInWorkspace = !relEc && !pathHasTraversal(relativeToWorkspace);

    fs::path repoRelativePath;
    if (alreadyInWorkspace) {
        repoRelativePath = relativeToWorkspace;
    } else {
        if (providedPath.is_absolute()) {
            repoRelativePath = providedPath.filename();
        } else {
            repoRelativePath = sanitizeRelativePath(providedPath.lexically_normal());
        }
        if (repoRelativePath.empty()) {
            repoRelativePath = sourcePath.filename();
        }
        fs::path destination = workspacePath / repoRelativePath;
        std::error_code createEc;
        auto parent = destination.parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent, createEc);
            if (createEc) {
                return "Error: Unable to prepare workspace folder: " + createEc.message();
            }
        }

        std::error_code copyEc;
        if (fs::equivalent(sourcePath, destination, copyEc)) {
            // nothing to copy
        } else {
            fs::copy_file(sourcePath,
                          destination,
                          fs::copy_options::overwrite_existing,
                          copyEc);
            if (copyEc) {
                return "Error: Failed to copy file into workspace: " + copyEc.message();
            }
        }
    }

    std::string relativeString = repoRelativePath.generic_string();
    if (relativeString.empty()) {
        relativeString = sourcePath.filename().generic_string();
        repoRelativePath = fs::path(relativeString);
    }

    std::string message;
    if (repoService_.addFile(ctx.root, relativeString, message)) {
        if (repoOverride) {
            return "Added: " + relativeString + " -> " + repoLabel;
        }
        return "Added: " + relativeString;
    }
    return "Error: " + message;
}

std::string GitLiteApp::handleCommitCommand(const std::string &message) {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    fs::path glitePath = currentDir_ / ".glite";
    
    if (!fs::exists(glitePath)) {
        return "Error: Not a GitLite repository. Run 'init' first.";
    }
    
    CommitRecord record;
    std::string error;
    if (repoService_.commit(glitePath.parent_path(), session_->username, message, record, error)) {
        return "Commit created: " + record.id.substr(0, 12) + "...";
    }
    return "Error: " + error;
}

std::string GitLiteApp::handleLogCommand(const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, false, ctx, error)) {
        return error;
    }
    
    std::string branch = repoService_.currentBranch(ctx.root);
    auto records = repoService_.history(ctx.root, branch, 10);
    
    if (records.empty()) {
        return "No commits yet.";
    }
    
    std::string result = "Commit history (" + branch + "):\n";
    for (const auto &record : records) {
        result += record.id.substr(0, 10) + " | " + record.timestamp + " | " + record.author + "\n";
        result += "  " + record.message + "\n";
    }
    return result;
}

std::string GitLiteApp::handleBranchListCommand(const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, false, ctx, error)) {
        return error;
    }
    
    auto branches = repoService_.listBranchesWithHead(ctx.root);
    std::string current = repoService_.currentBranch(ctx.root);
    
    if (branches.empty()) {
        return "No branches found.";
    }
    
    std::string result = "Branches:\n";
    for (const auto &pair : branches) {
        std::string marker = (pair.first == current) ? "* " : "  ";
        result += marker + pair.first + "\n";
    }
    return result;
}

std::string GitLiteApp::handleBranchCreateCommand(const std::string &branchName,
                                                  const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, true, ctx, error)) {
        return error;
    }

    if (!isValidIdentifier(branchName)) {
        return "Error: Invalid branch name.";
    }
    
    if (repoService_.createBranch(ctx.root, branchName, error)) {
        return "Branch '" + branchName + "' created.";
    }
    return "Error: " + error;
}

std::string GitLiteApp::handleCheckoutCommand(const std::string &branch,
                                              const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, true, ctx, error)) {
        return error;
    }
    
    repoService_.setCurrentBranch(ctx.root, branch);
    return "Switched to branch: " + branch;
}

void GitLiteApp::addMultiLineToTerminal(const std::string &text) {
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        ui_.addTerminalLine(line);
    }
}

fs::path GitLiteApp::getCurrentRepoPath() {
    fs::path glitePath = currentDir_ / ".glite";
    if (fs::exists(glitePath)) {
        return currentDir_;
    }
    return fs::path();
}

// Branching commands
std::string GitLiteApp::handleMergeCommand(const std::string &branch,
                                           const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, true, ctx, error)) {
        return error;
    }
    
    if (repoService_.mergeBranch(ctx.root, branch, error)) {
        return "Merged branch '" + branch + "' into current branch.";
    }
    return "Error: " + error;
}

std::string GitLiteApp::handleRebaseCommand(const std::string &branch,
                                            const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, true, ctx, error)) {
        return error;
    }
    
    if (repoService_.rebaseBranch(ctx.root, branch, error)) {
        return "Rebased current branch onto '" + branch + "'.";
    }
    return "Error: " + error;
}

std::string GitLiteApp::handleRenameBranchCommand(const std::string &oldName,
                                                  const std::string &newName,
                                                  const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, true, ctx, error)) {
        return error;
    }

    if (!isValidIdentifier(newName)) {
        return "Error: Invalid branch name.";
    }
    
    if (repoService_.renameBranch(ctx.root, oldName, newName, error)) {
        return "Branch renamed from '" + oldName + "' to '" + newName + "'.";
    }
    return "Error: " + error;
}

std::string GitLiteApp::handleDeleteBranchCommand(const std::string &branchName,
                                                  const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, true, ctx, error)) {
        return error;
    }
    
    std::string current = repoService_.currentBranch(ctx.root);
    if (branchName == current) {
        return "Error: Cannot delete current branch. Switch to another branch first.";
    }
    
    if (repoService_.deleteBranch(ctx.root, branchName, error)) {
        return "Branch '" + branchName + "' deleted.";
    }
    return "Error: " + error;
}

// Collaboration commands
std::string GitLiteApp::handlePermAddCommand(const std::string &repo, const std::string &user) {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    if (!hasWriteAccess(session_->username, repo)) {
        return "Error: You don't have permission to manage collaborators for this repo.";
    }
    
    if (!userExists(user)) {
        return "Error: User '" + user + "' not found.";
    }
    
    if (user == session_->username) {
        return "Error: Owner already has access.";
    }
    
    auto perms = storage_.loadPermissions();
    std::string key = session_->username + "/" + repo;
    perms[key].insert(user);
    storage_.savePermissions(perms);
    
    return "Added collaborator '" + user + "' to repository '" + repo + "'.";
}

std::string GitLiteApp::handlePermRmCommand(const std::string &repo, const std::string &user) {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    if (!hasWriteAccess(session_->username, repo)) {
        return "Error: You don't have permission to manage collaborators for this repo.";
    }
    
    auto perms = storage_.loadPermissions();
    std::string key = session_->username + "/" + repo;
    perms[key].erase(user);
    storage_.savePermissions(perms);
    
    return "Removed collaborator '" + user + "' from repository '" + repo + "'.";
}

std::string GitLiteApp::handlePermListCommand(const std::string &repo) {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    auto perms = storage_.loadPermissions();
    std::string key = session_->username + "/" + repo;
    auto it = perms.find(key);
    
    if (it == perms.end() || it->second.empty()) {
        return "No collaborators for repository '" + repo + "'.";
    }
    
    std::string result = "Collaborators for " + repo + ":\n";
    for (const auto &user : it->second) {
        result += "  " + user + "\n";
    }
    return result;
}

std::string GitLiteApp::handleForkCommand(const std::string &userRepo) {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    size_t pos = userRepo.find('/');
    if (pos == std::string::npos) {
        return "Error: Invalid format. Use: fork <user>/<repo>";
    }
    
    std::string owner = userRepo.substr(0, pos);
    std::string repo = userRepo.substr(pos + 1);
    
    if (!storage_.repoExists(owner, repo)) {
        return "Error: Repository '" + userRepo + "' not found.";
    }
    
    if (!repoService_.isPublic(owner, repo) && !hasWriteAccess(owner, repo)) {
        return "Error: Repository is private and you don't have access.";
    }
    
    // Create a copy in current user's storage
    fs::path sourceRepo = storage_.repoPath(owner, repo);
    std::string newRepoName = repo + "-fork";
    std::string error;
    
    if (!storage_.createRepo(session_->username, newRepoName, error)) {
        // Try with different name
        int counter = 1;
        while (!storage_.createRepo(session_->username, newRepoName + std::to_string(counter), error)) {
            counter++;
            if (counter > 100) {
                return "Error: Could not create fork.";
            }
        }
        newRepoName = newRepoName + std::to_string(counter);
    }
    
    fs::path destRepo = storage_.repoPath(session_->username, newRepoName);
    repoService_.pull(destRepo, sourceRepo, error);
    
    return "Forked '" + userRepo + "' to '" + session_->username + "/" + newRepoName + "'.";
}

std::string GitLiteApp::handleTransferCommand(const std::string &repo, const std::string &newOwner) {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    // Check if user owns the repo or is admin
    if (session_->role != "admin") {
        // User must own the repo to transfer it
        if (!storage_.repoExists(session_->username, repo)) {
            return "Error: Only repo owner or admin can transfer repositories.";
        }
    }
    
    if (!userExists(newOwner)) {
        return "Error: User '" + newOwner + "' not found.";
    }
    
    if (!storage_.repoExists(session_->username, repo)) {
        return "Error: Repository not found.";
    }
    
    // Move repository
    fs::path oldPath = storage_.repoPath(session_->username, repo);
    fs::path newPath = storage_.repoPath(newOwner, repo);
    
    if (fs::exists(newPath)) {
        return "Error: Repository already exists for user '" + newOwner + "'.";
    }
    
    try {
        fs::create_directories(newPath.parent_path());
        fs::rename(oldPath, newPath);
        
        // Update permissions
        auto perms = storage_.loadPermissions();
        std::string oldKey = session_->username + "/" + repo;
        std::string newKey = newOwner + "/" + repo;
        if (perms.find(oldKey) != perms.end()) {
            perms[newKey] = perms[oldKey];
            perms.erase(oldKey);
            storage_.savePermissions(perms);
        }
        
        return "Repository transferred to '" + newOwner + "'.";
    } catch (const std::exception &ex) {
        return "Error: " + std::string(ex.what());
    }
}

// Syncing commands
std::string GitLiteApp::handlePushCommand() {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    fs::path repoPath = getCurrentRepoPath();
    if (repoPath.empty()) {
        return "Error: Not a GitLite repository. Run 'init' first.";
    }
    
    // Find repo in storage
    std::string repoName = repoPath.filename().string();
    if (!storage_.repoExists(session_->username, repoName)) {
        return "Error: Repository not found in storage. Use 'create' first.";
    }
    
    fs::path remoteRoot = storage_.root() / "_remotes" / session_->username / repoName;
    std::string error;
    if (repoService_.push(repoPath, remoteRoot, error)) {
        return "Pushed to remote.";
    }
    return "Error: " + error;
}

std::string GitLiteApp::handlePullCommand() {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    fs::path repoPath = getCurrentRepoPath();
    if (repoPath.empty()) {
        return "Error: Not a GitLite repository. Run 'init' first.";
    }
    
    std::string repoName = repoPath.filename().string();
    fs::path remoteRoot = storage_.root() / "_remotes" / session_->username / repoName;
    
    std::string error;
    if (repoService_.pull(repoPath, remoteRoot, error)) {
        return "Pulled from remote.";
    }
    return "Error: " + error;
}

std::string GitLiteApp::handleFetchCommand() {
    return handlePullCommand(); // Simplified: fetch = pull for now
}

std::string GitLiteApp::handleSyncCommand() {
    std::string result = handleFetchCommand();
    if (result.find("Error") == std::string::npos) {
        return "Synced successfully.";
    }
    return result;
}

std::string GitLiteApp::handleCloneCommand(const std::string &userRepo) {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    size_t pos = userRepo.find('/');
    if (pos == std::string::npos) {
        return "Error: Invalid format. Use: clone <user>/<repo>";
    }
    
    std::string owner = userRepo.substr(0, pos);
    std::string repo = userRepo.substr(pos + 1);
    
    if (!storage_.repoExists(owner, repo)) {
        return "Error: Repository '" + userRepo + "' not found.";
    }
    
    if (!repoService_.isPublic(owner, repo) && !hasWriteAccess(owner, repo)) {
        return "Error: Repository is private and you don't have access.";
    }
    
    fs::path sourceRepo = storage_.repoPath(owner, repo);
    fs::path destRepo = currentDir_ / repo;
    
    if (fs::exists(destRepo)) {
        return "Error: Directory '" + repo + "' already exists.";
    }
    
    try {
        fs::create_directories(destRepo);
        std::string error;
        repoService_.pull(destRepo, sourceRepo, error);
        return "Cloned '" + userRepo + "' to current directory.";
    } catch (const std::exception &ex) {
        return "Error: " + std::string(ex.what());
    }
}

// Repository management
std::string GitLiteApp::handleDeleteCommand(const std::string &repo) {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    if (!storage_.repoExists(session_->username, repo)) {
        return "Error: Repository not found.";
    }
    
    if (session_->role != "admin" && !hasWriteAccess(session_->username, repo)) {
        return "Error: You don't have permission to delete this repository.";
    }
    
    try {
        fs::path repoPath = storage_.repoPath(session_->username, repo);
        fs::remove_all(repoPath);
        
        // Remove from permissions
        auto perms = storage_.loadPermissions();
        std::string key = session_->username + "/" + repo;
        perms.erase(key);
        storage_.savePermissions(perms);
        
        return "Repository '" + repo + "' deleted.";
    } catch (const std::exception &ex) {
        return "Error: " + std::string(ex.what());
    }
}

std::string GitLiteApp::handleVisibilityCommand(const std::optional<std::string> &repoOverride,
                                                std::optional<bool> newState) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, true, ctx, error)) {
        return error;
    }

    std::string label = ctx.owner.empty() ? ctx.name : ctx.owner + "/" + ctx.name;
    std::string currentVisibility = storage_.getVisibility(ctx.owner, ctx.name);
    bool isCurrentlyPublic = (currentVisibility == "public");

    bool desiredState = newState.value_or(!isCurrentlyPublic);

    if (newState.has_value() && desiredState == isCurrentlyPublic) {
        return "Repository '" + label + "' is already " + (isCurrentlyPublic ? "public." : "private.");
    }

    if (!storage_.setVisibility(ctx.owner, ctx.name, desiredState)) {
        return "Error: Failed to set repository visibility.";
    }

    std::string stateLabel = desiredState ? "public" : "private";
    if (newState.has_value()) {
        return "Repository '" + label + "' is now " + stateLabel + ".";
    }
    return "Repository '" + label + "' visibility toggled to " + stateLabel + ".";
}

std::string GitLiteApp::handleSetPublicCommand(const std::string &repo) {
    return handleVisibilityCommand(std::optional<std::string>(repo), true);
}

std::string GitLiteApp::handleSetPrivateCommand(const std::string &repo) {
    return handleVisibilityCommand(std::optional<std::string>(repo), false);
}

std::string GitLiteApp::handleViewCommand(const std::string &userRepo) {
    size_t pos = userRepo.find('/');
    if (pos == std::string::npos) {
        return "Error: Invalid format. Use: view <user>/<repo>";
    }
    
    std::string owner = userRepo.substr(0, pos);
    std::string repo = userRepo.substr(pos + 1);
    
    if (!storage_.repoExists(owner, repo)) {
        return "Error: Repository '" + userRepo + "' not found.";
    }
    
    if (!repoService_.isPublic(owner, repo) && (!session_ || !hasWriteAccess(owner, repo))) {
        return "Error: Repository is private and you don't have access.";
    }
    
    fs::path repoPath = storage_.repoPath(owner, repo);
    std::string result = "Repository: " + userRepo + "\n";
    result += "Visibility: " + storage_.getVisibility(owner, repo) + "\n";
    result += "Branches:\n";
    
    auto branches = repoService_.listBranchesWithHead(repoPath);
    for (const auto &pair : branches) {
        result += "  " + pair.first + "\n";
    }
    
    return result;
}

// File operations
std::string GitLiteApp::handleRmCommand(const std::string &file) {
    fs::path repoPath = getCurrentRepoPath();
    if (repoPath.empty()) {
        return "Error: Not a GitLite repository. Run 'init' first.";
    }
    
    std::string error;
    if (repoService_.removeFile(repoPath, file, error)) {
        return "Removed: " + file;
    }
    return "Error: " + error;
}

std::string GitLiteApp::handleDiffCommand() {
    fs::path repoPath = getCurrentRepoPath();
    if (repoPath.empty()) {
        return "Error: Not a GitLite repository. Run 'init' first.";
    }
    
    return repoService_.getDiff(repoPath);
}

std::string GitLiteApp::handleResetCommand(const std::string &file) {
    fs::path repoPath = getCurrentRepoPath();
    if (repoPath.empty()) {
        return "Error: Not a GitLite repository. Run 'init' first.";
    }
    
    std::string error;
    if (repoService_.resetFile(repoPath, file, error)) {
        return "Unstaged: " + file;
    }
    return "Error: " + error;
}

std::string GitLiteApp::handleIgnoreCommand(const std::string &pattern) {
    fs::path repoPath = getCurrentRepoPath();
    if (repoPath.empty()) {
        return "Error: Not a GitLite repository. Run 'init' first.";
    }
    
    std::string error;
    if (repoService_.addIgnorePattern(repoPath, pattern, error)) {
        return "Added to .gliteignore: " + pattern;
    }
    return "Error: " + error;
}

// Commit operations
std::string GitLiteApp::handleShowCommand(const std::string &commitHash) {
    fs::path repoPath = getCurrentRepoPath();
    if (repoPath.empty()) {
        return "Error: Not a GitLite repository. Run 'init' first.";
    }
    
    CommitRecord record = repoService_.getCommit(repoPath, commitHash);
    if (record.id.empty()) {
        return "Error: Commit not found.";
    }
    
    std::string result = "Commit: " + record.id + "\n";
    result += "Author: " + record.author + "\n";
    result += "Date: " + record.timestamp + "\n";
    result += "Branch: " + record.branch + "\n";
    result += "Message: " + record.message + "\n";
    result += "Files:\n";
    for (const auto &file : record.files) {
        result += "  " + file.first + "\n";
    }
    
    return result;
}

std::string GitLiteApp::handleRevertCommand(const std::string &commitHash) {
    if (!session_) {
        return "Error: Not logged in.";
    }
    
    fs::path repoPath = getCurrentRepoPath();
    if (repoPath.empty()) {
        return "Error: Not a GitLite repository. Run 'init' first.";
    }
    
    std::string error;
    if (repoService_.revertCommit(repoPath, commitHash, session_->username, error)) {
        return "Reverted commit: " + commitHash;
    }
    return "Error: " + error;
}

// Tagging
std::string GitLiteApp::handleTagCommand(const std::string &tagName,
                                         const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, true, ctx, error)) {
        return error;
    }
    
    if (repoService_.createTag(ctx.root, tagName, error)) {
        return "Tagged current commit as: " + tagName;
    }
    return "Error: " + error;
}

std::string GitLiteApp::handleTagsCommand(const std::optional<std::string> &repoOverride) {
    RepoContext ctx;
    std::string error;
    if (!resolveRepoContext(repoOverride, false, ctx, error)) {
        return error;
    }
    
    auto tags = repoService_.listTags(ctx.root);
    if (tags.empty()) {
        return "No tags found.";
    }
    
    std::string result = "Tags:\n";
    for (const auto &tag : tags) {
        result += "  " + tag + "\n";
    }
    return result;
}

// Admin commands
std::string GitLiteApp::handleMakeAdminCommand(const std::string &username) {
    if (!session_ || session_->role != "admin") {
        return "Error: Only admins can promote users.";
    }
    
    auto users = storage_.loadUsers();
    auto it = std::find_if(users.begin(), users.end(), 
                          [&](const User &u) { return u.username == username; });
    
    if (it == users.end()) {
        return "Error: User not found.";
    }
    
    it->role = "admin";
    storage_.saveUsers(users);
    return "User '" + username + "' promoted to admin.";
}

std::string GitLiteApp::handleRemoveAdminCommand(const std::string &username) {
    if (!session_ || session_->role != "admin") {
        return "Error: Only admins can demote users.";
    }
    
    if (username == session_->username) {
        return "Error: Cannot demote yourself.";
    }
    
    auto users = storage_.loadUsers();
    auto it = std::find_if(users.begin(), users.end(), 
                          [&](const User &u) { return u.username == username; });
    
    if (it == users.end()) {
        return "Error: User not found.";
    }
    
    it->role = "user";
    storage_.saveUsers(users);
    return "User '" + username + "' demoted to regular user.";
}

std::string GitLiteApp::handleReposAllCommand() {
    if (!session_ || session_->role != "admin") {
        return "Error: Only admins can list all repositories.";
    }
    
    auto all = storage_.listAllRepos();
    if (all.empty()) {
        return "No repositories found.";
    }
    
    std::string result = "All repositories:\n";
    for (const auto &pair : all) {
        std::string visibility = storage_.getVisibility(pair.first, pair.second);
        result += "  " + pair.first + "/" + pair.second + " [" + visibility + "]\n";
    }
    return result;
}

// Utility commands
std::string GitLiteApp::handleVersionCommand() {
    return "GitLite v1.0.0 - Offline Terminal GitHub Clone";
}

std::string GitLiteApp::handleConfigCommand(const std::vector<std::string> &args) {
    if (args.empty()) {
        return "Usage: config set|get|list <key> [value]";
    }
    
    if (args[0] == "list") {
        return "Config system not yet implemented.";
    } else if (args[0] == "get" && args.size() >= 2) {
        return "Config '" + args[1] + "' not found.";
    } else if (args[0] == "set" && args.size() >= 3) {
        return "Config '" + args[1] + "' set to '" + args[2] + "'.";
    }
    
    return "Usage: config set|get|list <key> [value]";
}

// Navigation commands
std::string GitLiteApp::handleCdCommand(const std::string &path) {
    fs::path targetPath;
    
    // Handle special paths
    if (path == ".." || path == "../") {
        targetPath = currentDir_.parent_path();
    } else if (path == "." || path == "./") {
        return "Already in: " + currentDir_.string();
    } else if (path == "~" || path == "~/" || path.empty()) {
        targetPath = storage_.root().parent_path();
    } else if (fs::path(path).is_absolute()) {
        targetPath = fs::path(path);
    } else {
        // Relative path
        targetPath = currentDir_ / path;
    }
    
    // Normalize path
    try {
        targetPath = fs::canonical(targetPath);
    } catch (const fs::filesystem_error &) {
        // If canonical fails, try to resolve it
        targetPath = fs::absolute(targetPath);
        if (!fs::exists(targetPath)) {
            return "Error: Directory does not exist: " + path;
        }
    }
    
    if (!fs::exists(targetPath)) {
        return "Error: Directory does not exist: " + path;
    }
    
    if (!fs::is_directory(targetPath)) {
        return "Error: Not a directory: " + path;
    }
    
    currentDir_ = targetPath;
    return "Changed to: " + currentDir_.string();
}

std::string GitLiteApp::handlePwdCommand() {
    return currentDir_.string();
}

std::string GitLiteApp::handleLsCommand() {
    std::string result = "Contents of: " + currentDir_.string() + "\n\n";
    
    if (!fs::exists(currentDir_)) {
        return "Error: Directory does not exist.";
    }
    
    if (!fs::is_directory(currentDir_)) {
        return "Error: Not a directory.";
    }
    
    std::vector<std::string> dirs;
    std::vector<std::string> files;
    
    try {
        for (const auto &entry : fs::directory_iterator(currentDir_)) {
            std::string name = entry.path().filename().string();
            
            // Skip hidden files on Unix-like systems (but show .glite)
            if (name[0] == '.' && name != ".glite") {
                continue;
            }
            
            if (entry.is_directory()) {
                dirs.push_back(name + "/");
            } else if (entry.is_regular_file()) {
                // Get file size
                auto size = entry.file_size();
                std::string sizeStr;
                if (size < 1024) {
                    sizeStr = std::to_string(size) + " B";
                } else if (size < 1024 * 1024) {
                    sizeStr = std::to_string(size / 1024) + " KB";
                } else {
                    sizeStr = std::to_string(size / (1024 * 1024)) + " MB";
                }
                files.push_back(name + " (" + sizeStr + ")");
            }
        }
    } catch (const fs::filesystem_error &ex) {
        return "Error: " + std::string(ex.what());
    }
    
    // Sort
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());
    
    // Display directories first
    if (!dirs.empty()) {
        result += "Directories:\n";
        for (const auto &dir : dirs) {
            result += "  " + dir + "\n";
        }
        result += "\n";
    }
    
    // Then files
    if (!files.empty()) {
        result += "Files:\n";
        for (const auto &file : files) {
            result += "  " + file + "\n";
        }
    }
    
    if (dirs.empty() && files.empty()) {
        result += "(empty)";
    }
    
    return result;
}


