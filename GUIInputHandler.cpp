#include "GUIInputHandler.h"

#include <SDL.h>

static bool map_key(SDL_Keycode key, SDL_Keymod mod, bool &k_prefix, MappedInput &out)
{
    // Ctrl handling
    bool is_ctrl = (mod & KMOD_CTRL) != 0;

    // Movement and basic keys
    switch (key) {
        case SDLK_LEFT:  out = {true, CommandId::MoveLeft,  "", 0}; return true;
        case SDLK_RIGHT: out = {true, CommandId::MoveRight, "", 0}; return true;
        case SDLK_UP:    out = {true, CommandId::MoveUp,    "", 0}; return true;
        case SDLK_DOWN:  out = {true, CommandId::MoveDown,  "", 0}; return true;
        case SDLK_HOME:  out = {true, CommandId::MoveHome,  "", 0}; return true;
        case SDLK_END:   out = {true, CommandId::MoveEnd,   "", 0}; return true;
        case SDLK_DELETE: out = {true, CommandId::DeleteChar, "", 0}; return true;
        case SDLK_BACKSPACE: out = {true, CommandId::Backspace, "", 0}; return true;
        case SDLK_RETURN: case SDLK_KP_ENTER: out = {true, CommandId::Newline, "", 0}; return true;
        case SDLK_ESCAPE: k_prefix = false; out = {true, CommandId::Refresh, "", 0}; return true;
        default: break;
    }

    if (is_ctrl) {
        switch (key) {
            case SDLK_k: case SDLK_KP_EQUALS: // treat Ctrl-K
                k_prefix = true;
                out = {true, CommandId::Refresh, "", 0};
                return true;
            case SDLK_g:
                k_prefix = false;
                out = {true, CommandId::Refresh, "", 0};
                return true;
            case SDLK_l: out = {true, CommandId::Refresh,   "", 0}; return true;
            case SDLK_s: out = {true, CommandId::FindStart, "", 0}; return true;
            case SDLK_q: out = {true, CommandId::Quit,      "", 0}; return true;
            case SDLK_x: out = {true, CommandId::SaveAndQuit, "", 0}; return true;
            default: break;
        }
    }

    if (k_prefix) {
        k_prefix = false;
        switch (key) {
            case SDLK_s: out = {true, CommandId::Save,        "", 0}; return true;
            case SDLK_x: out = {true, CommandId::SaveAndQuit, "", 0}; return true;
            case SDLK_q: out = {true, CommandId::Quit,        "", 0}; return true;
            default: break;
        }
        out.hasCommand = false;
        return true;
    }

    return false;
}

bool GUIInputHandler::ProcessSDLEvent(const SDL_Event &e)
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
                mi.id = CommandId::InsertText;
                mi.arg = std::string(e.text.text);
                mi.count = 0;
                produced = true;
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

bool GUIInputHandler::Poll(MappedInput &out)
{
    std::lock_guard<std::mutex> lk(mu_);
    if (q_.empty()) return false;
    out = q_.front();
    q_.pop();
    return true;
}
