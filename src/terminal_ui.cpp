#include "terminal_ui.hpp"

#include <algorithm>
#include <cctype>
#include <ncurses.h>

namespace {
std::pair<int, int> computePaneWidths(int maxX) {
    if (maxX <= 0) {
        return {0, 0};
    }

    int terminalWidth = std::max(60, (maxX * 80) / 100);
    if (terminalWidth > maxX - 20) {
        terminalWidth = maxX - 20;
    }
    if (terminalWidth < 40) {
        terminalWidth = std::max(1, maxX - 1);
    }

    int sidebarWidth = maxX - terminalWidth - 1;
    if (sidebarWidth < 20) {
        sidebarWidth = 20;
        terminalWidth = maxX - sidebarWidth - 1;
        if (terminalWidth < 40) {
            terminalWidth = std::max(40, maxX - 1);
            sidebarWidth = std::max(0, maxX - terminalWidth - 1);
        }
    }

    if (terminalWidth < 0) terminalWidth = 0;
    if (sidebarWidth < 0) sidebarWidth = 0;

    return {terminalWidth, sidebarWidth};
}
} // namespace

TerminalUI::TerminalUI() 
    : terminalWin_(nullptr), sidebarWin_(nullptr), terminalScrollOffset_(0), splitScreenMode_(false) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    start_color();
    use_default_colors();
    init_pair(1, COLOR_CYAN, -1);    // Command
    init_pair(2, COLOR_YELLOW, -1);  // Message
    init_pair(3, COLOR_RED, -1);     // Error
    init_pair(4, COLOR_GREEN, -1);   // Success
    // Enable mouse support
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
    mouseinterval(0);
    // Make fullscreen
    resize_term(0, 0);
}

TerminalUI::~TerminalUI() {
    endwin();
}

void TerminalUI::addHistory(const std::string &entry, int colorPair) {
    history_.emplace_back(entry, colorPair);
    drawHistory();
}

void TerminalUI::drawHistory() {
    clear();
    int maxY, maxX;
    getmaxyx(stdscr, maxY, maxX);
    int startLine = std::max(0, (int)history_.size() - (maxY - 2));
    for (int i = startLine, y = 0; i < (int)history_.size() && y < maxY - 1; ++i, ++y) {
        int color = history_[i].second;
        if (color > 0) wattron(stdscr, COLOR_PAIR(color));
        mvprintw(y, 0, "%s", history_[i].first.c_str());
        if (color > 0) wattroff(stdscr, COLOR_PAIR(color));
    }
    move(maxY - 1, 0);
    clrtoeol();
    refresh();
}

int TerminalUI::menu(const std::string &title,
                     const std::vector<std::string> &options,
                     const std::string &hint) {
    if (options.empty()) {
        return -1;
    }

    WINDOW *win = nullptr;
    int highlight = 0;
    int winHeight = 0, winWidth = 0;

    auto redrawMenu = [&]() {
        if (win) {
            delwin(win);
        }
        clear();
        refresh();
        
        winWidth = static_cast<int>(title.size()) + 30;
        for (const auto &opt : options) {
            winWidth = std::max(winWidth, static_cast<int>(opt.size()) + 30);
        }
        winWidth = std::min(winWidth, COLS - 10);
        winWidth = std::max(winWidth, 70);
        winHeight = static_cast<int>(options.size()) + 12;
        winHeight = std::min(winHeight, LINES - 6);
        winHeight = std::max(winHeight, 15);
        int starty = (LINES - winHeight) / 2;
        int startx = (COLS - winWidth) / 2;
        starty = std::max(0, starty);
        startx = std::max(0, startx);

        win = newwin(winHeight, winWidth, starty, startx);
        keypad(win, TRUE);
        box(win, 0, 0);
        wattron(win, COLOR_PAIR(1) | A_BOLD);
        mvwprintw(win, 1, std::max(2, (winWidth - static_cast<int>(title.size())) / 2), "%s", title.c_str());
        wattroff(win, COLOR_PAIR(1) | A_BOLD);
        
        int hintLen = static_cast<int>(hint.size());
        if (hintLen < winWidth - 4) {
            mvwprintw(win, winHeight - 2, 2, "%s", hint.c_str());
        } else {
            mvwprintw(win, winHeight - 2, 2, "%.*s", winWidth - 4, hint.c_str());
        }

        for (std::size_t i = 0; i < options.size(); ++i) {
            int y = 3 + static_cast<int>(i);
            if (y >= winHeight - 2) {
                break;
            }
            if (static_cast<int>(i) == highlight) {
                wattron(win, A_REVERSE);
            }
            int optWidth = winWidth - 6;
            if (static_cast<int>(options[i].size()) > optWidth) {
                mvwprintw(win, y, 3, "%.*s", optWidth, options[i].c_str());
            } else {
                mvwprintw(win, y, 3, "%-*s", optWidth, options[i].c_str());
            }
            wattroff(win, A_REVERSE);
        }
        wrefresh(win);
    };

    redrawMenu();

    while (true) {
        int ch = wgetch(win);
        if (ch == KEY_RESIZE) {
            resize_term(0, 0);
            endwin();
            refresh();
            redrawMenu();
            continue;
        }
        
        // Handle mouse events
        if (ch == KEY_MOUSE) {
            MEVENT event;
            if (getmouse(&event) == OK) {
                // Check if click is within the menu window
                int winY, winX;
                getbegyx(win, winY, winX);
                int relY = event.y - winY;
                int relX = event.x - winX;
                
                if (relY >= 3 && relY < winHeight - 2 && relX >= 0 && relX < winWidth) {
                    int clickedIndex = relY - 3;
                    if (clickedIndex >= 0 && clickedIndex < static_cast<int>(options.size())) {
                        if (event.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED)) {
                            highlight = clickedIndex;
                            delwin(win);
                            return highlight;
                        } else if (event.bstate & BUTTON1_RELEASED) {
                            highlight = clickedIndex;
                            redrawMenu();
                        }
                    }
                }
            }
            continue;
        }
        
        if (ch == KEY_UP) {
            highlight = (highlight - 1 + static_cast<int>(options.size())) % static_cast<int>(options.size());
            redrawMenu();
        } else if (ch == KEY_DOWN) {
            highlight = (highlight + 1) % static_cast<int>(options.size());
            redrawMenu();
        } else if (ch == 10 || ch == KEY_ENTER) {
            delwin(win);
            return highlight;
        } else if (ch == 'q' || ch == 'Q' || ch == 27) {
            delwin(win);
            return -1;
        }
    }
}

