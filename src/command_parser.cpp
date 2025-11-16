#include "command_parser.hpp"

#include "hashing.hpp"
#include "utils.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

using gitlite::util::isValidIdentifier;
using gitlite::util::split;
using gitlite::util::trim;

namespace fs = std::filesystem;

CommandParser::CommandParser(StorageManager &storage, RepoService &repoService, TerminalUI &ui)
    : storage_(storage), repoService_(repoService), ui_(ui) {}

std::vector<std::string> CommandParser::splitCommand(const std::string &command) {
    std::vector<std::string> args;
    std::istringstream iss(command);
    std::string arg;
    bool inQuotes = false;
    std::string current;
    
    for (char c : command) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ' ' && !inQuotes) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        args.push_back(current);
    }
    
    return args;
}

CommandResult CommandParser::execute(const std::string &command, std::optional<User> &session) {
    if (command.empty()) {
        return {true, ""};
    }
    
    auto args = splitCommand(command);
    if (args.empty()) {
        return {true, ""};
    }
    
    std::string cmd = args[0];
    args.erase(args.begin());
    
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
    
    if (cmd == "signup") {
        return handleSignup(args);
    } else if (cmd == "login") {
        return handleLogin(args, session);
    } else if (cmd == "logout") {
        return handleLogout(session);
    } else if (cmd == "whoami") {
        return handleWhoami(session);
    } else if (cmd == "changepass") {
        return handleChangepass(args, session);
    } else if (cmd == "users") {
        if (args.size() >= 1 && args[0] == "list") {
            return handleUsersList(args, session);
        } else if (args.size() >= 2 && args[0] == "delete") {
            return handleUsersDelete(args, session);
        }
        return {false, "Usage: users list|delete <username>"};
    } else if (cmd == "init") {
        return handleInit(args);
    } else if (cmd == "create") {
        return handleCreate(args, session);
    } else if (cmd == "clone") {
        return handleClone(args, session);
    } else if (cmd == "delete") {
        return handleDelete(args, session);
    } else if (cmd == "set-public") {
        return handleSetPublic(args, session);
    } else if (cmd == "set-private") {
        return handleSetPrivate(args, session);
    } else if (cmd == "list") {
        return handleList(session);
    } else if (cmd == "ls-users") {
        return handleLsUsers();
    } else if (cmd == "ls-repos") {
        return handleLsRepos(args);
    } else if (cmd == "view") {
        return handleView(args);
    } else if (cmd == "add") {
        return handleAdd(args, session);
    } else if (cmd == "rm") {
        return handleRm(args, session);
    } else if (cmd == "status") {
        return handleStatus(session);
    } else if (cmd == "diff") {
        return handleDiff(args, session);
    } else if (cmd == "reset") {
        return handleReset(args, session);
    } else if (cmd == "ignore") {
        return handleIgnore(args, session);
    } else if (cmd == "commit") {
        return handleCommit(args, session);
    } else if (cmd == "log") {
        return handleLog(args, session);
    } else if (cmd == "show") {
        return handleShow(args, session);
    } else if (cmd == "revert") {
        return handleRevert(args, session);
    } else if (cmd == "tag") {
        return handleTag(args, session);
    } else if (cmd == "tags") {
        return handleTags(session);
    } else if (cmd == "checkout") {
        return handleCheckout(args, session);
    } else if (cmd == "branch") {
        return handleBranch(args, session);
    } else if (cmd == "merge") {
        return handleMerge(args, session);
    } else if (cmd == "rebase") {
        return handleRebase(args, session);
    } else if (cmd == "rename-branch") {
        return handleRenameBranch(args, session);
    } else if (cmd == "delete-branch") {
        return handleDeleteBranch(args, session);
    } else if (cmd == "push") {
        return handlePush(session);
    } else if (cmd == "pull") {
        return handlePull(session);
    } else if (cmd == "fetch") {
        return handleFetch(session);
    } else if (cmd == "remote") {
        return handleRemote(args, session);
    } else if (cmd == "sync") {
        return handleSync(session);
    } else if (cmd == "perm") {
        if (args.size() >= 2 && args[0] == "add") {
            return handlePermAdd(args, session);
        } else if (args.size() >= 2 && args[0] == "rm") {
            return handlePermRm(args, session);
        } else if (args.size() >= 2 && args[0] == "list") {
            return handlePermList(args, session);
        }
        return {false, "Usage: perm add|rm|list <repo> [user]"};
    } else if (cmd == "transfer") {
        return handleTransfer(args, session);
    } else if (cmd == "fork") {
        return handleFork(args, session);
    } else if (cmd == "make-admin") {
        return handleMakeAdmin(args, session);
    } else if (cmd == "remove-admin") {
        return handleRemoveAdmin(args, session);
    } else if (cmd == "repos") {
        if (args.size() >= 1 && args[0] == "all") {
            return handleReposAll(session);
        }
        return {false, "Usage: repos all"};
    } else if (cmd == "menu") {
        return handleMenu(session);
    } else if (cmd == "help") {
        return handleHelp(args);
    } else if (cmd == "clear") {
        return handleClear();
    } else if (cmd == "version") {
        return handleVersion();
    } else if (cmd == "config") {
        return handleConfig(args);
    } else if (cmd == "history") {
        return handleHistory();
    } else if (cmd == "exit" || cmd == "quit") {
        return {true, "Goodbye!", true};
    } else {
        return {false, "Unknown command: " + cmd + ". Type 'help' for available commands."};
    }
}

