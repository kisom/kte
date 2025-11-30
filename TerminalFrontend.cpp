#include "TerminalFrontend.h"

#include <unistd.h>
#include <ncurses.h>

#include "Editor.h"
#include "Command.h"

bool TerminalFrontend::Init(Editor &ed)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(1);

    int r = 0, c = 0;
    getmaxyx(stdscr, r, c);
    prev_r_ = r; prev_c_ = c;
    ed.SetDimensions(static_cast<std::size_t>(r), static_cast<std::size_t>(c));
    return true;
}

void TerminalFrontend::Step(Editor &ed, bool &running)
{
    // Handle resize and keep editor dimensions synced
    int r, c;
    getmaxyx(stdscr, r, c);
    if (r != prev_r_ || c != prev_c_) {
        resizeterm(r, c);
        clear();
        prev_r_ = r; prev_c_ = c;
    }
    ed.SetDimensions(static_cast<std::size_t>(r), static_cast<std::size_t>(c));

    MappedInput mi;
    if (input_.Poll(mi)) {
        if (mi.hasCommand) {
            Execute(ed, mi.id, mi.arg, mi.count);
            if (mi.id == CommandId::Quit || mi.id == CommandId::SaveAndQuit) {
                running = false;
            }
        }
    } else {
        // Avoid busy loop
        usleep(1000);
    }

    renderer_.Draw(ed);
}

void TerminalFrontend::Shutdown()
{
    endwin();
}
