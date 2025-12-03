#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <imgui.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>

#include "GUIFrontend.h"
#include "Command.h"
#include "Editor.h"
#include "GUIConfig.h"
#include "GUITheme.h"
#include "fonts/Font.h" // embedded default font (DefaultFont)
#include "syntax/HighlighterRegistry.h"
#include "syntax/NullHighlighter.h"


#ifndef KTE_FONT_SIZE
#define KTE_FONT_SIZE 16.0f
#endif

static auto kGlslVersion = "#version 150"; // GL 3.2 core (macOS compatible)

bool
GUIFrontend::Init(Editor &ed)
{
	(void) ed; // editor dimensions will be initialized during the first Step() frame
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		return false;
	}

	// Load GUI configuration (fullscreen, columns/rows, font size, theme, background)
	GUIConfig cfg = GUIConfig::Load();

	// GL attributes for core profile
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	// Compute desired window size from config
	Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

	if (cfg.fullscreen) {
		// "Fullscreen": fill the usable bounds of the primary display.
		// On macOS, do NOT use true fullscreen so the menu/status bar remains visible.
		SDL_Rect usable{};
		if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
			width_  = usable.w;
			height_ = usable.h;
		}
#if !defined(__APPLE__)
		// Non-macOS: desktop fullscreen uses the current display resolution.
		win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
	} else {
		// Windowed: width = columns * font_size, height = (rows * 2) * font_size
		int w = cfg.columns * static_cast<int>(cfg.font_size);
		int h = cfg.rows * static_cast<int>(cfg.font_size * 1.2);

		// As a safety, clamp to display usable bounds if retrievable
		SDL_Rect usable{};
		if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
			w = std::min(w, usable.w);
			h = std::min(h, usable.h);
		}
		width_  = std::max(320, w);
		height_ = std::max(200, h);
	}

	SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
	window_ = SDL_CreateWindow(
		"kge - kyle's graphical editor " KTE_VERSION_STR,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		width_, height_,
		win_flags);
	if (!window_) {
		return false;
	}

	SDL_EnableScreenSaver();

#if defined(__APPLE__)
	// macOS: when "fullscreen" is requested, position the window at the
	// top-left of the usable display area to mimic fullscreen while keeping
	// the system menu bar visible.
	if (cfg.fullscreen) {
		SDL_Rect usable{};
		if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
			SDL_SetWindowPosition(window_, usable.x, usable.y);
		}
	}
#endif

	gl_ctx_ = SDL_GL_CreateContext(window_);
	if (!gl_ctx_)
		return false;
	SDL_GL_MakeCurrent(window_, gl_ctx_);
	SDL_GL_SetSwapInterval(1); // vsync

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();

	// Set custom ini filename path to ~/.config/kte/imgui.ini
	if (const char *home = std::getenv("HOME")) {
		namespace fs = std::filesystem;
		fs::path config_dir = fs::path(home) / ".config" / "kte";

		std::error_code ec;
		if (!fs::exists(config_dir)) {
			fs::create_directories(config_dir, ec);
		}

		if (fs::exists(config_dir)) {
			static std::string ini_path = (config_dir / "imgui.ini").string();
			io.IniFilename              = ini_path.c_str();
		}
	}

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
	ImGui::StyleColorsDark();

	// Apply background mode and selected theme (default: Nord). Can be changed at runtime via commands.
	if (cfg.background == "light")
		kte::SetBackgroundMode(kte::BackgroundMode::Light);
	else
		kte::SetBackgroundMode(kte::BackgroundMode::Dark);
	kte::ApplyThemeByName(cfg.theme);

	// Apply default syntax highlighting preference from GUI config to the current buffer
	if (Buffer *b = ed.CurrentBuffer()) {
		if (cfg.syntax) {
			b->SetSyntaxEnabled(true);
			// Ensure a highlighter is available if possible
			b->EnsureHighlighter();
			if (auto *eng = b->Highlighter()) {
				if (!eng->HasHighlighter()) {
					// Try detect from filename and first line; fall back to cpp or existing filetype
					std::string first_line;
					const auto &rows = b->Rows();
					if (!rows.empty())
						first_line = static_cast<std::string>(rows[0]);
					std::string ft = kte::HighlighterRegistry::DetectForPath(
						b->Filename(), first_line);
					if (!ft.empty()) {
						eng->SetHighlighter(kte::HighlighterRegistry::CreateFor(ft));
						b->SetFiletype(ft);
						eng->InvalidateFrom(0);
					} else {
						// Unknown/unsupported -> install a null highlighter to keep syntax enabled
						eng->SetHighlighter(std::make_unique<kte::NullHighlighter>());
						b->SetFiletype("");
						eng->InvalidateFrom(0);
					}
				}
			}
		} else {
			b->SetSyntaxEnabled(false);
		}
	}

	if (!ImGui_ImplSDL2_InitForOpenGL(window_, gl_ctx_))
		return false;
	if (!ImGui_ImplOpenGL3_Init(kGlslVersion))
		return false;

	// Cache initial window size; logical rows/cols will be computed in Step() once a valid ImGui frame exists
	int w, h;
	SDL_GetWindowSize(window_, &w, &h);
	width_  = w;
	height_ = h;