CommandResult CommandParser::handleMenu(const std::optional<User> &session) {
    if (!session) {
        return {false, "Not logged in. Use 'login' first."};
    }
    return {true, "MENU_MODE"};
}

CommandResult CommandParser::handleHelp(const std::vector<std::string> &args) {
    if (args.empty()) {
        std::vector<std::string> categories = {
            "I. User & Auth System",
            "II. Repository Management",
            "III. File Tracking Commands",
            "IV. Commit System",
            "V. Branching & Merging",
            "VI. Syncing / Collaboration",
            "VII. Collaboration & Permissions",
            "VIII. Admin & Role Management",
            "IX. UI & Utility Commands"
        };
        int choice = ui_.list("Help Categories", categories);
        if (choice < 0) return {true, ""};
        ui_.message("Commands", categories);
        return {true, ""};
    }
    return {true, ""};
}

CommandResult CommandParser::handleSignup(const std::vector<std::string> &args) {
    return {false, "Use the signup menu option"};
}

CommandResult CommandParser::handleLogin(const std::vector<std::string> &args, std::optional<User> &session) {
    return {false, "Use the login menu option"};
}

CommandResult CommandParser::handleLogout(std::optional<User> &session) {
    session.reset();
    return {true, "Logged out successfully"};
}

CommandResult CommandParser::handleWhoami(const std::optional<User> &session) {
    if (!session) return {false, "Not logged in"};
    return {true, "User: " + session->username + " (Role: " + session->role + ")"};
}

// =============================
// 🧩 FINAL STUB HANDLERS
// =============================

#define CLEAN_NAME(name) (std::string(#name).substr(6))
#define STUB_CMD_MUTABLE(name) CommandResult CommandParser::name(const std::vector<std::string> &, std::optional<User> &) { \
    return {false, CLEAN_NAME(name) + " not implemented"}; }
#define STUB_CMD_CONST(name) CommandResult CommandParser::name(const std::vector<std::string> &, const std::optional<User> &) { \
    return {false, CLEAN_NAME(name) + " not implemented"}; }
#define STUB_SIMPLE(name) CommandResult CommandParser::name(const std::optional<User> &) { \
    return {false, CLEAN_NAME(name) + " not implemented"}; }
#define STUB_ARGS(name) CommandResult CommandParser::name(const std::vector<std::string> &) { \
    return {false, CLEAN_NAME(name) + " not implemented"}; }
#define STUB_VOID(name) CommandResult CommandParser::name() { \
    return {false, CLEAN_NAME(name) + " not implemented"}; }

STUB_CMD_MUTABLE(handleChangepass)
STUB_CMD_CONST(handleUsersList)
STUB_CMD_CONST(handleUsersDelete)
STUB_ARGS(handleInit)
STUB_CMD_CONST(handleCreate)
STUB_CMD_CONST(handleClone)
STUB_CMD_CONST(handleDelete)
STUB_CMD_CONST(handleSetPublic)
STUB_CMD_CONST(handleSetPrivate)
STUB_SIMPLE(handleList)
STUB_VOID(handleLsUsers)
STUB_ARGS(handleLsRepos)
STUB_ARGS(handleView)
STUB_CMD_CONST(handleAdd)
STUB_CMD_CONST(handleRm)
STUB_SIMPLE(handleStatus)
STUB_CMD_CONST(handleDiff)
STUB_CMD_CONST(handleReset)
STUB_CMD_CONST(handleIgnore)
STUB_CMD_CONST(handleCommit)
STUB_CMD_CONST(handleLog)
STUB_CMD_CONST(handleShow)
STUB_CMD_CONST(handleRevert)
STUB_CMD_CONST(handleTag)
STUB_SIMPLE(handleTags)
STUB_CMD_CONST(handleCheckout)
STUB_CMD_CONST(handleBranch)
STUB_CMD_CONST(handleMerge)
STUB_CMD_CONST(handleRebase)
STUB_CMD_CONST(handleRenameBranch)
STUB_CMD_CONST(handleDeleteBranch)
STUB_SIMPLE(handlePush)
STUB_SIMPLE(handlePull)
STUB_SIMPLE(handleFetch)
STUB_CMD_CONST(handleRemote)
STUB_SIMPLE(handleSync)
STUB_CMD_CONST(handlePermAdd)
STUB_CMD_CONST(handlePermRm)
STUB_CMD_CONST(handlePermList)
STUB_CMD_CONST(handleTransfer)
STUB_CMD_CONST(handleFork)
STUB_CMD_CONST(handleMakeAdmin)
STUB_CMD_CONST(handleRemoveAdmin)
STUB_SIMPLE(handleReposAll)
STUB_VOID(handleClear)
STUB_VOID(handleVersion)
STUB_ARGS(handleConfig)
STUB_VOID(handleHistory)

// =============================
// 🧩 Helper Stubs
// =============================
std::filesystem::path CommandParser::getCurrentRepoPath(const std::optional<User> &) {
    return std::filesystem::current_path();
}
bool CommandParser::isInRepo() { return false; }
bool CommandParser::hasWriteAccess(const std::string &, const std::string &, const std::optional<User> &) { return true; }
