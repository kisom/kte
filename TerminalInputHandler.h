/*
 * TerminalInputHandler - ncurses-based input handling for terminal mode
 */
#pragma once
#include "InputHandler.h"


class TerminalInputHandler final : public InputHandler {
public:
	TerminalInputHandler();

	~TerminalInputHandler() override;


	void Attach(Editor *ed) override
	{
		ed_ = ed;
	}


	bool Poll(MappedInput &out) override;

private:
	bool decode_(MappedInput &out);

	// ke-style prefix state
	bool k_prefix_ = false; // true after C-k until next key or ESC
	// Optional control qualifier inside k-prefix (e.g., user typed literal 'C' or '^')
	bool k_ctrl_pending_ = false;
	// Simple meta (ESC) state for ESC sequences like ESC b/f
	bool esc_meta_ = false;

	Editor *ed_ = nullptr; // attached editor for uarg handling
};