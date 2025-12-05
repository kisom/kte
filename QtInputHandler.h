/*
 * QtInputHandler - Qt-based input mapping for GUI mode
 */
#pragma once

#include <mutex>
#include <queue>

#include "InputHandler.h"

class QKeyEvent;

class QtInputHandler final : public InputHandler {
public:
	QtInputHandler() = default;

	~QtInputHandler() override = default;


	void Attach(Editor *ed) override
	{
		ed_ = ed;
	}


	// Translate a Qt key event to editor command and enqueue if applicable.
	// Returns true if it produced a mapped command or consumed input.
	bool ProcessKeyEvent(const QKeyEvent &e);

	bool Poll(MappedInput &out) override;

private:
	std::mutex mu_;
	std::queue<MappedInput> q_;
	bool k_prefix_                 = false;
	bool k_ctrl_pending_           = false; // C-k C-… qualifier
	bool esc_meta_                 = false; // ESC-prefix for next key
	bool suppress_text_input_once_ = false; // reserved (Qt sends text in keyPressEvent)
	Editor *ed_                    = nullptr;
};