std::string TerminalUI::prompt(const std::string &label, bool secret, std::size_t maxLen) {
    WINDOW *win = nullptr;
    std::string input;

    auto redrawPrompt = [&]() {
        if (win) {
            delwin(win);
        }
        clear();
        refresh();
        
        int width = static_cast<int>(label.size()) + static_cast<int>(maxLen) + 20;
        width = std::min(width, COLS - 8);
        width = std::max(width, 50);
        int height = 9;
        int starty = (LINES - height) / 2;
        int startx = (COLS - width) / 2;
        starty = std::max(0, starty);
        startx = std::max(0, startx);

        win = newwin(height, width, starty, startx);
        keypad(win, TRUE);
        box(win, 0, 0);

        int labelLen = static_cast<int>(label.size());
        if (labelLen < width - 4) {
            mvwprintw(win, 1, 2, "%s", label.c_str());
        } else {
            mvwprintw(win, 1, 2, "%.*s", width - 4, label.c_str());
        }
        mvwprintw(win, height - 2, 2, "↵ Accept | ESC Cancel");
        
        std::string display = secret ? std::string(input.size(), '*') : input;
        int displayWidth = width - 4;
        if (static_cast<int>(display.size()) > displayWidth) {
            mvwprintw(win, 3, 2, "%.*s", displayWidth, display.c_str());
        } else {
            mvwprintw(win, 3, 2, "%-*s", displayWidth, display.c_str());
        }
        wmove(win, 3, 2 + std::min(static_cast<int>(display.size()), displayWidth));
        wrefresh(win);
    };

    redrawPrompt();

    while (true) {
        int ch = wgetch(win);
        if (ch == KEY_RESIZE) {
            resize_term(0, 0);
            endwin();
            refresh();
            redrawPrompt();
            continue;
        }
        if (ch == 27) {
            input.clear();
            break;
        }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!input.empty()) {
                input.pop_back();
            }
            redrawPrompt();
        } else if (ch == 10 || ch == KEY_ENTER) {
            break;
        } else if (std::isprint(ch) && input.size() < maxLen) {
            input.push_back(static_cast<char>(ch));
            redrawPrompt();
        }
    }
    delwin(win);
    return input;
}

