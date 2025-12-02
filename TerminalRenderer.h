/*
 * TerminalRenderer - ncurses-based renderer for terminal mode
 */
#ifndef KTE_TERMINAL_RENDERER_H
#define KTE_TERMINAL_RENDERER_H

#include "Renderer.h"


class TerminalRenderer final : public Renderer {
public:
	TerminalRenderer();

	~TerminalRenderer() override;

	void Draw(Editor &ed) override;

	// Enable/disable UTF-8 aware rendering (set by TerminalFrontend after locale init)
	void SetUtf8Enabled(bool on)
	{
		utf8_enabled_ = on;
	}


	[[nodiscard]] bool Utf8Enabled() const
	{
		return utf8_enabled_;
	}

private:
	bool utf8_enabled_ = true;
};

#endif // KTE_TERMINAL_RENDERER_H