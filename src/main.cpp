#include "gitlite_app.hpp"

#include <exception>
#include <iostream>
#include <ncurses.h>

int main() {
    try {
        GitLiteApp app;
        app.run();
    } catch (const std::exception &ex) {
        endwin();
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}


