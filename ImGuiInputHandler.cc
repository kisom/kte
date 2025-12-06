#include <cstdio>
#include <algorithm>
#include <ncurses.h>

#include <SDL.h>
#include <imgui.h>

#include "ImGuiInputHandler.h"
#include "KKeymap.h"
#include "Editor.h"


static bool
map_key(const SDL_Keycode key,
        const SDL_Keymod mod,
        bool &k_prefix,
        bool &esc_meta,
        bool &k_ctrl_pending,
        Editor *ed,
        MappedInput &out,
        bool &suppress_textinput_once)
{
	// Ctrl handling
	const bool is_ctrl = (mod & KMOD_CTRL) != 0;
	const bool is_alt  = (mod & (KMOD_ALT | KMOD_LALT | KMOD_RALT)) != 0;

	// If previous key was ESC, interpret this as Meta via ESC keymap.
	// Prefer KEYDOWN when we can derive a printable ASCII; otherwise defer to TEXTINPUT.
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
			esc_meta  = false; // consume if we can decide on KEYDOWN
			ascii_key = KLowerAscii(ascii_key);
			CommandId id;
			if (KLookupEscCommand(ascii_key, id)) {
				out = {true, id, "", 0};
				return true;
			}
			// Known printable but unmapped ESC sequence: report invalid
			out = {true, CommandId::UnknownEscCommand, "", 0};
			return true;
		}
		// No usable ASCII from KEYDOWN → keep esc_meta set and let TEXTINPUT handle it
		out.hasCommand = false;
		return true;
	}

	// Movement and basic keys
	// These keys exit k-prefix mode if active (user pressed C-k then a special key).
	switch (key) {
	case SDLK_LEFT:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveLeft, "", 0};
		return true;
	case SDLK_RIGHT:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveRight, "", 0};
		return true;
	case SDLK_UP:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveUp, "", 0};
		return true;
	case SDLK_DOWN:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveDown, "", 0};
		return true;
	case SDLK_HOME:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveHome, "", 0};
		return true;
	case SDLK_END:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::MoveEnd, "", 0};
		return true;
	case SDLK_PAGEUP:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::PageUp, "", 0};
		return true;
	case SDLK_PAGEDOWN:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::PageDown, "", 0};
		return true;
	case SDLK_DELETE:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::DeleteChar, "", 0};
		return true;
	case SDLK_BACKSPACE:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::Backspace, "", 0};
		return true;
	case SDLK_TAB:
		// Insert a literal tab character when not interpreting a k-prefix suffix.
		// If k-prefix is active, let the k-prefix handler below consume the key
		// (so Tab doesn't leave k-prefix stuck).
		if (!k_prefix) {
			out = {true, CommandId::InsertText, std::string("\t"), 0};
			return true;
		}
		break; // fall through so k-prefix handler can process
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		k_prefix = false;
		k_ctrl_pending = false;
		out            = {true, CommandId::Newline, "", 0};
		return true;
	case SDLK_ESCAPE:
		k_prefix = false;
		k_ctrl_pending = false;
		esc_meta       = true; // next key will be treated as Meta
		out.hasCommand = false; // no immediate command for bare ESC in GUI
		return true;
	default:
		break;
	}

	// If we are in k-prefix, interpret the very next key via the C-k keymap immediately.
	if (k_prefix) {
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
		// If user typed a literal 'C' (or '^') as a control qualifier, keep k-prefix active
		if (ascii_key == 'C' || ascii_key == 'c' || ascii_key == '^') {
			k_ctrl_pending = true;
			// Keep waiting for the next suffix; show status and suppress ensuing TEXTINPUT
			if (ed)
				ed->SetStatus("C-k C _");
			suppress_textinput_once = true;
			out.hasCommand          = false;
			return true;
		}
		// Otherwise, consume the k-prefix now for the actual suffix
		k_prefix = false;
		if (ascii_key != 0) {
			int lower                  = KLowerAscii(ascii_key);
			bool ctrl_suffix_supported = (lower == 'd' || lower == 'x' || lower == 'q');
			bool pass_ctrl             = (ctrl2 || k_ctrl_pending) && ctrl_suffix_supported;
			k_ctrl_pending             = false;
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
				if (ed)
					ed->SetStatus(""); // clear "C-k _" hint after suffix
				return true;
			}
			int shown = KLowerAscii(ascii_key);
			char c    = (shown >= 0x20 && shown <= 0x7e) ? static_cast<char>(shown) : '?';
			std::string arg(1, c);
			out = {true, CommandId::UnknownKCommand, arg, 0};
			if (ed)
				ed->SetStatus(""); // clear hint; handler will set unknown status
			return true;
		}
		// Non-printable/unmappable key as k-suffix (e.g., F-keys): report unknown and exit k-mode
		out = {true, CommandId::UnknownKCommand, std::string("?"), 0};
		if (ed)
			ed->SetStatus("");
		return true;
	}

	if (is_ctrl) {
		// Universal argument: C-u
		if (key == SDLK_u) {
			if (ed)
				ed->UArgStart();
			out.hasCommand = false;
			return true;
		}
		// Cancel universal arg on C-g as well (it maps to Refresh via ctrl map)
		if (key == SDLK_g) {
			if (ed)
				ed->UArgClear();
			// Also cancel any pending k-prefix qualifier
			k_ctrl_pending = false;
			k_prefix       = false; // treat as cancel of prefix
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

	// If collecting universal argument, allow digits on KEYDOWN path too
	if (ed && ed->UArg() != 0) {
		if ((key >= SDLK_0 && key <= SDLK_9) && !(mod & KMOD_SHIFT)) {
			int d = static_cast<int>(key - SDLK_0);
			ed->UArgDigit(d);
			out.hasCommand = false;
			// We consumed a digit on KEYDOWN; SDL will often also emit TEXTINPUT for it.
			// Request suppression of the very next TEXTINPUT to avoid double-counting.
			suppress_textinput_once = true;
			return true;
		}
	}

	// k_prefix handled earlier

	return false;
}


bool
ImGuiInputHandler::ProcessSDLEvent(const SDL_Event &e)
{
	MappedInput mi;
	bool produced = false;
	switch (e.type) {
	case SDL_MOUSEWHEEL: {
		// High-resolution trackpads can deliver fractional wheel deltas. Accumulate
		// precise values and emit one scroll step per whole unit.
		float dy = 0.0f;
#if SDL_VERSION_ATLEAST(2,0,18)
		dy = e.wheel.preciseY;
#else
		dy = static_cast<float>(e.wheel.y);
#endif
#ifdef SDL_MOUSEWHEEL_FLIPPED
		if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
			dy = -dy;
#endif
		if (dy != 0.0f) {
			wheel_accum_y_ += dy;
			float abs_accum = wheel_accum_y_ >= 0.0f ? wheel_accum_y_ : -wheel_accum_y_;
			int steps       = static_cast<int>(abs_accum);
			if (steps > 0) {
				CommandId id = (wheel_accum_y_ > 0.0f) ? CommandId::ScrollUp : CommandId::ScrollDown;
				std::lock_guard<std::mutex> lk(mu_);
				for (int i = 0; i < steps; ++i) {
					q_.push(MappedInput{true, id, std::string(), 0});
				}
				// remove the whole steps, keep fractional remainder
				wheel_accum_y_ += (wheel_accum_y_ > 0.0f)
					                  ? -static_cast<float>(steps)
					                  : static_cast<float>(steps);
				return true; // consumed
			}
		}
		return false;
	}
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
						MappedInput ins{
							true, CommandId::InsertText, std::string(segment), 0
						};
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

		{
			bool suppress_req = false;
			produced          = map_key(key, mods,
			                   k_prefix_, esc_meta_,
			                   k_ctrl_pending_,
			                   ed_,
			                   mi,
			                   suppress_req);
			if (suppress_req) {
				// Prevent the corresponding TEXTINPUT from delivering the same digit again
				suppress_text_input_once_ = true;
			}
		}

		// Note: Do NOT suppress SDL_TEXTINPUT after inserting a TAB. Most platforms
		// do not emit TEXTINPUT for Tab, and suppressing here would incorrectly
		// eat the next character typed if no TEXTINPUT follows the Tab press.

		// If we just consumed a universal-argument digit or '-' on KEYDOWN and emitted UArgStatus,
		// suppress the subsequent SDL_TEXTINPUT for this keystroke to avoid duplicating the digit in status.
		// Additional suppression handled above when KEYDOWN consumed a uarg digit

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
				key == SDLK_COMMA || key == SDLK_PERIOD || key == SDLK_LESS || key ==
				SDLK_GREATER);
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

		// If editor universal argument is active, consume digit TEXTINPUT
		if (ed_ &&ed_


		
		->
		UArg() != 0
		)
		{
			const char *txt = e.text.text;
			if (txt && *txt) {
				unsigned char c0 = static_cast<unsigned char>(txt[0]);
				if (c0 >= '0' && c0 <= '9') {
					int d = c0 - '0';
					ed_->UArgDigit(d);
					produced = true; // consumed to update status
					break;
				}
			}
			// Non-digit ends collection; allow processing normally below
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
					// Qualifier via TEXTINPUT: 'C' or '^'
					if (ascii_key == 'C' || ascii_key == 'c' || ascii_key == '^') {
						k_ctrl_pending_ = true;
						if (ed_)
							ed_->SetStatus("C-k C _");
						// Keep k-prefix active; do not emit a command
						k_prefix_ = true;
						produced  = true;
						break;
					}
					// Map via k-prefix table; do not pass Ctrl for TEXTINPUT case
					CommandId id;
					bool pass_ctrl  = k_ctrl_pending_;
					k_ctrl_pending_ = false;
					bool mapped     = KLookupKCommand(ascii_key, pass_ctrl, id);
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
						mi = {true, id, "", 0};
						if (ed_)
							ed_->SetStatus(""); // clear "C-k _" hint after suffix
						produced = true;
						break; // handled; do not insert text
					} else {
						// Unknown k-command via TEXTINPUT path
						int shown = KLowerAscii(ascii_key);
						char c    = (shown >= 0x20 && shown <= 0x7e)
							         ? static_cast<char>(shown)
							         : '?';
						std::string arg(1, c);
						mi = {true, CommandId::UnknownKCommand, arg, 0};
						if (ed_)
							ed_->SetStatus("");
						produced = true;
						break;
					}
				}
			}
			// If no usable ASCII was found, still report an unknown k-command and exit k-mode
			mi = {true, CommandId::UnknownKCommand, std::string("?"), 0};
			if (ed_)
				ed_->SetStatus("");
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
			// If we get here, unmapped ESC sequence via TEXTINPUT: report invalid
			mi       = {true, CommandId::UnknownEscCommand, "", 0};
			produced = true;
			break;
		}
		if (!k_prefix_ && e.text.text[0] != '\0') {
			// Ensure InsertText never carries a newline; those must originate from KEYDOWN
			std::string text(e.text.text);
			// Strip any CR/LF that might slip through from certain platforms/IME behaviors
			text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
			text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
			if (!text.empty()) {
				mi.hasCommand = true;
				mi.id         = CommandId::InsertText;
				mi.arg        = std::move(text);
				mi.count      = 0;
				produced      = true;
			} else {
				// Nothing to insert after filtering; consume the event
				produced = true;
			}
		} else {
			produced = true; // consumed while k-prefix is active
		}
		break;
	}
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
ImGuiInputHandler::Poll(MappedInput &out)
{
	std::lock_guard<std::mutex> lk(mu_);
	if (q_.empty())
		return false;
	out = q_.front();
	q_.pop();
	return true;
}