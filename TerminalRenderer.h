/*
 * TerminalRenderer - ncurses-based renderer for terminal mode
 */
#ifndef KTE_TERMINAL_RENDERER_H
#define KTE_TERMINAL_RENDERER_H

#include "Renderer.h"

class TerminalRenderer : public Renderer {
public:
    TerminalRenderer();
    ~TerminalRenderer() override;

    void Draw(const Editor &ed) override;
};

#endif // KTE_TERMINAL_RENDERER_H
