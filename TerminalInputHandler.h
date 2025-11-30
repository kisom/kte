/*
 * TerminalInputHandler - ncurses-based input handling for terminal mode
 */
#ifndef KTE_TERMINAL_INPUT_HANDLER_H
#define KTE_TERMINAL_INPUT_HANDLER_H

#include <cstdint>

#include "InputHandler.h"


class TerminalInputHandler final : public InputHandler {
public:
	TerminalInputHandler();

	~TerminalInputHandler() override;

	bool Poll(MappedInput &out) override;

private:
	bool decode_(MappedInput &out);

	// ke-style prefix state
	bool k_prefix_ = false; // true after C-k until next key or ESC
	// Simple meta (ESC) state for ESC sequences like ESC b/f
	bool esc_meta_ = false;
};

#endif // KTE_TERMINAL_INPUT_HANDLER_H
