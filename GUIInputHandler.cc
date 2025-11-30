#include <SDL.h>
#include <cstdio>
#include <ncurses.h>

#include "GUIInputHandler.h"
#include "KKeymap.h"


static bool
map_key(const SDL_Keycode key,
        const SDL_Keymod mod,
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
	// Ctrl handling
	const bool is_ctrl = (mod & KMOD_CTRL) != 0;
	const bool is_alt  = (mod & (KMOD_ALT | KMOD_LALT | KMOD_RALT)) != 0;

	// If previous key was ESC, interpret this as Meta via ESC keymap
	if (esc_meta) {
		int ascii_key = 0;
		if (key == SDLK_BACKSPACE) {
			// ESC BACKSPACE: map to DeleteWordPrev using ncurses KEY_BACKSPACE constant
			ascii_key = KEY_BACKSPACE;
		} else if (key >= SDLK_a && key <= SDLK_z) {
			ascii_key = static_cast<int>('a' + (key - SDLK_a));
		} else if (key == SDLK_COMMA) {
			ascii_key = '<';
		} else if (key == SDLK_PERIOD) {
			ascii_key = '>';
		} else if (key == SDLK_LESS) {
			ascii_key = '<';
		} else if (key == SDLK_GREATER) {
			ascii_key = '>';
		}
		if (ascii_key != 0) {
			ascii_key = KLowerAscii(ascii_key);
			CommandId id;
			if (KLookupEscCommand(ascii_key, id)) {
				// Only consume the ESC-meta prefix if we actually mapped a command
				esc_meta = false;
				out      = {true, id, "", 0};
				return true;
			}
		}
		// Unhandled meta chord at KEYDOWN: do not clear esc_meta here.
		// Leave it set so SDL_TEXTINPUT fallback can translate and suppress insertion.
		out.hasCommand = false;
		return true;
	}

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
		esc_meta       = true; // next key will be treated as Meta
		out.hasCommand = false; // no immediate command for bare ESC in GUI
		return true;
	default:
		break;
	}

	// If we are in k-prefix, interpret the very next key via the C-k keymap immediately.
	if (k_prefix) {
		k_prefix = false;
		esc_meta = false;
		// Normalize to ASCII; preserve case for letters using Shift
		int ascii_key = 0;
		if (key >= SDLK_a && key <= SDLK_z) {
			ascii_key = static_cast<int>('a' + (key - SDLK_a));
			if (mod & KMOD_SHIFT)
				ascii_key = ascii_key - 'a' + 'A';
		} else if (key == SDLK_COMMA) {
			ascii_key = '<';
		} else if (key == SDLK_PERIOD) {
			ascii_key = '>';
		} else if (key == SDLK_LESS) {
			ascii_key = '<';
		} else if (key == SDLK_GREATER) {
			ascii_key = '>';
		} else if (key >= SDLK_SPACE && key <= SDLK_z) {
			ascii_key = static_cast<int>(key);
		}
		bool ctrl2 = (mod & KMOD_CTRL) != 0;
		if (ascii_key != 0) {
			int lower                  = KLowerAscii(ascii_key);
			bool ctrl_suffix_supported = (lower == 'd' || lower == 'x' || lower == 'q');
			bool pass_ctrl             = ctrl2 && ctrl_suffix_supported;
			CommandId id;
			bool mapped = KLookupKCommand(ascii_key, pass_ctrl, id);
			// Diagnostics for u/U
			if (lower == 'u') {
				char disp = (ascii_key >= 0x20 && ascii_key <= 0x7e)
					            ? static_cast<char>(ascii_key)
					            : '?';
				std::fprintf(stderr,
				             "[kge] k-prefix suffix: sym=%d mods=0x%x ascii=%d '%c' ctrl2=%d pass_ctrl=%d mapped=%d id=%d\n",
				             static_cast<int>(key), static_cast<unsigned int>(mod), ascii_key, disp,
				             ctrl2 ? 1 : 0, pass_ctrl ? 1 : 0, mapped ? 1 : 0,
				             mapped ? static_cast<int>(id) : -1);
				std::fflush(stderr);
			}
			if (mapped) {
				out = {true, id, "", 0};
				return true;
			}
			int shown = KLowerAscii(ascii_key);
			char c    = (shown >= 0x20 && shown <= 0x7e) ? static_cast<char>(shown) : '?';
			std::string arg(1, c);
			out = {true, CommandId::UnknownKCommand, arg, 0};
			return true;
		}
		out.hasCommand = false;
		return true;
	}

	if (is_ctrl) {
		// Universal argument: C-u
		if (key == SDLK_u) {
			if (!uarg_active) {
				uarg_active     = true;
				uarg_collecting = true;
				uarg_negative   = false;
				uarg_had_digits = false;
				uarg_value      = 4; // default
				uarg_text.clear();
				out = {true, CommandId::UArgStatus, uarg_text, 0};
				return true;
			} else if (uarg_collecting && !uarg_had_digits && !uarg_negative) {
				if (uarg_value <= 0)
					uarg_value = 4;
				else
					uarg_value *= 4; // repeated C-u multiplies by 4
				out = {true, CommandId::UArgStatus, uarg_text, 0};
				return true;
			} else {
				// End collection if already started with digits or '-'
				uarg_collecting = false;
				if (!uarg_had_digits && !uarg_negative && uarg_value <= 0)
					uarg_value = 4;
			}
			out.hasCommand = false;
			return true;
		}
		// Cancel universal arg on C-g as well (it maps to Refresh via ctrl map)
		if (key == SDLK_g) {
			uarg_active     = false;
			uarg_collecting = false;
			uarg_negative   = false;
			uarg_had_digits = false;
			uarg_value      = 0;
			uarg_text.clear();
		}
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
		int ascii_key = 0;
		if (key == SDLK_BACKSPACE) {
			// Alt BACKSPACE: map to DeleteWordPrev using ncurses KEY_BACKSPACE constant
			ascii_key = KEY_BACKSPACE;
		} else if (key >= SDLK_a && key <= SDLK_z) {
			ascii_key = static_cast<int>('a' + (key - SDLK_a));
		} else if (key == SDLK_COMMA) {
			ascii_key = '<';
		} else if (key == SDLK_PERIOD) {
			ascii_key = '>';
		} else if (key == SDLK_LESS) {
			ascii_key = '<';
		} else if (key == SDLK_GREATER) {
			ascii_key = '>';
		}
		if (ascii_key != 0) {
			CommandId id;
			if (KLookupEscCommand(ascii_key, id)) {
				out = {true, id, "", 0};
				return true;
			}
		}
	}

	// If collecting universal argument, allow digits/minus on KEYDOWN path too
	if (uarg_active && uarg_collecting) {
		if ((key >= SDLK_0 && key <= SDLK_9) && !(mod & KMOD_SHIFT)) {
			int d = static_cast<int>(key - SDLK_0);
			if (!uarg_had_digits) {
				uarg_value      = 0;
				uarg_had_digits = true;
			}
			if (uarg_value < 100000000) {
				uarg_value = uarg_value * 10 + d;
			}
			uarg_text.push_back(static_cast<char>('0' + d));
			out = {true, CommandId::UArgStatus, uarg_text, 0};
			return true;
		}
		if (key == SDLK_MINUS && !uarg_had_digits && !uarg_negative) {
			uarg_negative = true;
			uarg_text     = "-";
			out           = {true, CommandId::UArgStatus, uarg_text, 0};
			return true;
		}
		// Any other key will end collection; process it normally
		uarg_collecting = false;
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
		// Remember state before mapping; used for TEXTINPUT suppression heuristics
		const bool was_k_prefix = k_prefix_;
		const bool was_esc_meta = esc_meta_;
		SDL_Keymod mods         = SDL_Keymod(e.key.keysym.mod);
		const SDL_Keycode key   = e.key.keysym.sym;

		// Handle Paste: Ctrl+V (Windows/Linux) or Cmd+V (macOS)
		// Note: SDL defines letter keycodes in lowercase only (e.g., SDLK_v). Shift does not change keycode.
		if ((mods & (KMOD_CTRL | KMOD_GUI)) && (key == SDLK_v)) {
			char *clip = SDL_GetClipboardText();
			if (clip) {
				std::string text(clip);
				SDL_free(clip);
				// Split on '\n' and enqueue as InsertText/Newline commands
				std::lock_guard<std::mutex> lk(mu_);
				std::size_t start = 0;
				while (start <= text.size()) {
					std::size_t pos = text.find('\n', start);
					std::string_view segment;
					bool has_nl = (pos != std::string::npos);
					if (has_nl) {
						segment = std::string_view(text).substr(start, pos - start);
					} else {
						segment = std::string_view(text).substr(start);
					}
					if (!segment.empty()) {
						MappedInput ins{true, CommandId::InsertText, std::string(segment), 0};
						q_.push(ins);
					}
					if (has_nl) {
						MappedInput nl{true, CommandId::Newline, std::string(), 0};
						q_.push(nl);
						start = pos + 1;
					} else {
						break;
					}
				}
				// Suppress the corresponding TEXTINPUT that may follow
				suppress_text_input_once_ = true;
				return true; // consumed
			}
		}

		produced = map_key(key, mods,
		                   k_prefix_, esc_meta_,
		                   uarg_active_, uarg_collecting_, uarg_negative_, uarg_had_digits_, uarg_value_,
		                   uarg_text_,
		                   mi);

		// If we just consumed a universal-argument digit or '-' on KEYDOWN and emitted UArgStatus,
		// suppress the subsequent SDL_TEXTINPUT for this keystroke to avoid duplicating the digit in status.
		if (produced && mi.hasCommand && mi.id == CommandId::UArgStatus) {
			// Digits without shift, or a plain '-'
			const bool is_digit_key = (key >= SDLK_0 && key <= SDLK_9) && !(mods & KMOD_SHIFT);
			const bool is_minus_key = (key == SDLK_MINUS);
			if (uarg_active_ && uarg_collecting_ && (is_digit_key || is_minus_key)) {
				suppress_text_input_once_ = true;
			}
		}

		// Suppress the immediate following SDL_TEXTINPUT when a printable KEYDOWN was used as a
		// k-prefix suffix or Meta (Alt/ESC) chord, regardless of whether a command was produced.
		const bool is_alt              = (mods & (KMOD_ALT | KMOD_LALT | KMOD_RALT)) != 0;
		const bool is_printable_letter = (key >= SDLK_SPACE && key <= SDLK_z);
		const bool is_non_text_key     =
			key == SDLK_TAB || key == SDLK_RETURN || key == SDLK_KP_ENTER ||
			key == SDLK_BACKSPACE || key == SDLK_DELETE || key == SDLK_ESCAPE ||
			key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN ||
			key == SDLK_HOME || key == SDLK_END || key == SDLK_PAGEUP || key == SDLK_PAGEDOWN;

		bool should_suppress = false;
		if (!is_non_text_key) {
			// Any k-prefix suffix that is printable should suppress TEXTINPUT, even if no
			// command mapped (we report unknown via status instead of inserting text).
			if (was_k_prefix && is_printable_letter) {
				should_suppress = true;
			}
			// Alt/Meta + letter can also generate TEXTINPUT on some platforms
			const bool is_meta_symbol = (
				key == SDLK_COMMA || key == SDLK_PERIOD || key == SDLK_LESS || key == SDLK_GREATER);
			if (is_alt && ((key >= SDLK_a && key <= SDLK_z) || is_meta_symbol)) {
				should_suppress = true;
			}
			// ESC-as-meta followed by printable
			if (was_esc_meta && (is_printable_letter || is_meta_symbol)) {
				should_suppress = true;
			}
		}
		if (should_suppress) {
			suppress_text_input_once_ = true;
		}
		break;
	}
	case SDL_TEXTINPUT: {
		// If the previous KEYDOWN requested suppression of this TEXTINPUT (e.g.,
		// we already handled a uarg digit/minus or a k-prefix printable), do it
		// immediately before any other handling to avoid duplicates.
		if (suppress_text_input_once_) {
			suppress_text_input_once_ = false; // consume suppression
			produced                  = true; // consumed event
			break;
		}

		// If universal argument collection is active, consume digit/minus TEXTINPUT
		if (uarg_active_ && uarg_collecting_) {
			const char *txt = e.text.text;
			if (txt && *txt) {
				unsigned char c0 = static_cast<unsigned char>(txt[0]);
				if (c0 >= '0' && c0 <= '9') {
					int d = c0 - '0';
					if (!uarg_had_digits_) {
						uarg_value_      = 0;
						uarg_had_digits_ = true;
					}
					if (uarg_value_ < 100000000) {
						uarg_value_ = uarg_value_ * 10 + d;
					}
					uarg_text_.push_back(static_cast<char>(c0));
					mi       = {true, CommandId::UArgStatus, uarg_text_, 0};
					produced = true; // consumed and enqueued status update
					break;
				}
				if (c0 == '-' && !uarg_had_digits_ && !uarg_negative_) {
					uarg_negative_ = true;
					uarg_text_     = "-";
					mi             = {true, CommandId::UArgStatus, uarg_text_, 0};
					produced       = true;
					break;
				}
			}
			// End collection and allow this TEXTINPUT to be processed normally below
			uarg_collecting_ = false;
		}

		// If we are still in k-prefix and KEYDOWN path didn't handle the suffix,
		// use TEXTINPUT's actual character (handles Shifted letters on macOS) to map the k-command.
		if (k_prefix_) {
			k_prefix_       = false;
			esc_meta_       = false;
			const char *txt = e.text.text;
			if (txt && *txt) {
				unsigned char c0 = static_cast<unsigned char>(txt[0]);
				int ascii_key    = 0;
				if (c0 < 0x80) {
					ascii_key = static_cast<int>(c0);
				}
				if (ascii_key != 0) {
					// Map via k-prefix table; do not pass Ctrl for TEXTINPUT case
					CommandId id;
					bool mapped = KLookupKCommand(ascii_key, false, id);
					// Diagnostics: log any k-prefix TEXTINPUT suffix mapping
					char disp = (ascii_key >= 0x20 && ascii_key <= 0x7e)
						            ? static_cast<char>(ascii_key)
						            : '?';
					std::fprintf(stderr,
					             "[kge] k-prefix TEXTINPUT suffix: ascii=%d '%c' mapped=%d id=%d\n",
					             ascii_key, disp, mapped ? 1 : 0,
					             mapped ? static_cast<int>(id) : -1);
					std::fflush(stderr);
					if (mapped) {
						mi       = {true, id, "", 0};
						produced = true;
						break; // handled; do not insert text
					} else {
						// Unknown k-command via TEXTINPUT path
						int shown = KLowerAscii(ascii_key);
						char c    = (shown >= 0x20 && shown <= 0x7e)
							         ? static_cast<char>(shown)
							         : '?';
						std::string arg(1, c);
						mi       = {true, CommandId::UnknownKCommand, arg, 0};
						produced = true;
						break;
					}
				}
			}
			// Consume even if no usable ascii was found
			produced = true;
			break;
		}
		// (suppression is handled at the top of this case)
		// Handle ESC-as-meta fallback on TEXTINPUT: some platforms emit only TEXTINPUT
		// for the printable part after ESC. If esc_meta_ is set, translate first char.
		if (esc_meta_) {
			esc_meta_       = false; // consume meta prefix
			const char *txt = e.text.text;
			if (txt && *txt) {
				// Parse first UTF-8 codepoint (we care only about common ASCII cases)
				unsigned char c0 = static_cast<unsigned char>(txt[0]);
				// Map a few common symbols/letters used in our ESC map
				int ascii_key = 0;
				if (c0 < 0x80) {
					// ASCII path
					ascii_key = static_cast<int>(c0);
					ascii_key = KLowerAscii(ascii_key);
				} else {
					// Basic handling for macOS Option combos that might produce ≤/≥
					// Compare the UTF-8 prefix for these two symbols
					std::string s(txt);
					if (s.rfind("\xE2\x89\xA4", 0) == 0) {
						// U+2264 '≤'
						ascii_key = '<';
					} else if (s.rfind("\xE2\x89\xA5", 0) == 0) {
						// U+2265 '≥'
						ascii_key = '>';
					}
				}
				if (ascii_key != 0) {
					CommandId id;
					if (KLookupEscCommand(ascii_key, id)) {
						mi       = {true, id, "", 0};
						produced = true;
						break;
					}
				}
			}
			// If we get here, swallow the TEXTINPUT (do not insert stray char)
			produced = true;
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
	}
	default:
		break;
	}

	if (produced && mi.hasCommand) {
		// Attach universal-argument count if present, then clear the state
		if (uarg_active_ && mi.id != CommandId::UArgStatus) {
			int count = 0;
			if (!uarg_had_digits_ && !uarg_negative_) {
				count = (uarg_value_ > 0) ? uarg_value_ : 4;
			} else {
				count = uarg_value_;
				if (uarg_negative_)
					count = -count;
			}
			mi.count         = count;
			uarg_active_     = false;
			uarg_collecting_ = false;
			uarg_negative_   = false;
			uarg_had_digits_ = false;
			uarg_value_      = 0;
			uarg_text_.clear();
		}
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
