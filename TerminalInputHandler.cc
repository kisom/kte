#include <cstdio>
#include <ncurses.h>

#include "TerminalInputHandler.h"
#include "KKeymap.h"

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
                   // universal-argument state (by ref)
                   bool &uarg_active,
                   bool &uarg_collecting,
                   bool &uarg_negative,
                   bool &uarg_had_digits,
                   int &uarg_value,
                   std::string &uarg_text,
                   MappedInput &out)
{
	// Handle special keys from ncurses
	switch (ch) {
	case KEY_MOUSE: {
		MEVENT ev{};
		if (getmouse(&ev) == OK) {
			// Mouse wheel → map to MoveUp/MoveDown one line per wheel notch
#ifdef BUTTON4_PRESSED
			if (ev.bstate & (BUTTON4_PRESSED | BUTTON4_RELEASED | BUTTON4_CLICKED)) {
				out = {true, CommandId::MoveUp, "", 0};
				return true;
			}
#endif
#ifdef BUTTON5_PRESSED
			if (ev.bstate & (BUTTON5_PRESSED | BUTTON5_RELEASED | BUTTON5_CLICKED)) {
				out = {true, CommandId::MoveDown, "", 0};
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
		out = {true, CommandId::MoveLeft, "", 0};
		return true;
	case KEY_RIGHT:
		out = {true, CommandId::MoveRight, "", 0};
		return true;
	case KEY_UP:
		out = {true, CommandId::MoveUp, "", 0};
		return true;
	case KEY_DOWN:
		out = {true, CommandId::MoveDown, "", 0};
		return true;
	case KEY_HOME:
		out = {true, CommandId::MoveHome, "", 0};
		return true;
	case KEY_END:
		out = {true, CommandId::MoveEnd, "", 0};
		return true;
	case KEY_PPAGE:
		out = {true, CommandId::PageUp, "", 0};
		return true;
	case KEY_NPAGE:
		out = {true, CommandId::PageDown, "", 0};
		return true;
	case KEY_DC:
		out = {true, CommandId::DeleteChar, "", 0};
		return true;
	case KEY_RESIZE:
		out = {true, CommandId::Refresh, "", 0};
		return true;
	default:
		break;
	}

	// ESC as cancel of prefix; many terminals send meta sequences as ESC+...
	if (ch == 27) {
		// ESC
		k_prefix       = false;
		esc_meta       = true; // next key will be considered meta-modified
		out.hasCommand = false; // no command yet
		return true;
	}

	// Control keys
	if (ch == CTRL('K')) {
		// C-k prefix
		k_prefix = true;
		out      = {true, CommandId::KPrefix, "", 0};
		return true;
	}
	if (ch == CTRL('G')) {
		// cancel
		k_prefix = false;
		esc_meta = false;
		// cancel universal argument as well
		uarg_active     = false;
		uarg_collecting = false;
		uarg_negative   = false;
		uarg_had_digits = false;
		uarg_value      = 0;
		uarg_text.clear();
		out = {true, CommandId::Refresh, "", 0};
		return true;
	}
	// Universal argument: C-u
	if (ch == CTRL('U')) {
		// Start or extend universal argument
		if (!uarg_active) {
			uarg_active     = true;
			uarg_collecting = true;
			uarg_negative   = false;
			uarg_had_digits = false;
			uarg_value      = 4; // default
			// Reset collected text and emit status update
			uarg_text.clear();
			out = {true, CommandId::UArgStatus, uarg_text, 0};
			return true;
		} else if (uarg_collecting && !uarg_had_digits && !uarg_negative) {
			// Bare repeated C-u multiplies by 4
			if (uarg_value <= 0)
				uarg_value = 4;
			else
				uarg_value *= 4;
			// Keep showing status (no digits yet)
			out = {true, CommandId::UArgStatus, uarg_text, 0};
			return true;
		} else {
			// If digits or '-' have been entered, C-u ends the argument (ready for next command)
			uarg_collecting = false;
			if (!uarg_had_digits && !uarg_negative && uarg_value <= 0)
				uarg_value = 4;
		}
		// No command produced by C-u itself
		out.hasCommand = false;
		return true;
	}
	// Tab (note: terminals encode Tab and C-i as the same code 9)
	if (ch == '\t') {
		k_prefix       = false;
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
		k_prefix      = false; // consume the prefix for this one key
		bool ctrl     = false;
		int ascii_key = ch;
		if (ch >= 1 && ch <= 26) {
			ctrl      = true;
			ascii_key = 'a' + (ch - 1);
		}
		// Do NOT lowercase here; KLookupKCommand handles case-sensitive bindings
		CommandId id;
		if (KLookupKCommand(ascii_key, ctrl, id)) {
			out = {true, id, "", 0};
		} else {
			int shown = KLowerAscii(ascii_key);
			char c    = (shown >= 0x20 && shown <= 0x7e) ? static_cast<char>(shown) : '?';
			std::string arg(1, c);
			out = {true, CommandId::UnknownKCommand, arg, 0};
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
		k_prefix = false;
		out      = {true, CommandId::Newline, "", 0};
		return true;
	}
	// If previous key was ESC, interpret as meta and use ESC keymap
	if (esc_meta) {
		esc_meta      = false;
		int ascii_key = ch;
		// Handle ESC + BACKSPACE (meta-backspace, Alt-Backspace)
		if (ch == KEY_BACKSPACE || ch == 127 || ch == CTRL('H')) {
			ascii_key = KEY_BACKSPACE; // normalized value for lookup
		} else if (ascii_key >= 'A' && ascii_key <= 'Z') {
			ascii_key = ascii_key - 'A' + 'a';
		}
		CommandId id;
		if (KLookupEscCommand(ascii_key, id)) {
			out = {true, id, "", 0};
			return true;
		}
		// Unhandled meta key: no command
		out.hasCommand = false;
		return true;
	}

	// Backspace in ncurses can be KEY_BACKSPACE or 127
	if (ch == KEY_BACKSPACE || ch == 127 || ch == CTRL('H')) {
		k_prefix = false;
		out      = {true, CommandId::Backspace, "", 0};
		return true;
	}

	// k_prefix handled earlier

	// If collecting universal arg, handle digits and optional leading '-'
	if (uarg_active && uarg_collecting) {
		if (ch >= '0' && ch <= '9') {
			int d = ch - '0';
			if (!uarg_had_digits) {
				// First digit overrides any 4^n default
				uarg_value      = 0;
				uarg_had_digits = true;
			}
			if (uarg_value < 100000000) {
				// avoid overflow
				uarg_value = uarg_value * 10 + d;
			}
			// Update raw text and status to reflect collected digits
			uarg_text.push_back(static_cast<char>(ch));
			out = {true, CommandId::UArgStatus, uarg_text, 0};
			return true;
		}
		if (ch == '-' && !uarg_had_digits && !uarg_negative) {
			uarg_negative = true;
			// Show leading minus in status
			uarg_text = "-";
			out       = {true, CommandId::UArgStatus, uarg_text, 0};
			return true;
		}
		// Any other key will be processed as a command; fall through to mapping below
		// but mark collection finished so we apply the argument to that command
		uarg_collecting = false;
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
		uarg_active_, uarg_collecting_, uarg_negative_, uarg_had_digits_, uarg_value_, uarg_text_,
		out);
	if (!consumed)
		return false;
	// If a command was produced and a universal argument is active, attach it and clear state
	if (out.hasCommand && uarg_active_ && out.id != CommandId::UArgStatus) {
		int count = 0;
		if (!uarg_had_digits_ && !uarg_negative_) {
			// No explicit digits: use current value (default 4 or 4^n)
			count = (uarg_value_ > 0) ? uarg_value_ : 4;
		} else {
			count = uarg_value_;
			if (uarg_negative_)
				count = -count;
		}
		out.count = count;
		// Clear state
		uarg_active_     = false;
		uarg_collecting_ = false;
		uarg_negative_   = false;
		uarg_had_digits_ = false;
		uarg_value_      = 0;
	}
	return true;
}


bool
TerminalInputHandler::Poll(MappedInput &out)
{
	out = {};
	return decode_(out) && out.hasCommand;
}