void TerminalUI::message(const std::string &title, const std::vector<std::string> &lines, int colorPair) {
    WINDOW *win = nullptr;

    auto redrawMessage = [&]() {
        if (win) {
            delwin(win);
        }
        clear();
        refresh();
        
        int width = static_cast<int>(title.size()) + 20;
        for (const auto &line : lines) {
            width = std::max(width, static_cast<int>(line.size()) + 20);
        }
        width = std::min(width, COLS - 8);
        width = std::max(width, 60);
        int height = static_cast<int>(lines.size()) + 10;
        height = std::min(height, LINES - 4);
        height = std::max(height, 15);
        int starty = (LINES - height) / 2;
        int startx = (COLS - width) / 2;
        starty = std::max(0, starty);
        startx = std::max(0, startx);

        win = newwin(height, width, starty, startx);
        box(win, 0, 0);
        wattron(win, COLOR_PAIR(colorPair > 0 ? colorPair : 2) | A_BOLD);
        mvwprintw(win, 1, std::max(2, (width - static_cast<int>(title.size())) / 2), "%s", title.c_str());
        wattroff(win, COLOR_PAIR(colorPair > 0 ? colorPair : 2) | A_BOLD);
        
        for (std::size_t i = 0; i < lines.size() && 3 + static_cast<int>(i) < height - 2; ++i) {
            int lineLen = static_cast<int>(lines[i].size());
            int lineWidth = width - 4;
            if (lineLen > lineWidth) {
                mvwprintw(win, 3 + static_cast<int>(i), 2, "%.*s", lineWidth, lines[i].c_str());
            } else {
                mvwprintw(win, 3 + static_cast<int>(i), 2, "%s", lines[i].c_str());
            }
        }
        mvwprintw(win, height - 2, 2, "Press any key to continue");
        wrefresh(win);
    };

    redrawMessage();

    while (true) {
        int ch = wgetch(win);
        if (ch == KEY_RESIZE) {
            resize_term(0, 0);
            endwin();
            refresh();
            redrawMessage();
            continue;
        }
        break;
    }
    delwin(win);
}

bool TerminalUI::confirm(const std::string &question) {
    WINDOW *win = nullptr;

    auto redrawConfirm = [&]() {
        if (win) {
            delwin(win);
        }
        clear();
        refresh();
        
        int width = static_cast<int>(question.size()) + 20;
        width = std::min(width, COLS - 8);
        width = std::max(width, 50);
        int height = 9;
        int starty = (LINES - height) / 2;
        int startx = (COLS - width) / 2;
        starty = std::max(0, starty);
        startx = std::max(0, startx);

        win = newwin(height, width, starty, startx);
        box(win, 0, 0);
        
        int qLen = static_cast<int>(question.size());
        int qWidth = width - 4;
        if (qLen > qWidth) {
            mvwprintw(win, 2, 2, "%.*s", qWidth, question.c_str());
        } else {
            mvwprintw(win, 2, 2, "%s", question.c_str());
        }
        mvwprintw(win, height - 2, 2, "Y Confirm | N Cancel");
        wrefresh(win);
    };

    redrawConfirm();

    while (true) {
        int ch = wgetch(win);
        if (ch == KEY_RESIZE) {
            resize_term(0, 0);
            endwin();
            refresh();
            redrawConfirm();
            continue;
        }
        if (ch == 'y' || ch == 'Y') {
            delwin(win);
            return true;
        }
        if (ch == 'n' || ch == 'N' || ch == 27) {
            delwin(win);
            return false;
        }
    }
}

int TerminalUI::list(const std::string &title,
                     const std::vector<std::string> &items,
                     const std::string &hint) {
    return menu(title, items, hint);
}

std::string TerminalUI::getCommand(const std::string &prompt) {
    echo();
    curs_set(1);
    
    int maxY, maxX;
    getmaxyx(stdscr, maxY, maxX);
    move(maxY - 1, 0);
    clrtoeol();
    printw("%s", prompt.c_str());
    refresh();
    
    char input[512] = {0};
    getnstr(input, 511);
    
    noecho();
    curs_set(0);
    
    return std::string(input);
}

void TerminalUI::initSplitScreen() {
    splitScreenMode_ = true;
    terminalScrollOffset_ = 0;
    terminalLines_.clear();
    
    int maxY, maxX;
    getmaxyx(stdscr, maxY, maxX);
    
    auto [terminalWidth, sidebarWidth] = computePaneWidths(maxX);
    
    if (terminalWin_) {
        delwin(terminalWin_);
    }
    if (sidebarWin_) {
        delwin(sidebarWin_);
    }
    
    terminalWin_ = newwin(maxY, terminalWidth, 0, 0);
    sidebarWin_ = newwin(maxY, sidebarWidth, 0, terminalWidth + 1);
    
    keypad(terminalWin_, TRUE);
    scrollok(terminalWin_, TRUE);
    idlok(terminalWin_, TRUE);
    
    refreshSplitScreen();
}

