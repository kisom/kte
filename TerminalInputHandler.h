/*
 * TerminalInputHandler - ncurses-based input handling for terminal mode
 */
#ifndef KTE_TERMINAL_INPUT_HANDLER_H
#define KTE_TERMINAL_INPUT_HANDLER_H

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

	// Universal argument (C-u) state
	bool uarg_active_     = false; // an argument is pending for the next command
	bool uarg_collecting_ = false; // collecting digits / '-' right now
	bool uarg_negative_   = false; // whether a leading '-' was supplied
	bool uarg_had_digits_ = false; // whether any digits were supplied
	int uarg_value_       = 0; // current absolute value (>=0)
	std::string uarg_text_; // raw digits/minus typed for status display
};

#endif // KTE_TERMINAL_INPUT_HANDLER_H
