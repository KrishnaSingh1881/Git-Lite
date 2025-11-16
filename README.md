# GitLite – Developer Guide and Architectural Reference

GitLite is a lightweight, ncurses-powered GitHub clone designed for offline use. This document explains the structure of the `src/` directory, detailing how each translation unit collaborates to deliver the experience. It also catalogues every user-facing command and the supporting flows underneath, so you can orient yourself quickly when maintaining or extending the project.

---

## Table of Contents

1. [Executive Overview](#executive-overview)  
2. [Runtime Architecture](#runtime-architecture)  
   - [Event Flow](#event-flow)  
   - [Subsystem Responsibilities](#subsystem-responsibilities)  
3. [File-by-File Walkthrough (`src/`)](#file-by-file-walkthrough-src)  
   - [`main.cpp`](#maincpp)  
   - [`gitlite_app.hpp/cpp`](#gitlite_apphppcpp)  
   - [`terminal_ui.hpp/cpp`](#terminal_uihppcpp)  
   - [`storage_manager.hpp/cpp`](#storage_managerhppcpp)  
   - [`repo_service.hpp/cpp`](#repo_servicehppcpp)  
   - [`hashing.hpp/cpp`](#hashing_hppcpp)  
   - [`utils.hpp/cpp`](#utils_hppcpp)  
   - [`command_parser.hpp/cpp`](#command_parserhppcpp)  
   - [`gitlite_app_helper blocks`](#gitlite-app-command-handlers)  
4. [Command Reference](#command-reference)  
   - [Authentication & Session](#authentication--session)  
   - [Repository Lifecycle](#repository-lifecycle)  
   - [File Operations & Index](#file-operations--index)  
   - [Branching & History](#branching--history)  
   - [Collaboration & Visibility](#collaboration--visibility)  
   - [Synchronization](#synchronization)  
   - [Navigation & Utility](#navigation--utility)  
5. [Data Layout](#data-layout)  
6. [Inter-module Communication](#inter-module-communication)  
7. [Extending the System](#extending-the-system)  
8. [Development Checklist](#development-checklist)  

---

## Executive Overview

GitLite wraps a Git-inspired workflow in a split-pane terminal interface. The split view pairs:

- **Terminal Pane (left)** – accepts commands, displays results, and now supports scrolling through past output (`PgUp`, `PgDn`, arrow keys).
- **Sidebar (right)** – shows session context (current repo, user tips).

Input is processed by `GitLiteApp`, which:

1. Manages authentication,
2. Maintains session state and current working directory,
3. Dispatches commands to the correct handler,
4. Delegates persistence concerns to `StorageManager`,
5. Delegates VCS-specific operations to `RepoService`,
6. Relies on `TerminalUI` as the view layer.

---

## Runtime Architecture

### Event Flow

1. **Process Entry (`main.cpp`)**  
   `main()` constructs `GitLiteApp` and calls `run()`.

2. **Initialization (`GitLiteApp::GitLiteApp`)**  
   - Instantiates `StorageManager`, `RepoService`, and `TerminalUI`.
   - Captures OS current path as `currentDir_`.

3. **Landing Loop (`GitLiteApp::showLanding`)**  
   - Displays a menu (signup, login, exit).
   - On successful login, transitions into terminal mode.

4. **Terminal Mode (`GitLiteApp::terminalMode`)**  
   - Initializes split screen, displays session banner.
   - Enters command loop (`while (session_)`).
   - Each iteration:
     1. Prompts via `TerminalUI::getTerminalCommand`.
     2. Parses and route command to a handler (`handleAddCommand`, etc.).
     3. Writes output to `TerminalUI`.
     4. Refreshes sidebar (`updateSidebar`).

5. **Handlers**  
   - Use `StorageManager` for filesystem interactions outside a repo.
   - Use `RepoService` for repo-internal logic (index, commits, branches, history).

6. **Supporting Services**  
   - `hashing` (libsodium-backed, or fallback) handles password and object hashing.
   - `utils` offers string helpers, timestamps, path validation.

### Subsystem Responsibilities

| Subsystem | Responsibility | Key Collaborators |
|-----------|----------------|-------------------|
| `GitLiteApp` | Session orchestration, command routing, UI coordination | `TerminalUI`, `StorageManager`, `RepoService`, `hashing`, `utils` |
| `TerminalUI` | Ncurses-based rendering, split-pane management, input capture (with scrollback) | Ncurses (external) |
| `StorageManager` | Data persistence under `storage/`, user management, repository scaffolding | `<filesystem>`, `utils` |
| `RepoService` | Core VCS mechanics: staging, commits, branches, tags, sync | `StorageManager`, `hashing`, `<filesystem>` |
| `hashing` | Password hashing/verification, SHA-256 object hashing | Libsodium (runtime dependency) |
| `utils` | Common helpers: string trimming, identifier validation, tokenization | Standard library |

---

## File-by-File Walkthrough (`src/`)

### `main.cpp`

*Purpose:* Entry point. Sets up the app and handles fatal exceptions to restore terminal state.

```text
Main Flow:
1. Construct GitLiteApp.
2. Call run().
3. Catch std::exception, call endwin() to reset ncurses, print error.
```

No business logic resides here—this file simply bootstraps the application.

---

### `gitlite_app.hpp/cpp`

*Purpose:* Central coordinator for everything the user experiences after launching the executable.

#### Key Members

- `StorageManager storage_` – persistence service.
- `RepoService repoService_` – VCS operations built atop `storage_`.
- `TerminalUI ui_` – ncurses-based view layer.
- `std::optional<User> session_` – current authenticated user.
- `std::filesystem::path currentDir_` – tracks current working directory.

#### High-level Methods

| Method | Responsibility |
|--------|----------------|
| `run()` | Ensures cryptography is available (`hashing::ensureSodium`) and shows landing menu. |
| `showLanding()` | Signup/login loop. |
| `handleSignup()` | Validates username/password, persists new user, creates user storage folder. |
| `handleLogin()` | Authenticates via password hash verification, sets `session_`, then calls `terminalMode()`. |
| `terminalMode()` | Configures split screen, enters command dispatch loop. |
| `updateSidebar()` | Displays “My repositories”, highlights the repo matching `currentDir_`, includes user tips. |
| `dashboard()` | Legacy menu for quick actions (create repo, view repos, etc.). |

#### Command Handling Blocks

`terminalMode()` routes commands through a long `if/else if` ladder. Each handler adheres to a consistent pattern:

1. Validate session/arguments.
2. Resolve paths (often via `currentDir_` or `StorageManager`).
3. Call `RepoService` or `StorageManager`.
4. Return user-facing message(s).

Examples:

| Handler | Highlights |
|---------|------------|
| `handleAddCommand` | Accepts `add <file> [repo]`. If a repo override is provided, confirms access, copies file into the repo’s `workspace/`, and stages it. Supports absolute and relative paths and now tolerates scrollback interactions. |
| `handleCommitCommand` | Validates login, ensures `.glite` exists, passes data to `RepoService::commit`. |
| `handleBranchCreateCommand` | Validates branch name with `utils::isValidIdentifier`, delegates to `RepoService`. |
| `handleCloneCommand` | Validates repository visibility/access, clones into `currentDir_` via `RepoService::pull`. |
| `handleVisibilityCommand` | Centralizes repo visibility updates, supporting toggle or explicit public/private state with optional repo overrides. |

The class also contains UI-heavy helper methods (`manageRepository`, `showStatus`, etc.) that run pop-up menus through `TerminalUI`.

---

### `terminal_ui.hpp/cpp`

*Purpose:* All presentation logic and input capture.

#### Construction & Teardown

- Initializes ncurses state: full-screen layout, color pairs, mouse capture.
- Destructor calls `endwin()` to restore terminal.

#### Core Methods

| Method | Responsibility |
|--------|----------------|
| `menu()` / `list()` | Modal menus with keyboard/mouse navigation. |
| `prompt()` | Text entry dialogs (with optional masking). |
| `message()` | Styled modal alert boxes. |
| `confirm()` | Yes/no modal prompts. |
| `initSplitScreen()` | Sets up terminal + sidebar panes, enabling scroll. |
| `drawSidebar()` | Renders the sidebar content. |
| `addTerminalLine()` | Appends terminal output, resets scroll offset, refreshes split view. |
| `scrollTerminal(int)` | Adjusts scroll offset and redraws content. |
| `getTerminalCommand()` | Interactive input loop:
  - Keeps cursor anchored at bottom.
  - Re-renders prompt + typed characters on each keypress.
  - Handles scrolling shortcuts while waiting for input (`PgUp`, `PgDn`, `↑`, `↓`).
  - Supports resize events by recalculating pane geometry. |

The new scrollback behaviour is localized here, ensuring no other modules require changes.

---

### `storage_manager.hpp/cpp`

*Purpose:* Owns everything under the `storage/` directory.

#### Data Model

- `storage/users.tsv` – tab-separated list of users (`username`, `passwordHash`, `role`).
- `storage/permissions.tsv` – repo-to-collaborator mapping.
- `storage/<user>/<repo>/` – repository tree (includes `.glite/` metadata and `workspace/` directory).
- `storage/_remotes/` – remote mirrors for push/pull.

#### Responsibilities

| Method | Responsibility |
|--------|----------------|
| `loadUsers` / `saveUsers` | Parse/persist TSV user file. |
| `loadPermissions` / `savePermissions` | Handle collaborator data (comma-separated list per repo key). |
| `ensureUserFolder` | Guarantee `storage/<user>` exists. |
| `listUserRepos` | Enumerate repos for a user (excluding internal `_remotes`). |
| `listAllRepos` | Cross-user discovery (used for sidebar/public browsing). |
| `repoPath` / `repoExists` | Build and check repository paths. |
| `createRepo` | Scaffold `.glite` structure (HEAD, refs, index, config, log) and `workspace/`. |
| `setVisibility` / `getVisibility` | Update `.glite/config` key/value pairs. |

Internally relies on helper functions for directory/file creation (`ensureDirectory`, `ensureFile`, `writeFile`) and config parsing/writing.

---

### `repo_service.hpp/cpp`

*Purpose:* Implements GitLite’s VCS semantics using simple filesystem operations.

#### Major Capabilities

- **Index Management**: `readIndex`, `writeIndex`.
- **Staging**: `addFile` – validates presence in `workspace/`, copies blob into `.glite/objects/`, updates index.
- **Commit Lifecycle**: `commit`, `appendLog`, `getCommit`.
- **Branching**: `listBranchesWithHead`, `currentBranch`, `setCurrentBranch`, `createBranch`, `mergeBranch`, `rebaseBranch`, `renameBranch`, `deleteBranch`.
- **Tags**: `createTag`, `listTags`.
- **History**: `history` (returns `CommitRecord` vector), `getDiff`.
- **Sync**: `push`/`pull` (filesystem copy between repo and `_remotes`).
- **File Operations**: `removeFile`, `resetFile`, `addIgnorePattern`.

`RepoService` is intentionally stateless apart from holding a reference to `StorageManager`. Every method accepts an explicit `repoRoot` path so the caller controls context.

---

### `hashing.hpp/cpp`

*Purpose:* Cryptographic utilities.

- `ensureSodium()` – initializes libsodium or throws if unavailable.
- `hashPassword()` / `verifyPassword()` – password hashing suitable for storage.
- `sha256File()` / `sha256String()` – deterministic hashing for blob/commit IDs.

Errors bubble up as `std::runtime_error` with diagnostic messages.

---

### `utils.hpp/cpp`

*Purpose:* Shared helpers.

- `trim`, `split` – string manipulation.
- `timestamp` – returns formatted current time.
- `isValidIdentifier` – enforces repository/user/branch naming rules `[A-Za-z0-9._-]`.
- Additional helpers for path and token handling used throughout the codebase.

---

### `command_parser.hpp/cpp`

*Purpose:* While the current CLI parsing is manual, this module supplies reusable parsing utilities (tokenization, quoting support) for any menu-driven flows requiring more sophisticated handling. In the existing code, the `split` helper from `utils` is typically used, but this module remains available for future enhancement or alternate UI modes.

---

### GitLite App Command Handlers

Within `gitlite_app.cpp` the command handlers are grouped by functional area using comments (`// Collaboration commands`, `// Syncing commands`, etc.). Each block:

1. Performs parameter validation and permission checks.
2. Resolves the relevant repository via `StorageManager`.
3. Invokes `RepoService` for side-effects.
4. Returns a user-friendly status string, often consumed by `addMultiLineToTerminal`.

This explicit grouping makes it easy to deactivate or modify subsets of functionality.

---

## Command Reference

Outputs use Git-style messaging wherever practical.

### Authentication & Session

| Command | Description |
|---------|-------------|
| `login` / `signup` | Performed from landing menu, not terminal commands. |
| `logout`, `exit`, `quit` | End session and return to landing screen. |
| `whoami` | Display authenticated user and role. |

### Repository Lifecycle

| Command | Description |
|---------|-------------|
| `create <repo>` | Create repository under `storage/<user>/<repo>` and switch to it. |
| `list` | List your repositories with visibility status. |
| `ls-repos <user>` | List repositories belonging to `<user>`. |
| `delete <repo>` | Remove repository (with permission check). |
| `set-public <repo>` / `set-private <repo>` | Force visibility to public or private. Accepts `owner/repo`. |
| `visibility [repo] [public\|private]` | Toggle current repo visibility or set the target repo to a specific state without changing directories. |
| `view <user>/<repo>` | Inspect repository metadata (branches, visibility). |

### File Operations & Index

| Command | Description |
|---------|-------------|
| `add <file> [repo]` | Stage file for commit. If `[repo]` supplied, target that repo without changing directories; file is copied into its `workspace/` automatically. |
| `status [repo]` | Show staged files. Optional `[repo]` lets you inspect another repo from anywhere. |
| `rm <file>` | Remove and unstage file. |
| `reset <file>` | Unstage file without deleting it. |
| `diff` | View staged diffs. |
| `ignore <pattern>` | Add pattern to `.gliteignore`. |

### Commit Lifecycle

| Command | Description |
|---------|-------------|
| `commit -m "message"` | Commit staged files. Prompted for message if `-m` omitted. |
| `log [repo]` | Show recent commit history for the current or specified repo. |
| `show <commit>` | Inspect commit details. |
| `revert <commit>` | Create a new commit reverting changes from `<commit>`. |
| `tag <name> [repo]` / `tags [repo]` | Create/list tags for the current or specified repo. |

### Branching & History

| Command | Description |
|---------|-------------|
| `branch [repo]` / `branch list [repo]` | List branches (current or target repo). |
| `branch <name> [repo]` | Create branch. |
| `checkout <branch> [repo]` | Switch branch. |
| `merge <branch> [repo]` | Merge into current branch. |
| `rebase <branch> [repo]` | Rebase current branch onto `<branch>`. |
| `rename-branch <old> <new> [repo]` | Rename branch. |
| `delete-branch <name> [repo]` | Delete branch (not HEAD). |

### Collaboration & Visibility

| Command | Description |
|---------|-------------|
| `perm add <repo> <user>` | Grant collaborator (owner/admin only). |
| `perm rm <repo> <user>` | Revoke collaborator. |
| `perm list <repo>` | List collaborators. |
| `make-admin <user>` / `remove-admin <user>` | Promote/demote admins (admin only). |
| `repos all` | List all repos (admin only). |
| `fork <user>/<repo>` | Create a fork in your namespace. |
| `transfer <repo> <new-owner>` | Transfer ownership. |

### Synchronization

| Command | Description |
|---------|-------------|
| `push` | Copy repo to `_remotes/<user>/<repo>`. |
| `pull` | Refresh from remote mirror. |
| `fetch` | Alias of `pull`. |
| `sync` | `fetch` then merge (simplified). |
| `clone <user>/<repo>` | Clone repo into current directory. |

### Navigation & Utility

| Command | Description |
|---------|-------------|
| `cd [path]` | Change directory (defaults to workspace root when omitted). |
| `pwd` | Show current directory. |
| `ls` / `dir` | List directory contents. Hidden entries (except `.glite`) suppressed. |
| `menu` | Return to dashboard menu. |
| `help` | Show categories. |
| `help/<category>` or `help <category>` | Show category-specific guidance. |
| `version` | Display version banner. |
| `config set|get|list` | Placeholder configuration commands. |
| `clear` | Clear terminal pane. |

---

## Data Layout

```
storage/
├─ users.tsv              # Registered users (username, hash, role)
├─ permissions.tsv        # Collaborator lists per repo key
├─ <username>/
│  └─ <repo>/
│     ├─ .glite/
│     │  ├─ HEAD
│     │  ├─ refs/heads/<branch>
│     │  ├─ objects/<blob-or-commit-id>
│     │  ├─ index
│     │  ├─ config
│     │  └─ log
│     └─ workspace/       # User-editable working tree
└─ _remotes/<user>/<repo> # Remote mirrors for syncing
```

---

## Inter-module Communication

1. **GitLiteApp ↔ TerminalUI**  
   - `GitLiteApp` invokes UI helpers (`menu`, `message`, `getTerminalCommand`); UI reports back user choices.
   - `TerminalUI` does not depend on application logic – it simply renders input/output.

2. **GitLiteApp ↔ StorageManager**  
   - Used for user persistence, repo discovery, repo creation, and visibility settings.
   - `GitLiteApp` holds on to `StorageManager::root()` for path calculations when syncing UI cues.

3. **GitLiteApp ↔ RepoService**  
   - All VCS operations flow through this channel. `GitLiteApp` provides user context (author, permissions) while `RepoService` executes filesystem changes.

4. **RepoService ↔ StorageManager**  
   - `RepoService` maintains a reference to `StorageManager` for high-level operations but mostly works directly with the filesystem under a repo root.

5. **hashing / utils**  
   - Utilities are consumed by multiple layers (e.g., `hashing::hashPassword` in signup/login, `utils::split` for command parsing).

---

## Extending the System

When adding features:

1. **Decide the Layer**  
   - If it’s UI-related (new dialog, different layout) → `TerminalUI`.
   - If it’s repository metadata or user data → `StorageManager`.
   - If it’s Git-like behavior (diff algorithm, commit semantics) → `RepoService`.
   - If it’s command parsing/state → `GitLiteApp`.

2. **Maintain Separation**  
   - UI should not know about storage internals.
   - `RepoService` should remain stateless, driven by explicit parameters.
   - Introduce new helper modules if horizontal concerns emerge (e.g., logging, configuration).

3. **Update Help & README**  
   - Document new commands in `help` strings inside `GitLiteApp` and add them to this README.

---

## Development Checklist

- [ ] Run `hashing::ensureSodium()` during startup (already handled in `run()`).
- [ ] Keep `TerminalUI` responsive to `KEY_RESIZE`.
- [ ] Keep command handlers free of UI code beyond returning strings or using `ui_.message`.
- [ ] After editing UI interactions, verify scrollback (`PgUp`, `PgDn`, `↑`, `↓`) still works.
- [ ] Ensure every new repository action updates the sidebar (`updateSidebar`).
- [ ] When touching `repo_service`, consider both on-disk and remote mirror implications.
- [ ] Update automated help strings with any CLI changes.

With these guidelines and the module breakdown above, you should have a comprehensive picture of how GitLite is organized and how the components wire together. Happy hacking!


