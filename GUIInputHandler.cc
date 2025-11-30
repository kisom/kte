#include <SDL.h>

#include "GUIInputHandler.h"
#include "KKeymap.h"


static bool
map_key(const SDL_Keycode key, const SDL_Keymod mod, bool &k_prefix, MappedInput &out)
{
	// Ctrl handling
	const bool is_ctrl = (mod & KMOD_CTRL) != 0;
	const bool is_alt  = (mod & (KMOD_ALT | KMOD_LALT | KMOD_RALT)) != 0;

	// Movement and basic keys
	switch (key) {
	case SDLK_LEFT:
		out = {true, CommandId::MoveLeft, "", 0};
		return true;
	case SDLK_RIGHT:
		out = {true, CommandId::MoveRight, "", 0};
		return true;
	case SDLK_UP:
		out = {true, CommandId::MoveUp, "", 0};
		return true;
	case SDLK_DOWN:
		out = {true, CommandId::MoveDown, "", 0};
		return true;
	case SDLK_HOME:
		out = {true, CommandId::MoveHome, "", 0};
		return true;
	case SDLK_END:
		out = {true, CommandId::MoveEnd, "", 0};
		return true;
	case SDLK_PAGEUP:
		out = {true, CommandId::PageUp, "", 0};
		return true;
	case SDLK_PAGEDOWN:
		out = {true, CommandId::PageDown, "", 0};
		return true;
	case SDLK_DELETE:
		out = {true, CommandId::DeleteChar, "", 0};
		return true;
	case SDLK_BACKSPACE:
		out = {true, CommandId::Backspace, "", 0};
		return true;
	case SDLK_TAB:
		// Insert a literal tab character
		out.hasCommand = true;
		out.id    = CommandId::InsertText;
		out.arg   = "\t";
		out.count = 0;
		return true;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		out = {true, CommandId::Newline, "", 0};
		return true;
	case SDLK_ESCAPE:
		k_prefix = false;
		out = {true, CommandId::Refresh, "", 0};
		return true;
	default:
		break;
	}

	// If we are in k-prefix, the very next key must be interpreted via the
	// C-k keymap first, even if Control is held (e.g., C-k C-d).
	if (k_prefix) {
		k_prefix = false;
		// Normalize SDL key to ASCII where possible
		int ascii_key = 0;
		if (key >= SDLK_SPACE && key <= SDLK_z) {
			ascii_key = static_cast<int>(key);
		}
		bool ctrl2 = (mod & KMOD_CTRL) != 0;
		if (ascii_key != 0) {
			ascii_key = KLowerAscii(ascii_key);
			CommandId id;
			if (KLookupKCommand(ascii_key, ctrl2, id)) {
				out = {true, id, "", 0};
				return true;
			}
			// Unknown k-command: report the typed character
			char c = (ascii_key >= 0x20 && ascii_key <= 0x7e) ? static_cast<char>(ascii_key) : '?';
			std::string arg(1, c);
			out = {true, CommandId::UnknownKCommand, arg, 0};
			return true;
		}
		out.hasCommand = false;
		return true;
	}

	if (is_ctrl) {
		if (key == SDLK_k || key == SDLK_KP_EQUALS) {
			k_prefix = true;
			out      = {true, CommandId::KPrefix, "", 0};
			return true;
		}
		// Map other control chords via shared keymap
		if (key >= SDLK_a && key <= SDLK_z) {
			int ascii_key = static_cast<int>('a' + (key - SDLK_a));
			CommandId id;
			if (KLookupCtrlCommand(ascii_key, id)) {
				out = {true, id, "", 0};
				return true;
			}
		}
	}

	// Alt/Meta bindings (ESC f/b equivalent)
	if (is_alt) {
		if (key >= SDLK_a && key <= SDLK_z) {
			int ascii_key = static_cast<int>('a' + (key - SDLK_a));
			CommandId id;
			if (KLookupEscCommand(ascii_key, id)) {
				out = {true, id, "", 0};
				return true;
			}
		}
	}

	// k_prefix handled earlier

	return false;
}


bool
GUIInputHandler::ProcessSDLEvent(const SDL_Event &e)
{
	MappedInput mi;
	bool produced = false;
	switch (e.type) {
	case SDL_KEYDOWN: {
		// Remember whether we were in k-prefix before handling this key
		bool was_k_prefix     = k_prefix_;
		SDL_Keymod mods       = SDL_Keymod(e.key.keysym.mod);
		const SDL_Keycode key = e.key.keysym.sym;
		produced              = map_key(key, mods, k_prefix_, mi);
		// Suppress the immediate following SDL_TEXTINPUT only in cases where
		// SDL would also emit a text input for the same physical keystroke:
		//  - k-prefix printable suffix keys (no Ctrl), and
		//  - Alt/Meta modified printable letters.
		// Do NOT suppress for non-text keys like Tab/Enter/Backspace/arrows/etc.,
		// otherwise the next normal character would be dropped.
		if (produced && mi.hasCommand) {
			const bool is_ctrl             = (mods & KMOD_CTRL) != 0;
			const bool is_alt              = (mods & (KMOD_ALT | KMOD_LALT | KMOD_RALT)) != 0;
			const bool is_printable_letter = (key >= SDLK_SPACE && key <= SDLK_z);
			const bool is_non_text_key     =
				key == SDLK_TAB || key == SDLK_RETURN || key == SDLK_KP_ENTER ||
				key == SDLK_BACKSPACE || key == SDLK_DELETE || key == SDLK_ESCAPE ||
				key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN ||
				key == SDLK_HOME || key == SDLK_END || key == SDLK_PAGEUP || key == SDLK_PAGEDOWN;

			bool should_suppress = false;
			if (!is_non_text_key) {
				// k-prefix then a printable key normally generates TEXTINPUT
				if (was_k_prefix && is_printable_letter && !is_ctrl) {
					should_suppress = true;
				}
				// Alt/Meta + letter can also generate TEXTINPUT on some platforms
				if (is_alt && key >= SDLK_a && key <= SDLK_z) {
					should_suppress = true;
				}
			}
			if (should_suppress) {
				suppress_text_input_once_ = true;
			}
		}
	}
	break;
	case SDL_TEXTINPUT:
		// Ignore text input while in k-prefix, or once after a command-producing keydown
		if (suppress_text_input_once_) {
			suppress_text_input_once_ = false; // consume suppression
			produced                  = true; // consumed input
			break;
		}
		if (!k_prefix_ && e.text.text[0] != '\0') {
			mi.hasCommand = true;
			mi.id         = CommandId::InsertText;
			mi.arg        = std::string(e.text.text);
			mi.count      = 0;
			produced      = true;
		} else {
			produced = true; // consumed while k-prefix is active
		}
		break;
	default:
		break;
	}
	if (produced && mi.hasCommand) {
		std::lock_guard<std::mutex> lk(mu_);
		q_.push(mi);
	}
	return produced;
}


bool
GUIInputHandler::Poll(MappedInput &out)
{
	std::lock_guard<std::mutex> lk(mu_);
	if (q_.empty())
		return false;
	out = q_.front();
	q_.pop();
	return true;
}
