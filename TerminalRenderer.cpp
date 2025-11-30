#include "TerminalRenderer.h"

#include <ncurses.h>
#include <cstdio>

#include "Editor.h"
#include "Buffer.h"

TerminalRenderer::TerminalRenderer() = default;

TerminalRenderer::~TerminalRenderer() = default;

void TerminalRenderer::Draw(const Editor &ed)
{
    // Determine terminal size and keep editor dimensions in sync
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // Clear screen
    erase();

    const Buffer *buf = ed.CurrentBuffer();
    int content_rows   = rows - 1; // last line is status

    if (buf) {
        const auto &lines = buf->Rows();
        std::size_t rowoffs = buf->Rowoffs();
        std::size_t coloffs = buf->Coloffs();

        for (int r = 0; r < content_rows; ++r) {
            int y = r;
            int x = 0;
            move(y, x);
            std::size_t li = rowoffs + static_cast<std::size_t>(r);
            if (li < lines.size()) {
                const std::string &line = lines[li];
                if (coloffs < line.size()) {
                    // Render a windowed slice of the line
                    std::size_t len = std::min<std::size_t>(static_cast<std::size_t>(cols), line.size() - coloffs);
                    addnstr(line.c_str() + static_cast<long>(coloffs), static_cast<int>(len));
                }
            }
            clrtoeol();
        }

        // Place cursor (best-effort; tabs etc. not handled yet)
        std::size_t cy = buf->Cury();
        std::size_t cx = buf->Curx();
        int cur_y      = static_cast<int>(cy - buf->Rowoffs());
        int cur_x      = static_cast<int>(cx - buf->Coloffs());
        if (cur_y >= 0 && cur_y < content_rows && cur_x >= 0 && cur_x < cols) {
            move(cur_y, cur_x);
        }
    } else {
        mvaddstr(0, 0, "[no buffer]");
    }

    // Status line (inverse)
    move(rows - 1, 0);
    attron(A_REVERSE);
    char status[1024];
    const char *fname = (buf && buf->IsFileBacked()) ? buf->Filename().c_str() : "(new)";
    int dirty = (buf && buf->Dirty()) ? 1 : 0;
    snprintf(status, sizeof(status), " %s%s  %zux%zu  %s ",
             fname,
             dirty ? "*" : "",
             ed.Rows(), ed.Cols(),
             ed.Status().c_str());
    addnstr(status, cols);
    clrtoeol();
    attroff(A_REVERSE);

    refresh();
}
