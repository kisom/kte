/*
 * GUIInputHandler - ImGui/SDL2-based input mapping for GUI mode
 */
#pragma once
#include <mutex>
#include <queue>

#include "InputHandler.h"


union SDL_Event; // fwd decl to avoid including SDL here (SDL defines SDL_Event as a union)

class GUIInputHandler final : public InputHandler {
public:
	GUIInputHandler() = default;

	~GUIInputHandler() override = default;


	void Attach(Editor *ed) override
	{
		ed_ = ed;
	}


	// Translate an SDL event to editor command and enqueue if applicable.
	// Returns true if it produced a mapped command or consumed input.
	bool ProcessSDLEvent(const SDL_Event &e);

	bool Poll(MappedInput &out) override;

private:
	std::mutex mu_;
	std::queue<MappedInput> q_;
	bool k_prefix_       = false;
	bool k_ctrl_pending_ = false; // if true, next k-suffix is treated as Ctrl- (qualifier via literal 'C' or '^')
	// Treat ESC as a Meta prefix: next key is looked up via ESC keymap
	bool esc_meta_ = false;
	// When a printable keydown generated a non-text command, suppress the very next SDL_TEXTINPUT
	// event produced by SDL for the same keystroke to avoid inserting stray characters.
	bool suppress_text_input_once_ = false;

	Editor *ed_ = nullptr; // attached editor for editor-owned uarg handling
};