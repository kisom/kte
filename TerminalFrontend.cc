#include <unistd.h>
#include <termios.h>
#include <ncurses.h>

#include "Editor.h"
#include "Command.h"
#include "TerminalFrontend.h"


bool
TerminalFrontend::Init(Editor &ed)
{
	// Ensure Ctrl-S/Ctrl-Q reach the application by disabling XON/XOFF flow control
	{
		struct termios tio{};
		if (tcgetattr(STDIN_FILENO, &tio) == 0) {
			tio.c_iflag &= static_cast<unsigned long>(~IXON);
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
	endwin();
}
