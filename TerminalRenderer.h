/*
 * TerminalRenderer - ncurses-based renderer for terminal mode
 */
#pragma once
#include "Renderer.h"


class TerminalRenderer final : public Renderer {
public:
	TerminalRenderer();

	~TerminalRenderer() override;

	void Draw(Editor &ed) override;
};