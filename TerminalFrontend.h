/*
 * TerminalFrontend - couples TerminalInputHandler + TerminalRenderer and owns ncurses lifecycle
 */
#pragma once
#include <termios.h>
#include <signal.h>

#include "Frontend.h"
#include "TerminalInputHandler.h"
#include "TerminalRenderer.h"


class TerminalFrontend final : public Frontend {
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
	// Saved terminal attributes to restore on shutdown
	bool have_orig_tio_ = false;
	struct termios orig_tio_{};
	// Saved SIGINT handler to restore on shutdown
	bool have_old_sigint_ = false;
	struct sigaction old_sigint_{};
};