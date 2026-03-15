/*
 * GUIFrontend - couples ImGuiInputHandler + GUIRenderer and owns SDL2/ImGui lifecycle
 */
#pragma once
#include <memory>
#include <vector>

#include "Frontend.h"
#include "GUIConfig.h"
#include "ImGuiInputHandler.h"
#include "ImGuiRenderer.h"
#include "Editor.h"


struct SDL_Window;
typedef void *SDL_GLContext;

class GUIFrontend final : public Frontend {
public:
	GUIFrontend() = default;

	~GUIFrontend() override = default;

	bool Init(int &argc, char **argv, Editor &ed) override;

	void Step(Editor &ed, bool &running) override;

	void Shutdown() override;

private:
	// Per-window state
	struct WindowState {
		SDL_Window *window   = nullptr;
		SDL_GLContext gl_ctx = nullptr;
		ImGuiInputHandler input{};
		ImGuiRenderer renderer{};
		Editor editor{};
		int width  = 1280;
		int height = 800;
		bool alive = true;
	};

	// Open a new secondary window sharing the primary editor's buffer list.
	// Returns false if window creation fails.
	bool OpenNewWindow_(Editor &primary);

	static bool LoadGuiFont_(const char *path, float size_px);

	GUIConfig config_{};
	// Primary window (index 0 in windows_); created during Init.
	std::vector<std::unique_ptr<WindowState> > windows_;
};