#if defined(__APPLE__)
	// Workaround: On macOS Retina when starting maximized, we sometimes get a
	// subtle input vs draw alignment mismatch until the first manual resize.
	// Nudge the window size by 1px and back to trigger a proper internal
	// recomputation, without visible impact.
	if (w > 1 && h > 1) {
		SDL_SetWindowSize(window_, w - 1, h - 1);
		SDL_SetWindowSize(window_, w, h);
		// Update cached size in case backend reports immediately
		SDL_GetWindowSize(window_, &w, &h);
		width_  = w;
		height_ = h;
	}
#endif

	// Initialize GUI font from embedded default (use configured size or compiled default)
	LoadGuiFont_(nullptr, (float) cfg.font_size);

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

	// Start a new ImGui frame BEFORE processing commands so dimensions are correct
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL2_NewFrame(window_);
	ImGui::NewFrame();

	// Update editor logical rows/cols using current ImGui metrics and display size
	{
		ImGuiIO &io  = ImGui::GetIO();
		float line_h = ImGui::GetTextLineHeightWithSpacing();
		float ch_w   = ImGui::CalcTextSize("M").x;
		if (line_h <= 0.0f)
			line_h = 16.0f;
		if (ch_w <= 0.0f)
			ch_w = 8.0f;
		// Prefer ImGui IO display size; fall back to cached SDL window size
		float disp_w = io.DisplaySize.x > 0 ? io.DisplaySize.x : static_cast<float>(width_);
		float disp_h = io.DisplaySize.y > 0 ? io.DisplaySize.y : static_cast<float>(height_);

		// Account for the GUI window padding and the status bar height used in GUIRenderer
		const ImGuiStyle &style = ImGui::GetStyle();
		float pad_x             = style.WindowPadding.x;
		float pad_y             = style.WindowPadding.y;
		// Status bar reserves one frame height (with spacing) inside the window
		float status_h = ImGui::GetFrameHeightWithSpacing();

		float avail_w = std::max(0.0f, disp_w - 2.0f * pad_x);
		float avail_h = std::max(0.0f, disp_h - 2.0f * pad_y - status_h);

		// Visible content rows inside the scroll child
		auto content_rows = static_cast<std::size_t>(std::floor(avail_h / line_h));
		// Editor::Rows includes the status line; add 1 back for it.
		std::size_t rows = std::max<std::size_t>(1, content_rows + 1);
		std::size_t cols = static_cast<std::size_t>(std::max(1.0f, std::floor(avail_w / ch_w)));

		// Only update if changed to avoid churn
		if (rows != ed.Rows() || cols != ed.Cols()) {
			ed.SetDimensions(rows, cols);
		}
	}

	// Execute pending mapped inputs (drain queue) AFTER dimensions are updated
	for (;;) {
		MappedInput mi;
		if (!input_.Poll(mi))
			break;
		if (mi.hasCommand) {
			// Track kill ring before and after to sync GUI clipboard when it changes
			const std::string before = ed.KillRingHead();
			Execute(ed, mi.id, mi.arg, mi.count);
			const std::string after = ed.KillRingHead();
			if (after != before && !after.empty()) {
				// Update the system clipboard to mirror the kill ring head in GUI
				SDL_SetClipboardText(after.c_str());
			}
		}
	}

	if (ed.QuitRequested()) {
		running = false;
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
GUIFrontend::LoadGuiFont_(const char * /*path*/, const float size_px)
{
	const ImGuiIO &io = ImGui::GetIO();
	io.Fonts->Clear();
	const ImFont *font = io.Fonts->AddFontFromMemoryCompressedTTF(
		kte::Fonts::DefaultFontData,
		kte::Fonts::DefaultFontSize,
		size_px);
	if (!font) {
		font = io.Fonts->AddFontDefault();
	}
	(void) font;
	io.Fonts->Build();
	return true;
}