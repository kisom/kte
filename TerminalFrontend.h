/*
 * TerminalFrontend - couples TerminalInputHandler + TerminalRenderer and owns ncurses lifecycle
 */
#ifndef KTE_TERMINAL_FRONTEND_H
#define KTE_TERMINAL_FRONTEND_H

#include "Frontend.h"
#include "TerminalInputHandler.h"
#include "TerminalRenderer.h"

class TerminalFrontend : public Frontend {
public:
    TerminalFrontend() = default;
    ~TerminalFrontend() override = default;

    bool Init(Editor &ed) override;
    void Step(Editor &ed, bool &running) override;
    void Shutdown() override;

private:
    TerminalInputHandler input_{};
    TerminalRenderer renderer_{};
    int prev_r_ = 0;
    int prev_c_ = 0;
};

#endif // KTE_TERMINAL_FRONTEND_H
