#include <cstdio>
#include <ncurses.h>

#include "TerminalInputHandler.h"
#include "KKeymap.h"
#include "Editor.h"

namespace {
constexpr int
CTRL(char c)
{
	return c & 0x1F;
}
}

TerminalInputHandler::TerminalInputHandler() = default;

TerminalInputHandler::~TerminalInputHandler() = default;


static bool
map_key_to_command(const int ch,
                   bool &k_prefix,
                   bool &esc_meta,
                   bool &k_ctrl_pending,
                   Editor *ed,
                   MappedInput &out)
{
	// Handle special keys from ncurses
	// These keys exit k-prefix mode if active (user pressed C-k then a special key).
	switch (ch) {
	case KEY_ENTER:
		// Some terminals send KEY_ENTER distinct from '\n'/'\r'
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::Newline, "", 0};
		return true;
	case KEY_MOUSE: {
		k_prefix       = false;
		k_ctrl_pending = false;
		MEVENT ev{};
		if (getmouse(&ev) == OK) {
			// Mouse wheel → scroll viewport without moving cursor
#ifdef BUTTON4_PRESSED
			if (ev.bstate & (BUTTON4_PRESSED | BUTTON4_RELEASED | BUTTON4_CLICKED)) {
				out = {true, CommandId::ScrollUp, "", 0};
				return true;
			}
#endif
#ifdef BUTTON5_PRESSED
			if (ev.bstate & (BUTTON5_PRESSED | BUTTON5_RELEASED | BUTTON5_CLICKED)) {
				out = {true, CommandId::ScrollDown, "", 0};
				return true;
			}
#endif
			// React to left button click/press
			if (ev.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON1_RELEASED)) {
				char buf[64];
				// Use screen coordinates; command handler will translate via offsets
				std::snprintf(buf, sizeof(buf), "@%d:%d", ev.y, ev.x);
				out = {true, CommandId::MoveCursorTo, std::string(buf), 0};
				return true;
			}
		}
		// No actionable mouse event
		out.hasCommand = false;
		return true;
	}
	case KEY_LEFT:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveLeft, "", 0};
		return true;
	case KEY_RIGHT:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveRight, "", 0};
		return true;
	case KEY_UP:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveUp, "", 0};
		return true;
	case KEY_DOWN:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveDown, "", 0};
		return true;
	case KEY_HOME:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveHome, "", 0};
		return true;
	case KEY_END:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveEnd, "", 0};
		return true;
	case KEY_PPAGE:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::PageUp, "", 0};
		return true;
	case KEY_NPAGE:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::PageDown, "", 0};
		return true;
	case KEY_DC:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::DeleteChar, "", 0};
		return true;
	case KEY_RESIZE:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::Refresh, "", 0};
		return true;
	default:
		break;
	}

	// ESC as cancel of prefix; many terminals send meta sequences as ESC+...
	if (ch == 27) {
		// ESC
		k_prefix       = false;
		k_ctrl_pending = false;
		esc_meta       = true; // next key will be considered meta-modified
		out.hasCommand = false; // no command yet
		return true;
	}

	// Control keys
	if (ch == CTRL('K')) {
		// C-k prefix
		k_prefix       = true;
		k_ctrl_pending = false;
		out            = {true, CommandId::KPrefix, "", 0};
		return true;
	}
	if (ch == CTRL('G')) {
		// cancel
		k_prefix       = false;
		k_ctrl_pending = false;
		esc_meta       = false;
		// cancel universal argument as well
		if (ed)
			ed->UArgClear();
		out = {true, CommandId::Refresh, "", 0};
		return true;
	}
	// Universal argument: C-u
	if (ch == CTRL('U')) {
		if (ed)
			ed->UArgStart();
		out.hasCommand = false; // C-u itself doesn't issue a command
		return true;
	}
	// Tab (note: terminals encode Tab and C-i as the same code 9)
	if (ch == '\t') {
		k_prefix       = false;
		k_ctrl_pending = false;
		out.hasCommand = true;
		out.id         = CommandId::InsertText;
		out.arg        = "\t";
		out.count      = 0;
		return true;
	}
	// Generic Control-chord lookup (after handling special prefixes/cancel)
	// IMPORTANT: if we're in k-prefix, the very next key must be interpreted
	// via the C-k keymap first, even if it's a Control chord like C-d.
	if (k_prefix) {
		// In k-prefix: allow a control qualifier via literal 'C' or '^'
		// Detect Control keycodes first
		bool ctrl     = false;
		int ascii_key = ch;
		if (ch >= 1 && ch <= 26) {
			ctrl      = true;
			ascii_key = 'a' + (ch - 1);
		}
		// If user typed literal 'C'/'c' or '^' as a qualifier, keep k-prefix and set pending
		if (ascii_key == 'C' || ascii_key == 'c' || ascii_key == '^') {
			k_ctrl_pending = true;
			if (ed)
				ed->SetStatus("C-k C _");
			out.hasCommand = false;
			return true;
		}
		// For actual suffix, consume the k-prefix
		k_prefix = false;
		// Do NOT lowercase here; KLookupKCommand handles case-sensitive bindings
		CommandId id;
		bool pass_ctrl = (ctrl || k_ctrl_pending);
		k_ctrl_pending = false;
		if (KLookupKCommand(ascii_key, pass_ctrl, id)) {
			out = {true, id, "", 0};
			if (ed)
				ed->SetStatus(""); // clear "C-k _" hint after suffix
		} else {
			int shown = KLowerAscii(ascii_key);
			char c    = (shown >= 0x20 && shown <= 0x7e) ? static_cast<char>(shown) : '?';
			std::string arg(1, c);
			out = {true, CommandId::UnknownKCommand, arg, 0};
			if (ed)
				ed->SetStatus(""); // clear hint; handler will set unknown status
		}
		return true;
	}

	if (ch >= 1 && ch <= 26) {
		int ascii_key = 'a' + (ch - 1);
		CommandId id;
		if (KLookupCtrlCommand(ascii_key, id)) {
			out = {true, id, "", 0};
			return true;
		}
	}

	// Enter
	if (ch == '\n' || ch == '\r') {
		k_prefix       = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::Newline, "", 0};
		return true;
	}
	// If previous key was ESC, interpret as meta and use ESC keymap
	if (esc_meta) {
		esc_meta      = false;
		int ascii_key = ch;
		// Handle ESC + BACKSPACE (meta-backspace, Alt-Backspace)
		if (ch == KEY_BACKSPACE || ch == 127 || ch == CTRL('H')) {
			ascii_key = KEY_BACKSPACE; // normalized value for lookup
		} else if (ch == ',') {
			// Some terminals emit ',' when Shift state is lost after ESC; treat as '<'
			ascii_key = '<';
		} else if (ch == '.') {
			// Likewise, map '.' to '>'
			ascii_key = '>';
		} else if (ascii_key >= 'A' && ascii_key <= 'Z') {
			ascii_key = ascii_key - 'A' + 'a';
		}
		CommandId id;
		if (KLookupEscCommand(ascii_key, id)) {
			out = {true, id, "", 0};
			return true;
		}
		// Unhandled ESC sequence: exit escape mode and show status
		out = {true, CommandId::UnknownEscCommand, "", 0};
		return true;
	}

	// Backspace in ncurses can be KEY_BACKSPACE or 127
	if (ch == KEY_BACKSPACE || ch == 127 || ch == CTRL('H')) {
		k_prefix       = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::Backspace, "", 0};
		return true;
	}

	// k_prefix handled earlier

	// If universal argument is active at editor level and we get a digit, feed it
	if (ed && ed->UArg() != 0 && ch >= '0' && ch <= '9') {
		ed->UArgDigit(ch - '0');
		out.hasCommand = false; // keep collecting, no command yet
		return true;
	}

	// Printable ASCII
	if (ch >= 0x20 && ch <= 0x7E) {
		out.hasCommand = true;
		out.id         = CommandId::InsertText;
		out.arg.assign(1, static_cast<char>(ch));
		out.count = 0;
		return true;
	}

	out.hasCommand = false;
	return true; // consumed a key
}


bool
TerminalInputHandler::decode_(MappedInput &out)
{
	int ch = getch();
	if (ch == ERR) {
		return false; // no input
	}
	bool consumed = map_key_to_command(
		ch,
		k_prefix_, esc_meta_,
		k_ctrl_pending_,
		ed_,
		out);
	if (!consumed)
		return false;
	return true;
}


bool
TerminalInputHandler::Poll(MappedInput &out)
{
	out = {};
	return decode_(out) && out.hasCommand;
}