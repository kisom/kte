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

#include "ImGuiFrontend.h"
#include "Command.h"
#include "Editor.h"
#include "GUIConfig.h"
#include "GUITheme.h"
#include "fonts/Font.h" // embedded default font (DefaultFont)
#include "fonts/FontRegistry.h"
#include "fonts/IosevkaExtended.h"
#include "syntax/HighlighterRegistry.h"
#include "syntax/NullHighlighter.h"


#ifndef KTE_FONT_SIZE
#define KTE_FONT_SIZE 16.0f
#endif

static auto kGlslVersion = "#version 150"; // GL 3.2 core (macOS compatible)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void
apply_syntax_to_buffer(Buffer *b, const GUIConfig &cfg)
{
	if (!b)
		return;
	if (cfg.syntax) {
		b->SetSyntaxEnabled(true);
		b->EnsureHighlighter();
		if (auto *eng = b->Highlighter()) {
			if (!eng->HasHighlighter()) {
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


// Update editor logical rows/cols from current ImGui metrics for a given display size.
static void
update_editor_dimensions(Editor &ed, float disp_w, float disp_h)
{
	float row_h = ImGui::GetTextLineHeightWithSpacing();
	float ch_w  = ImGui::CalcTextSize("M").x;
	if (row_h <= 0.0f)
		row_h = 16.0f;
	if (ch_w <= 0.0f)
		ch_w = 8.0f;

	const float pad_x = 6.0f;
	const float pad_y = 6.0f;

	float wanted_bar_h   = ImGui::GetFrameHeight();
	float total_avail_h  = std::max(0.0f, disp_h - 2.0f * pad_y);
	float actual_avail_h = std::floor((total_avail_h - wanted_bar_h) / row_h) * row_h;

	auto content_rows = static_cast<std::size_t>(std::max(0.0f, std::floor(actual_avail_h / row_h)));
	std::size_t rows  = content_rows + 1;

	float avail_w    = std::max(0.0f, disp_w - 2.0f * pad_x);
	std::size_t cols = static_cast<std::size_t>(std::max(1.0f, std::floor(avail_w / ch_w)));

	if (rows != ed.Rows() || cols != ed.Cols()) {
		ed.SetDimensions(rows, cols);
	}
}


// ---------------------------------------------------------------------------
// SetupImGuiStyle_ — apply theme, fonts, and flags to the current ImGui context
// ---------------------------------------------------------------------------
void
GUIFrontend::SetupImGuiStyle_()
{
	ImGuiIO &io = ImGui::GetIO();

	// Disable imgui.ini for secondary windows (primary sets its own path in Init)
	io.IniFilename = nullptr;

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	ImGui::StyleColorsDark();

	if (config_.background == "light")
		kte::SetBackgroundMode(kte::BackgroundMode::Light);
	else
		kte::SetBackgroundMode(kte::BackgroundMode::Dark);
	kte::ApplyThemeByName(config_.theme);

	// Load fonts into this context's font atlas.
	// Font registry is global and already populated by Init; just load into this atlas.
	if (!kte::Fonts::FontRegistry::Instance().LoadFont(config_.font, (float) config_.font_size)) {
		LoadGuiFont_(nullptr, (float) config_.font_size);
	}
}


// ---------------------------------------------------------------------------
// Destroy a single window's ImGui context + SDL/GL resources
// ---------------------------------------------------------------------------
void
GUIFrontend::DestroyWindowResources_(WindowState &ws)
{
	if (ws.imgui_ctx) {
		// Must activate this window's GL context before shutting down the
		// OpenGL3 backend, otherwise it deletes another context's resources.
		if (ws.window && ws.gl_ctx)
			SDL_GL_MakeCurrent(ws.window, ws.gl_ctx);
		ImGui::SetCurrentContext(ws.imgui_ctx);
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext(ws.imgui_ctx);
		ws.imgui_ctx = nullptr;
	}
	if (ws.gl_ctx) {
		SDL_GL_DeleteContext(ws.gl_ctx);
		ws.gl_ctx = nullptr;
	}
	if (ws.window) {
		SDL_DestroyWindow(ws.window);
		ws.window = nullptr;
	}
}


bool
GUIFrontend::Init(int &argc, char **argv, Editor &ed)
{
	(void) argc;
	(void) argv;

	// Load GUI configuration (fullscreen, columns/rows, font size, theme, background)
	config_ = GUIConfig::Load();

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

	// Compute desired window size from config
	Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

	int init_w = 1280, init_h = 800;
	if (config_.fullscreen) {
		SDL_Rect usable{};
		if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
			init_w = usable.w;
			init_h = usable.h;
		}
#if !defined(__APPLE__)
		win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
	} else {
		int w = config_.columns * static_cast<int>(config_.font_size);
		int h = config_.rows * static_cast<int>(config_.font_size * 1.2);
		SDL_Rect usable{};
		if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
			w = std::min(w, usable.w);
			h = std::min(h, usable.h);
		}
		init_w = std::max(320, w);
		init_h = std::max(200, h);
	}

	SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
	SDL_Window *win = SDL_CreateWindow(
		"kge - kyle's graphical editor " KTE_VERSION_STR,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		init_w, init_h,
		win_flags);
	if (!win) {
		return false;
	}

	SDL_EnableScreenSaver();

#if defined(__APPLE__)
	if (config_.fullscreen) {
		SDL_Rect usable{};
		if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
			SDL_SetWindowPosition(win, usable.x, usable.y);
		}
	}
#endif

	SDL_GLContext gl_ctx = SDL_GL_CreateContext(win);
	if (!gl_ctx) {
		SDL_DestroyWindow(win);
		return false;
	}
	SDL_GL_MakeCurrent(win, gl_ctx);
	SDL_GL_SetSwapInterval(1); // vsync

	// Create primary ImGui context
	IMGUI_CHECKVERSION();
	ImGuiContext *imgui_ctx = ImGui::CreateContext();
	ImGuiIO &io             = ImGui::GetIO();

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

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	ImGui::StyleColorsDark();

	if (config_.background == "light")
		kte::SetBackgroundMode(kte::BackgroundMode::Light);
	else
		kte::SetBackgroundMode(kte::BackgroundMode::Dark);
	kte::ApplyThemeByName(config_.theme);

	apply_syntax_to_buffer(ed.CurrentBuffer(), config_);

	if (!ImGui_ImplSDL2_InitForOpenGL(win, gl_ctx))
		return false;
	if (!ImGui_ImplOpenGL3_Init(kGlslVersion))
		return false;

	// Cache initial window size
	int w, h;
	SDL_GetWindowSize(win, &w, &h);
	init_w = w;
	init_h = h;

#if defined(__APPLE__)
	if (w > 1 && h > 1) {
		SDL_SetWindowSize(win, w - 1, h - 1);
		SDL_SetWindowSize(win, w, h);
		SDL_GetWindowSize(win, &w, &h);
		init_w = w;
		init_h = h;
	}
#endif

	// Install embedded fonts
	kte::Fonts::InstallDefaultFonts();
	if (!kte::Fonts::FontRegistry::Instance().LoadFont(config_.font, (float) config_.font_size)) {
		LoadGuiFont_(nullptr, (float) config_.font_size);
		kte::Fonts::FontRegistry::Instance().RequestLoadFont("default", (float) config_.font_size);
		std::string n;
		float s = 0.0f;
		if (kte::Fonts::FontRegistry::Instance().ConsumePendingFontRequest(n, s)) {
			kte::Fonts::FontRegistry::Instance().LoadFont(n, s);
		}
	}

	// Build primary WindowState
	auto ws       = std::make_unique<WindowState>();
	ws->window    = win;
	ws->gl_ctx    = gl_ctx;
	ws->imgui_ctx = imgui_ctx;
	ws->width     = init_w;
	ws->height    = init_h;
	// The primary window's editor IS the editor passed in from main; we don't
	// use ws->editor for the primary — instead we keep a pointer to &ed.
	// We store a sentinel: window index 0 uses the external editor reference.
	// To keep things simple, attach input to the passed-in editor.
	ws->input.Attach(&ed);
	windows_.push_back(std::move(ws));

	return true;
}


bool
GUIFrontend::OpenNewWindow_(Editor &primary)
{
	Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	int w            = windows_[0]->width;
	int h            = windows_[0]->height;

	SDL_Window *win = SDL_CreateWindow(
		"kge - kyle's graphical editor " KTE_VERSION_STR,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		w, h,
		win_flags);
	if (!win)
		return false;

	SDL_GLContext gl_ctx = SDL_GL_CreateContext(win);
	if (!gl_ctx) {
		SDL_DestroyWindow(win);
		return false;
	}
	SDL_GL_MakeCurrent(win, gl_ctx);
	SDL_GL_SetSwapInterval(1);

	// Each window gets its own ImGui context — ImGui requires exactly one
	// NewFrame/Render cycle per context per frame.
	ImGuiContext *imgui_ctx = ImGui::CreateContext();
	ImGui::SetCurrentContext(imgui_ctx);

	SetupImGuiStyle_();

	if (!ImGui_ImplSDL2_InitForOpenGL(win, gl_ctx)) {
		ImGui::DestroyContext(imgui_ctx);
		SDL_GL_DeleteContext(gl_ctx);
		SDL_DestroyWindow(win);
		return false;
	}
	if (!ImGui_ImplOpenGL3_Init(kGlslVersion)) {
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext(imgui_ctx);
		SDL_GL_DeleteContext(gl_ctx);
		SDL_DestroyWindow(win);
		return false;
	}

	auto ws       = std::make_unique<WindowState>();
	ws->window    = win;
	ws->gl_ctx    = gl_ctx;
	ws->imgui_ctx = imgui_ctx;
	ws->width     = w;
	ws->height    = h;

	// Secondary editor shares the primary's buffer list
	ws->editor.SetSharedBuffers(&primary.Buffers());
	ws->editor.SetDimensions(primary.Rows(), primary.Cols());

	// Open a new untitled buffer and switch to it in the new window.
	ws->editor.AddBuffer(Buffer());
	ws->editor.SwitchTo(ws->editor.BufferCount() - 1);

	ws->input.Attach(&ws->editor);

	windows_.push_back(std::move(ws));

	// Restore primary context
	ImGui::SetCurrentContext(windows_[0]->imgui_ctx);
	SDL_GL_MakeCurrent(windows_[0]->window, windows_[0]->gl_ctx);
	return true;
}


void
GUIFrontend::Step(Editor &ed, bool &running)
{
	// --- Event processing ---
	// SDL events carry a window ID. Route each event to the correct window's
	// ImGui context (for ImGui_ImplSDL2_ProcessEvent) and input handler.
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		// Determine which window this event belongs to
		Uint32 event_win_id = 0;
		switch (e.type) {
		case SDL_WINDOWEVENT:
			event_win_id = e.window.windowID;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			event_win_id = e.key.windowID;
			break;
		case SDL_TEXTINPUT:
			event_win_id = e.text.windowID;
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			event_win_id = e.button.windowID;
			break;
		case SDL_MOUSEWHEEL:
			event_win_id = e.wheel.windowID;
			break;
		case SDL_MOUSEMOTION:
			event_win_id = e.motion.windowID;
			break;
		default:
			break;
		}

		if (e.type == SDL_QUIT) {
			running = false;
			break;
		}

		// Find the target window and route the event to its ImGui context
		WindowState *target = nullptr;
		std::size_t target_idx = 0;
		if (event_win_id != 0) {
			for (std::size_t i = 0; i < windows_.size(); ++i) {
				if (SDL_GetWindowID(windows_[i]->window) == event_win_id) {
					target     = windows_[i].get();
					target_idx = i;
					break;
				}
			}
		}

		if (target && target->imgui_ctx) {
			// Set this window's ImGui context so ImGui_ImplSDL2_ProcessEvent
			// updates the correct IO state.
			ImGui::SetCurrentContext(target->imgui_ctx);
			ImGui_ImplSDL2_ProcessEvent(&e);
		}

		if (e.type == SDL_WINDOWEVENT) {
			if (e.window.event == SDL_WINDOWEVENT_CLOSE) {
				if (target) {
					if (target_idx == 0) {
						running = false;
					} else {
						target->alive = false;
					}
				}
			} else if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
				if (target) {
					target->width  = e.window.data1;
					target->height = e.window.data2;
				}
			}
		}

		// Route input events to the correct window's input handler
		if (target) {
			target->input.ProcessSDLEvent(e);
		}
	}

	if (!running)
		return;

	// --- Apply pending font change (to all contexts) ---
	{
		std::string fname;
		float fsize = 0.0f;
		if (kte::Fonts::FontRegistry::Instance().ConsumePendingFontRequest(fname, fsize)) {
			if (!fname.empty() && fsize > 0.0f) {
				for (auto &ws : windows_) {
					if (!ws->alive || !ws->imgui_ctx)
						continue;
					ImGui::SetCurrentContext(ws->imgui_ctx);
					SDL_GL_MakeCurrent(ws->window, ws->gl_ctx);
					kte::Fonts::FontRegistry::Instance().LoadFont(fname, fsize);
					ImGui_ImplOpenGL3_DestroyFontsTexture();
					ImGui_ImplOpenGL3_CreateFontsTexture();
				}
			}
		}
	}

	// --- Step each window ---
	// We iterate by index because OpenNewWindow_ may append to windows_.
	for (std::size_t wi = 0; wi < windows_.size(); ++wi) {
		WindowState &ws = *windows_[wi];
		if (!ws.alive)
			continue;

		Editor &wed = (wi == 0) ? ed : ws.editor;

		// Shared buffer list may have been modified by another window.
		wed.ValidateBufferIndex();

		// Activate this window's GL and ImGui contexts
		SDL_GL_MakeCurrent(ws.window, ws.gl_ctx);
		ImGui::SetCurrentContext(ws.imgui_ctx);

		// Start a new ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplSDL2_NewFrame(ws.window);
		ImGui::NewFrame();

		// Update editor dimensions
		{
			ImGuiIO &io  = ImGui::GetIO();
			float disp_w = io.DisplaySize.x > 0 ? io.DisplaySize.x : static_cast<float>(ws.width);
			float disp_h = io.DisplaySize.y > 0 ? io.DisplaySize.y : static_cast<float>(ws.height);
			update_editor_dimensions(wed, disp_w, disp_h);
		}

		// Allow deferred opens
		wed.ProcessPendingOpens();

		// Drain input queue
		for (;;) {
			MappedInput mi;
			if (!ws.input.Poll(mi))
				break;
			if (mi.hasCommand) {
				if (mi.id == CommandId::NewWindow) {
					// Open a new window; handled after this loop
					wed.SetNewWindowRequested(true);
				} else if (mi.id == CommandId::FontZoomIn ||
				           mi.id == CommandId::FontZoomOut ||
				           mi.id == CommandId::FontZoomReset) {
					auto &fr = kte::Fonts::FontRegistry::Instance();
					float cur = fr.CurrentFontSize();
					if (cur <= 0.0f) cur = config_.font_size;
					float next = cur;
					if (mi.id == CommandId::FontZoomIn)
						next = std::min(cur + 2.0f, 72.0f);
					else if (mi.id == CommandId::FontZoomOut)
						next = std::max(cur - 2.0f, 8.0f);
					else
						next = config_.font_size; // reset to config default
					if (next != cur)
						fr.RequestLoadFont(fr.CurrentFontName(), next);
				} else {
					const std::string before = wed.KillRingHead();
					Execute(wed, mi.id, mi.arg, mi.count);
					const std::string after = wed.KillRingHead();
					if (after != before && !after.empty()) {
						SDL_SetClipboardText(after.c_str());
					}
				}
			}
		}

		if (wi == 0 && wed.QuitRequested()) {
			running = false;
		}

		// Draw
		ws.renderer.Draw(wed);

		// Render
		ImGui::Render();
		int display_w, display_h;
		SDL_GL_GetDrawableSize(ws.window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(0.1f, 0.1f, 0.11f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(ws.window);
	}

	// Handle deferred new-window requests (must happen outside the render loop
	// to avoid corrupting an in-progress ImGui frame).
	for (std::size_t wi = 0; wi < windows_.size(); ++wi) {
		Editor &wed = (wi == 0) ? ed : windows_[wi]->editor;
		if (wed.NewWindowRequested()) {
			wed.SetNewWindowRequested(false);
			OpenNewWindow_(ed);
		}
	}

	// Remove dead secondary windows
	for (auto it = windows_.begin() + 1; it != windows_.end();) {
		if (!(*it)->alive) {
			DestroyWindowResources_(**it);
			it = windows_.erase(it);
		} else {
			++it;
		}
	}

	// Restore primary context
	if (!windows_.empty()) {
		ImGui::SetCurrentContext(windows_[0]->imgui_ctx);
		SDL_GL_MakeCurrent(windows_[0]->window, windows_[0]->gl_ctx);
	}
}


void
GUIFrontend::Shutdown()
{
	// Destroy all windows (secondary first, then primary)
	for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
		DestroyWindowResources_(**it);
	}
	windows_.clear();
	SDL_Quit();
}


bool
GUIFrontend::LoadGuiFont_(const char * /*path*/, const float size_px)
{
	const ImGuiIO &io = ImGui::GetIO();
	io.Fonts->Clear();

	ImFontConfig config;
	config.MergeMode = false;

	io.Fonts->AddFontFromMemoryCompressedTTF(
		kte::Fonts::DefaultFontData,
		kte::Fonts::DefaultFontSize,
		size_px,
		&config,
		io.Fonts->GetGlyphRangesDefault());

	config.MergeMode                       = true;
	static const ImWchar extended_ranges[] = {
		0x0370, 0x03FF, // Greek and Coptic
		0x2200, 0x22FF, // Mathematical Operators
		0,
	};
	io.Fonts->AddFontFromMemoryCompressedTTF(
		kte::Fonts::IosevkaExtended::DefaultFontRegularCompressedData,
		kte::Fonts::IosevkaExtended::DefaultFontRegularCompressedSize,
		size_px,
		&config,
		extended_ranges);

	io.Fonts->Build();
	return true;
}
