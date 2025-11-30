#include <SDL.h>
#include <SDL_opengl.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl3.h>
#include <cstdio>
#include <string>
#include <cstring>
#include <cstdlib>

#include "Editor.h"
#include "Command.h"
#include "GUIFrontend.h"
#include "Font.h" // embedded default font (DefaultFontRegular)

static const char *kGlslVersion = "#version 150"; // GL 3.2 core (macOS compatible)

bool
GUIFrontend::Init(Editor &ed)
{
    (void)ed; // editor dimensions will be initialized during the first Step() frame
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		return false;
	}

	// GL attributes for core profile
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	window_ = SDL_CreateWindow(
		"kte",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		width_, height_,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (!window_)
		return false;

	gl_ctx_ = SDL_GL_CreateContext(window_);
	if (!gl_ctx_)
		return false;
	SDL_GL_MakeCurrent(window_, gl_ctx_);
	SDL_GL_SetSwapInterval(1); // vsync

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
 ImGuiIO &io = ImGui::GetIO();
 (void) io;
 ImGui::StyleColorsDark();

	if (!ImGui_ImplSDL2_InitForOpenGL(window_, gl_ctx_))
		return false;
	if (!ImGui_ImplOpenGL3_Init(kGlslVersion))
		return false;

 // Cache initial window size; logical rows/cols will be computed in Step() once a valid ImGui frame exists
 int w, h;
 SDL_GetWindowSize(window_, &w, &h);
 width_  = w;
 height_ = h;

    // Initialize GUI font from embedded default
    LoadGuiFont_(nullptr, 16.f);

    return true;
}


void
GUIFrontend::Step(Editor &ed, bool &running)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		ImGui_ImplSDL2_ProcessEvent(&e);
		switch (e.type) {
			case SDL_QUIT:
				running = false;
				break;
   case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    width_  = e.window.data1;
                    height_ = e.window.data2;
                }
                break;
			default:
				break;
		}
		// Map input to commands
		input_.ProcessSDLEvent(e);
	}

 // Execute pending mapped inputs (drain queue)
	for (;;) {
		MappedInput mi;
		if (!input_.Poll(mi))
			break;
		if (mi.hasCommand) {
			Execute(ed, mi.id, mi.arg, mi.count);
			if (mi.id == CommandId::Quit || mi.id == CommandId::SaveAndQuit) {
				running = false;
			}
		}
	}

    // Start a new ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window_);
    ImGui::NewFrame();

    // Update editor logical rows/cols using current ImGui metrics and display size
    {
        ImGuiIO &io = ImGui::GetIO();
        float line_h = ImGui::GetTextLineHeightWithSpacing();
        float ch_w   = ImGui::CalcTextSize("M").x;
        if (line_h <= 0.0f) line_h = 16.0f;
        if (ch_w <= 0.0f) ch_w = 8.0f;
        // Prefer ImGui IO display size; fall back to cached SDL window size
        float disp_w = io.DisplaySize.x > 0 ? io.DisplaySize.x : static_cast<float>(width_);
        float disp_h = io.DisplaySize.y > 0 ? io.DisplaySize.y : static_cast<float>(height_);

        // Account for the GUI window padding and the status bar height used in GUIRenderer
        const ImGuiStyle &style = ImGui::GetStyle();
        float pad_x = style.WindowPadding.x;
        float pad_y = style.WindowPadding.y;
        // Status bar reserves one frame height (with spacing) inside the window
        float status_h = ImGui::GetFrameHeightWithSpacing();

        float avail_w = std::max(0.0f, disp_w - 2.0f * pad_x);
        float avail_h = std::max(0.0f, disp_h - 2.0f * pad_y - status_h);

        // Visible content rows inside the scroll child
        std::size_t content_rows = static_cast<std::size_t>(std::floor(avail_h / line_h));
        // Editor::Rows includes the status line; add 1 back for it.
        std::size_t rows = std::max<std::size_t>(1, content_rows + 1);
        std::size_t cols = static_cast<std::size_t>(std::max(1.0f, std::floor(avail_w / ch_w)));

        // Only update if changed to avoid churn
        if (rows != ed.Rows() || cols != ed.Cols()) {
            ed.SetDimensions(rows, cols);
        }
    }

 // No runtime font UI; always use embedded font.

 // Draw editor UI
 renderer_.Draw(ed);

	// Render
	ImGui::Render();
	int display_w, display_h;
	SDL_GL_GetDrawableSize(window_, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(0.1f, 0.1f, 0.11f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	SDL_GL_SwapWindow(window_);
}


void
GUIFrontend::Shutdown()
{
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	if (gl_ctx_) {
		SDL_GL_DeleteContext(gl_ctx_);
		gl_ctx_ = nullptr;
	}
	if (window_) {
		SDL_DestroyWindow(window_);
		window_ = nullptr;
	}
	SDL_Quit();
}


bool
GUIFrontend::LoadGuiFont_(const char * /*path*/, float size_px)
{
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFont *font = io.Fonts->AddFontFromMemoryCompressedTTF(
        (void*)DefaultFontRegularCompressedData,
        (int)DefaultFontRegularCompressedSize,
        size_px);
    if (!font) {
        font = io.Fonts->AddFontDefault();
    }
    (void) font;
    io.Fonts->Build();
    return true;
}

// No runtime font reload or system font resolution in this simplified build.