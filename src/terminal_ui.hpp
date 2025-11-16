#pragma once

#include <ncurses.h>
#include <string>
#include <vector>
#include <utility>

class TerminalUI {
public:
    TerminalUI();
    ~TerminalUI();

    int menu(const std::string &title,
             const std::vector<std::string> &options,
             const std::string &hint = "↑↓/Mouse Navigate | ↵/Click Select | Q Back");

    std::string prompt(const std::string &label, bool secret = false, std::size_t maxLen = 64);

    void message(const std::string &title, const std::vector<std::string> &lines, int colorPair = 0);

    bool confirm(const std::string &question);

    int list(const std::string &title,
             const std::vector<std::string> &items,
             const std::string &hint = "↑↓/Mouse Navigate | ↵/Click Select | Q Back");
    
    std::string getCommand(const std::string &prompt);

    void addHistory(const std::string &entry, int colorPair = 0);
    void drawHistory();
    
    // Split-screen terminal with history
    void initSplitScreen();
    void drawSidebar(const std::vector<std::string> &content, const std::string &title);
    void addTerminalLine(const std::string &line);
    void scrollTerminal(int lines);
    std::string getTerminalCommand(const std::string &prompt);
    void refreshSplitScreen();
    void clearTerminal();

private:
    std::vector<std::pair<std::string, int>> history_;
    WINDOW *terminalWin_;
    WINDOW *sidebarWin_;
    std::vector<std::string> terminalLines_;
    int terminalScrollOffset_;
    bool splitScreenMode_;
};


