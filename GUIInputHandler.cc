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

 if (is_ctrl) {
        switch (key) {
        case SDLK_k:
        case SDLK_KP_EQUALS: // treat Ctrl-K
            k_prefix = true;
            out = {true, CommandId::KPrefix, "", 0};
            return true;
        case SDLK_n: // C-n: down
            out = {true, CommandId::MoveDown, "", 0};
            return true;
        case SDLK_p: // C-p: up
            out = {true, CommandId::MoveUp, "", 0};
            return true;
        case SDLK_f: // C-f: right
            out = {true, CommandId::MoveRight, "", 0};
            return true;
        case SDLK_b: // C-b: left
            out = {true, CommandId::MoveLeft, "", 0};
            return true;
        case SDLK_a:
            out = {true, CommandId::MoveHome, "", 0};
            return true;
        case SDLK_e:
            out = {true, CommandId::MoveEnd, "", 0};
            return true;
        case SDLK_g:
            k_prefix = false;
            out = {true, CommandId::Refresh, "", 0};
            return true;
        case SDLK_l:
            out = {true, CommandId::Refresh, "", 0};
            return true;
        case SDLK_s:
            out = {true, CommandId::FindStart, "", 0};
            return true;
        case SDLK_q:
            out = {true, CommandId::Quit, "", 0};
            return true;
        case SDLK_x:
            out = {true, CommandId::SaveAndQuit, "", 0};
            return true;
        default:
            break;
        }
    }

    // Alt/Meta bindings (ESC f/b equivalent)
    if (is_alt) {
        switch (key) {
        case SDLK_b:
            out = {true, CommandId::WordPrev, "", 0};
            return true;
        case SDLK_f:
            out = {true, CommandId::WordNext, "", 0};
            return true;
        default:
            break;
        }
    }

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

	return false;
}


bool
GUIInputHandler::ProcessSDLEvent(const SDL_Event &e)
{
	MappedInput mi;
	bool produced = false;
	switch (e.type) {
	case SDL_KEYDOWN:
		produced = map_key(e.key.keysym.sym, SDL_Keymod(e.key.keysym.mod), k_prefix_, mi);
		break;
	case SDL_TEXTINPUT:
		if (e.text.text[0] != '\0') {
			mi.hasCommand = true;
			mi.id         = CommandId::InsertText;
			mi.arg        = std::string(e.text.text);
			mi.count      = 0;
			produced      = true;
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
