/*
 * GUIInputHandler - ImGui/SDL2-based input mapping for GUI mode
 */
#ifndef KTE_GUI_INPUT_HANDLER_H
#define KTE_GUI_INPUT_HANDLER_H

#include <queue>
#include <mutex>

#include "InputHandler.h"

union SDL_Event; // fwd decl to avoid including SDL here (SDL defines SDL_Event as a union)

class GUIInputHandler : public InputHandler {
public:
    GUIInputHandler() = default;
    ~GUIInputHandler() override = default;

    // Translate an SDL event to editor command and enqueue if applicable.
    // Returns true if it produced a mapped command or consumed input.
    bool ProcessSDLEvent(const SDL_Event &e);

    bool Poll(MappedInput &out) override;

private:
    std::mutex mu_;
    std::queue<MappedInput> q_;
    bool k_prefix_ = false;
};

#endif // KTE_GUI_INPUT_HANDLER_H
