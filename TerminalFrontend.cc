#include <unistd.h>
#include <termios.h>
#include <ncurses.h>

#include "Editor.h"
#include "Command.h"
#include "TerminalFrontend.h"


bool
TerminalFrontend::Init(Editor &ed)
{
	// Ensure Control keys reach the app: disable XON/XOFF and dsusp/susp bindings (e.g., ^S/^Q, ^Y on macOS)
	{
		struct termios tio{};
		if (tcgetattr(STDIN_FILENO, &tio) == 0) {
			// Save original to restore on shutdown
			orig_tio_      = tio;
			have_orig_tio_ = true;
			// Disable software flow control so C-s/C-q work
			tio.c_iflag &= static_cast<unsigned long>(~IXON);
#ifdef IXOFF
			tio.c_iflag &= static_cast<unsigned long>(~IXOFF);
#endif
			// Disable dsusp/susp characters so C-y (VDSUSP on macOS) and C-z don't signal-stop the app
#ifdef _POSIX_VDISABLE
#ifdef VSUSP
			tio.c_cc[VSUSP] = _POSIX_VDISABLE;
#endif
#ifdef VDSUSP
			tio.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
#endif
			(void) tcsetattr(STDIN_FILENO, TCSANOW, &tio);
		}
	}
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	// Enable 8-bit meta key sequences (Alt/ESC-prefix handling in terminals)
	meta(stdscr, TRUE);
	// Make ESC key sequences resolve quickly so ESC+<key> works as meta
#ifdef set_escdelay
	set_escdelay(50);
#endif
	nodelay(stdscr, TRUE);
	curs_set(1);
	// Enable mouse support if available
	mouseinterval(0);
	mousemask(ALL_MOUSE_EVENTS, nullptr);

	int r = 0, c = 0;
	getmaxyx(stdscr, r, c);
	prev_r_ = r;
	prev_c_ = c;
	ed.SetDimensions(static_cast<std::size_t>(r), static_cast<std::size_t>(c));
	return true;
}


void
TerminalFrontend::Step(Editor &ed, bool &running)
{
	// Handle resize and keep editor dimensions synced
	int r, c;
	getmaxyx(stdscr, r, c);
	if (r != prev_r_ || c != prev_c_) {
		resizeterm(r, c);
		clear();
		prev_r_ = r;
		prev_c_ = c;
	}
	ed.SetDimensions(static_cast<std::size_t>(r), static_cast<std::size_t>(c));

	MappedInput mi;
	if (input_.Poll(mi)) {
		if (mi.hasCommand) {
			Execute(ed, mi.id, mi.arg, mi.count);
		}
	} else {
		// Avoid busy loop
		usleep(1000);
	}

	if (ed.QuitRequested()) {
		running = false;
	}

	renderer_.Draw(ed);
}


void
TerminalFrontend::Shutdown()
{
	// Restore original terminal settings if we changed them
	if (have_orig_tio_) {
		(void) tcsetattr(STDIN_FILENO, TCSANOW, &orig_tio_);
		have_orig_tio_ = false;
	}
	endwin();
}