void TerminalUI::refreshSplitScreen() {
    if (!splitScreenMode_) return;
    
    int maxY, maxX;
    getmaxyx(stdscr, maxY, maxX);
    
    auto [terminalWidth, sidebarWidth] = computePaneWidths(maxX);
    
    if (terminalWin_) {
        wresize(terminalWin_, maxY, terminalWidth);
        mvwin(terminalWin_, 0, 0);
    }
    if (sidebarWin_) {
        wresize(sidebarWin_, maxY, sidebarWidth);
        mvwin(sidebarWin_, 0, terminalWidth + 1);
    }

    // Draw separator
    if (terminalWidth < maxX) {
        for (int y = 0; y < maxY; y++) {
            mvaddch(y, terminalWidth, '|');
        }
    }
    
    // Refresh terminal window
    if (terminalWin_) {
        wclear(terminalWin_);
        box(terminalWin_, 0, 0);
        mvwprintw(terminalWin_, 0, 2, " Terminal ");
        
        int winHeight, winWidth;
        getmaxyx(terminalWin_, winHeight, winWidth);
        int startLine = std::max(0, static_cast<int>(terminalLines_.size()) - (winHeight - 2) + terminalScrollOffset_);
        int endLine = startLine + (winHeight - 2);
        
        int y = 1;
        for (int i = startLine; i < endLine && i < static_cast<int>(terminalLines_.size()); i++) {
            std::string line = terminalLines_[i];
            if (static_cast<int>(line.size()) > winWidth - 2) {
                line = line.substr(0, winWidth - 2);
            }
            mvwprintw(terminalWin_, y++, 1, "%s", line.c_str());
        }
        
        wrefresh(terminalWin_);
    }
    
    // Refresh sidebar
    if (sidebarWin_) {
        wclear(sidebarWin_);
        box(sidebarWin_, 0, 0);
        wrefresh(sidebarWin_);
    }
    
    refresh();
}

void TerminalUI::drawSidebar(const std::vector<std::string> &content, const std::string &title) {
    if (!splitScreenMode_ || !sidebarWin_) return;
    
    wclear(sidebarWin_);
    box(sidebarWin_, 0, 0);
    mvwprintw(sidebarWin_, 0, 2, " %s ", title.c_str());
    
    int winHeight, winWidth;
    getmaxyx(sidebarWin_, winHeight, winWidth);
    
    int y = 1;
    for (size_t i = 0; i < content.size() && y < winHeight - 1; i++) {
        std::string line = content[i];
        if (static_cast<int>(line.size()) > winWidth - 2) {
            line = line.substr(0, winWidth - 2);
        }
        mvwprintw(sidebarWin_, y++, 1, "%s", line.c_str());
    }
    
    wrefresh(sidebarWin_);
}

void TerminalUI::addTerminalLine(const std::string &line) {
    terminalLines_.push_back(line);
    // Keep last 1000 lines
    if (terminalLines_.size() > 1000) {
        terminalLines_.erase(terminalLines_.begin());
    }
    terminalScrollOffset_ = 0;
    refreshSplitScreen();
}

void TerminalUI::scrollTerminal(int lines) {
    terminalScrollOffset_ += lines;
    int maxScroll = std::max(0, static_cast<int>(terminalLines_.size()) - 10);
    terminalScrollOffset_ = std::max(0, std::min(terminalScrollOffset_, maxScroll));
    refreshSplitScreen();
}

void TerminalUI::clearTerminal() {
    terminalLines_.clear();
    terminalScrollOffset_ = 0;
    refreshSplitScreen();
}

std::string TerminalUI::getTerminalCommand(const std::string &prompt) {
    if (!splitScreenMode_ || !terminalWin_) {
        return getCommand(prompt);
    }
    
    curs_set(1);
    noecho();
    
    int winHeight, winWidth;
    getmaxyx(terminalWin_, winHeight, winWidth);
    
    std::string input;

    auto redrawPrompt = [&]() {
        wmove(terminalWin_, winHeight - 1, 1);
        wclrtoeol(terminalWin_);
        std::string display = prompt + input;
        if (static_cast<int>(display.size()) > winWidth - 2) {
            display = display.substr(display.size() - (winWidth - 2));
        }
        wprintw(terminalWin_, "%s", display.c_str());
        wrefresh(terminalWin_);
    };

    redrawPrompt();

    while (true) {
        int ch = wgetch(terminalWin_);
        if (ch == KEY_RESIZE) {
            refreshSplitScreen();
            getmaxyx(terminalWin_, winHeight, winWidth);
            redrawPrompt();
            continue;
        }
        if (ch == KEY_PPAGE) {
            scrollTerminal(std::max(1, winHeight - 3));
            continue;
        }
        if (ch == KEY_NPAGE) {
            scrollTerminal(-std::max(1, winHeight - 3));
            continue;
        }
        if (ch == KEY_UP) {
            scrollTerminal(1);
            continue;
        }
        if (ch == KEY_DOWN) {
            scrollTerminal(-1);
            continue;
        }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (!input.empty()) {
                input.pop_back();
                redrawPrompt();
            }
            continue;
        }
        if (ch == '\n' || ch == KEY_ENTER) {
            break;
        }
        if (std::isprint(ch)) {
            input.push_back(static_cast<char>(ch));
            redrawPrompt();
        }
    }
    
    curs_set(0);
    
    addTerminalLine(prompt + input);
    
    return input;
}


