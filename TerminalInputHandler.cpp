#include "TerminalInputHandler.h"

#include <ncurses.h>

namespace {
constexpr int CTRL(char c) { return c & 0x1F; }
}

TerminalInputHandler::TerminalInputHandler() = default;

TerminalInputHandler::~TerminalInputHandler() = default;

static bool map_key_to_command(int ch, bool &k_prefix, MappedInput &out)
{
    // Handle special keys from ncurses
    switch (ch) {
        case KEY_LEFT:  out = {true, CommandId::MoveLeft,  "", 0}; return true;
        case KEY_RIGHT: out = {true, CommandId::MoveRight, "", 0}; return true;
        case KEY_UP:    out = {true, CommandId::MoveUp,    "", 0}; return true;
        case KEY_DOWN:  out = {true, CommandId::MoveDown,  "", 0}; return true;
        case KEY_HOME:  out = {true, CommandId::MoveHome,  "", 0}; return true;
        case KEY_END:   out = {true, CommandId::MoveEnd,   "", 0}; return true;
        case KEY_DC:    out = {true, CommandId::DeleteChar,"", 0}; return true;
        case KEY_RESIZE: out = {true, CommandId::Refresh,  "", 0}; return true;
        default: break;
    }

    // ESC as cancel of prefix; many terminals send meta sequences as ESC+...
    if (ch == 27) { // ESC
        k_prefix = false;
        out = {true, CommandId::Refresh, "", 0};
        return true;
    }

    // Control keys
    if (ch == CTRL('K')) { // C-k prefix
        k_prefix = true;
        out = {true, CommandId::Refresh, "", 0};
        return true;
    }
    if (ch == CTRL('G')) { // cancel
        k_prefix = false;
        out = {true, CommandId::Refresh, "", 0};
        return true;
    }
    if (ch == CTRL('L')) { out = {true, CommandId::Refresh,   "", 0}; return true; }
    if (ch == CTRL('S')) { out = {true, CommandId::FindStart, "", 0}; return true; }

    // Enter
    if (ch == '\n' || ch == '\r') { k_prefix = false; out = {true, CommandId::Newline,   "", 0}; return true; }
    // Backspace in ncurses can be KEY_BACKSPACE or 127
    if (ch == KEY_BACKSPACE || ch == 127 || ch == CTRL('H')) { k_prefix = false; out = {true, CommandId::Backspace, "", 0}; return true; }

    if (k_prefix) {
        k_prefix = false; // single next key only
        switch (ch) {
            case 's': case 'S': out = {true, CommandId::Save,        "", 0}; return true;
            case 'x': case 'X': out = {true, CommandId::SaveAndQuit, "", 0}; return true;
            case 'q': case 'Q': out = {true, CommandId::Quit,        "", 0}; return true;
            default: break;
        }
        if (ch == CTRL('Q')) { out = {true, CommandId::Quit, "", 0}; return true; }
        out.hasCommand = false; // unknown chord
        return true;
    }

    // Printable ASCII
    if (ch >= 0x20 && ch <= 0x7E) {
        out.hasCommand = true;
        out.id = CommandId::InsertText;
        out.arg.assign(1, static_cast<char>(ch));
        out.count = 0;
        return true;
    }

    out.hasCommand = false;
    return true; // consumed a key
}

bool TerminalInputHandler::decode_(MappedInput &out)
{
    int ch = getch();
    if (ch == ERR) {
        return false; // no input
    }
    return map_key_to_command(ch, k_prefix_, out);
}

bool TerminalInputHandler::Poll(MappedInput &out)
{
    out = {};
    return decode_(out) && out.hasCommand;
}
