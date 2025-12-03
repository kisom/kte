/*
 * GUIFrontend - couples GUIInputHandler + GUIRenderer and owns SDL2/ImGui lifecycle
 */
#pragma once
#include "Frontend.h"
#include "GUIConfig.h"
#include "GUIInputHandler.h"
#include "GUIRenderer.h"


struct SDL_Window;
typedef void *SDL_GLContext;

class GUIFrontend final : public Frontend {
public:
	GUIFrontend() = default;

	~GUIFrontend() override = default;

	bool Init(Editor &ed) override;

	void Step(Editor &ed, bool &running) override;

	void Shutdown() override;

private:
	static bool LoadGuiFont_(const char *path, float size_px);

	GUIConfig config_{};
	GUIInputHandler input_{};
	GUIRenderer renderer_{};
	SDL_Window *window_   = nullptr;
	SDL_GLContext gl_ctx_ = nullptr;
	int width_            = 1280;
	int height_           = 800;
};