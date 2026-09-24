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

	// Read one key if available. Returns false when no input was read; true
	// when a key was consumed (out.hasCommand says whether it produced a
	// command: prefixes such as C-k or ESC do not).
	bool PollKey(MappedInput &out)
	{
		out = {};
		return decode_(out);
	}

private:
	bool decode_(MappedInput &out);

	// ke-style prefix state
	bool k_prefix_ = false; // true after C-k until next key or ESC
	// Optional control qualifier inside k-prefix (e.g., user typed literal 'C' or '^')
	bool k_ctrl_pending_ = false;
	// Simple meta (ESC) state for ESC sequences like ESC b/f
	bool esc_meta_ = false;

	// Mouse drag selection state
	bool mouse_selecting_ = false;

	Editor *ed_ = nullptr; // attached editor for uarg handling
};
