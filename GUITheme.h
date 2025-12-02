// GUITheme.h — ImGui theming helpers and background mode
#pragma once

#include <imgui.h>
#include <vector>
#include <memory>
#include <string>
#include <cstddef>
#include <algorithm>
#include <cctype>

// Small helper to convert packed RGB (0xRRGGBB) + optional alpha to ImVec4
static inline ImVec4
RGBA(unsigned int rgb, float a = 1.0f)
{
	const float r = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
	const float g = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
	const float b = static_cast<float>(rgb & 0xFF) / 255.0f;
	return ImVec4(r, g, b, a);
}


namespace kte {
// Background mode selection for light/dark palettes
enum class BackgroundMode { Light, Dark };

// Global background mode; default to Dark to match prior defaults
static inline BackgroundMode gBackgroundMode = BackgroundMode::Dark;

// Basic theme identifier (kept minimal; some ids are aliases)
enum class ThemeId {
	EInk = 0,
	GruvboxDarkMedium = 1,
	GruvboxLightMedium = 1, // alias to unified gruvbox index
	Nord = 2,
	Plan9 = 3,
	Solarized = 4,
};

// Current theme tracking
static inline ThemeId gCurrentTheme          = ThemeId::Nord;
static inline std::size_t gCurrentThemeIndex = 0;

// Forward declarations for helpers used below
static inline size_t ThemeIndexFromId(ThemeId id);

static inline ThemeId ThemeIdFromIndex(size_t idx);

// Helpers to set/query background mode
static inline void
SetBackgroundMode(BackgroundMode m)
{
	gBackgroundMode = m;
}


static inline BackgroundMode
GetBackgroundMode()
{
	return gBackgroundMode;
}


static inline const char *
BackgroundModeName()
{
	return gBackgroundMode == BackgroundMode::Light ? "light" : "dark";
}


// Apply a Nord-inspired theme to the current ImGui style.
// Safe to call after ImGui::CreateContext().
static inline void
ApplyNordImGuiTheme()
{
	// Nord palette
	const ImVec4 nord0  = RGBA(0x2E3440); // darkest bg
	const ImVec4 nord1  = RGBA(0x3B4252);
	const ImVec4 nord2  = RGBA(0x434C5E);
	const ImVec4 nord3  = RGBA(0x4C566A);
	const ImVec4 nord4  = RGBA(0xD8DEE9);
	const ImVec4 nord6  = RGBA(0xECEFF4); // lightest
	const ImVec4 nord8  = RGBA(0x88C0D0); // cyan
	const ImVec4 nord9  = RGBA(0x81A1C1); // blue
	const ImVec4 nord10 = RGBA(0x5E81AC); // blue dark
	const ImVec4 nord12 = RGBA(0xD08770); // orange
	const ImVec4 nord13 = RGBA(0xEBCB8B); // yellow

	ImGuiStyle &style = ImGui::GetStyle();

	// Base style tweaks to suit Nord aesthetics
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 4.0f;
	style.FrameRounding    = 3.0f;
	style.PopupRounding    = 4.0f;
	style.GrabRounding     = 3.0f;
	style.TabRounding      = 4.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 1.0f;

	ImVec4 *colors = style.Colors;

	colors[ImGuiCol_Text]         = nord4; // primary text
	colors[ImGuiCol_TextDisabled] = ImVec4(nord4.x, nord4.y, nord4.z, 0.55f);
	colors[ImGuiCol_WindowBg]     = nord10;
	colors[ImGuiCol_ChildBg]      = nord0;
	colors[ImGuiCol_PopupBg]      = RGBA(0x2E3440, 0.98f);
	colors[ImGuiCol_Border]       = nord1;
	colors[ImGuiCol_BorderShadow] = RGBA(0x000000, 0.0f);

	colors[ImGuiCol_FrameBg]        = nord2;
	colors[ImGuiCol_FrameBgHovered] = nord3;
	colors[ImGuiCol_FrameBgActive]  = nord10;

	colors[ImGuiCol_TitleBg]          = nord1;
	colors[ImGuiCol_TitleBgActive]    = nord3;
	colors[ImGuiCol_TitleBgCollapsed] = nord1;

	colors[ImGuiCol_MenuBarBg]            = nord1;
	colors[ImGuiCol_ScrollbarBg]          = nord0;
	colors[ImGuiCol_ScrollbarGrab]        = nord3;
	colors[ImGuiCol_ScrollbarGrabHovered] = nord10;
	colors[ImGuiCol_ScrollbarGrabActive]  = nord9;

	colors[ImGuiCol_CheckMark]        = nord8;
	colors[ImGuiCol_SliderGrab]       = nord8;
	colors[ImGuiCol_SliderGrabActive] = nord9;

	colors[ImGuiCol_Button]        = nord3;
	colors[ImGuiCol_ButtonHovered] = nord10;
	colors[ImGuiCol_ButtonActive]  = nord9;

	colors[ImGuiCol_Header]        = nord3;
	colors[ImGuiCol_HeaderHovered] = nord10;
	colors[ImGuiCol_HeaderActive]  = nord10;

	colors[ImGuiCol_Separator]        = nord2;
	colors[ImGuiCol_SeparatorHovered] = nord10;
	colors[ImGuiCol_SeparatorActive]  = nord9;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(nord6.x, nord6.y, nord6.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(nord8.x, nord8.y, nord8.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive]  = nord9;

	colors[ImGuiCol_Tab]                = nord2;
	colors[ImGuiCol_TabHovered]         = nord10;
	colors[ImGuiCol_TabActive]          = nord3;
	colors[ImGuiCol_TabUnfocused]       = nord2;
	colors[ImGuiCol_TabUnfocusedActive] = nord3;

	// Docking colors are available only when docking branch is enabled; omit for compatibility

	colors[ImGuiCol_TableHeaderBg]     = nord2;
	colors[ImGuiCol_TableBorderStrong] = nord1;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(nord1.x, nord1.y, nord1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(nord1.x, nord1.y, nord1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(nord1.x, nord1.y, nord1.z, 0.35f);

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(nord8.x, nord8.y, nord8.z, 0.35f);
	colors[ImGuiCol_DragDropTarget]        = nord13;
	colors[ImGuiCol_NavHighlight]          = nord9;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(nord6.x, nord6.y, nord6.z, 0.7f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(nord0.x, nord0.y, nord0.z, 0.6f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(nord0.x, nord0.y, nord0.z, 0.6f);

	// Plots
	colors[ImGuiCol_PlotLines]            = nord8;
	colors[ImGuiCol_PlotLinesHovered]     = nord9;
	colors[ImGuiCol_PlotHistogram]        = nord13;
	colors[ImGuiCol_PlotHistogramHovered] = nord12;
}


// Apply a single Plan 9 acme-inspired theme (no light/dark variants)
// Palette: light yellow paper, black text, thin black borders, bright blue accents.
static inline void
ApplyPlan9Theme()
{
	// Acme-like colors
	const ImVec4 paper  = RGBA(0xFFFFE8); // pale yellow paper
	const ImVec4 pane   = RGBA(0xFFF4C1); // slightly deeper for frames
	const ImVec4 ink    = RGBA(0x000000); // black text
	const ImVec4 dim    = ImVec4(0, 0, 0, 0.60f);
	const ImVec4 border = RGBA(0x000000); // 1px black
	const ImVec4 blue   = RGBA(0x0064FF); // acme-ish blue accents
	const ImVec4 blueH  = RGBA(0x4C8DFF); // hover/active

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(6.0f, 6.0f);
	style.FramePadding     = ImVec2(5.0f, 3.0f);
	style.CellPadding      = ImVec2(5.0f, 3.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 0.0f;
	style.FrameRounding    = 0.0f;
	style.PopupRounding    = 0.0f;
	style.GrabRounding     = 0.0f;
	style.TabRounding      = 0.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 1.0f;

	ImVec4 *c                         = style.Colors;
	c[ImGuiCol_Text]                  = ink;
	c[ImGuiCol_TextDisabled]          = dim;
	c[ImGuiCol_WindowBg]              = paper;
	c[ImGuiCol_ChildBg]               = paper;
	c[ImGuiCol_PopupBg]               = ImVec4(pane.x, pane.y, pane.z, 0.98f);
	c[ImGuiCol_Border]                = border;
	c[ImGuiCol_BorderShadow]          = RGBA(0x000000, 0.0f);
	c[ImGuiCol_FrameBg]               = pane;
	c[ImGuiCol_FrameBgHovered]        = RGBA(0xFFEBA0);
	c[ImGuiCol_FrameBgActive]         = RGBA(0xFFE387);
	c[ImGuiCol_TitleBg]               = pane;
	c[ImGuiCol_TitleBgActive]         = RGBA(0xFFE8A6);
	c[ImGuiCol_TitleBgCollapsed]      = pane;
	c[ImGuiCol_MenuBarBg]             = pane;
	c[ImGuiCol_ScrollbarBg]           = paper;
	c[ImGuiCol_ScrollbarGrab]         = RGBA(0xEADFA5);
	c[ImGuiCol_ScrollbarGrabHovered]  = RGBA(0xE2D37F);
	c[ImGuiCol_ScrollbarGrabActive]   = RGBA(0xD8C757);
	c[ImGuiCol_CheckMark]             = blue;
	c[ImGuiCol_SliderGrab]            = blue;
	c[ImGuiCol_SliderGrabActive]      = blueH;
	c[ImGuiCol_Button]                = RGBA(0xFFF1B0);
	c[ImGuiCol_ButtonHovered]         = RGBA(0xFFE892);
	c[ImGuiCol_ButtonActive]          = RGBA(0xFFE072);
	c[ImGuiCol_Header]                = RGBA(0xFFF1B0);
	c[ImGuiCol_HeaderHovered]         = RGBA(0xFFE892);
	c[ImGuiCol_HeaderActive]          = RGBA(0xFFE072);
	c[ImGuiCol_Separator]             = border;
	c[ImGuiCol_SeparatorHovered]      = blue;
	c[ImGuiCol_SeparatorActive]       = blueH;
	c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0.12f);
	c[ImGuiCol_ResizeGripHovered]     = ImVec4(blue.x, blue.y, blue.z, 0.67f);
	c[ImGuiCol_ResizeGripActive]      = blueH;
	c[ImGuiCol_Tab]                   = RGBA(0xFFE8A6);
	c[ImGuiCol_TabHovered]            = RGBA(0xFFE072);
	c[ImGuiCol_TabActive]             = RGBA(0xFFD859);
	c[ImGuiCol_TabUnfocused]          = RGBA(0xFFE8A6);
	c[ImGuiCol_TabUnfocusedActive]    = RGBA(0xFFD859);
	c[ImGuiCol_TableHeaderBg]         = RGBA(0xFFE8A6);
	c[ImGuiCol_TableBorderStrong]     = border;
	c[ImGuiCol_TableBorderLight]      = ImVec4(0, 0, 0, 0.35f);
	c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0.04f);
	c[ImGuiCol_TableRowBgAlt]         = ImVec4(0, 0, 0, 0.08f);
	c[ImGuiCol_TextSelectedBg]        = ImVec4(blueH.x, blueH.y, blueH.z, 0.35f);
	c[ImGuiCol_DragDropTarget]        = blue;
	c[ImGuiCol_NavHighlight]          = blue;
	c[ImGuiCol_NavWindowingHighlight] = ImVec4(0, 0, 0, 0.20f);
	c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, 0.20f);
	c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.20f);
	c[ImGuiCol_PlotLines]             = blue;
	c[ImGuiCol_PlotLinesHovered]      = blueH;
	c[ImGuiCol_PlotHistogram]         = blue;
	c[ImGuiCol_PlotHistogramHovered]  = blueH;
}


// Apply Solarized (Dark)
static inline void
ApplySolarizedDarkTheme()
{
	// Base colors from Ethan Schoonover Solarized
	const ImVec4 base03 = RGBA(0x002b36);
	const ImVec4 base02 = RGBA(0x073642);
	const ImVec4 base01 = RGBA(0x586e75);
	const ImVec4 base00 = RGBA(0x657b83);
	const ImVec4 base0  = RGBA(0x839496);
	const ImVec4 base1  = RGBA(0x93a1a1);
	const ImVec4 base2  = RGBA(0xeee8d5);
	const ImVec4 yellow = RGBA(0xb58900);
	const ImVec4 orange = RGBA(0xcb4b16);
	const ImVec4 blue   = RGBA(0x268bd2);
	const ImVec4 cyan   = RGBA(0x2aa198);
	// Note: red, magenta, violet, green and base3 are intentionally omitted until used

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 3.0f;
	style.FrameRounding    = 3.0f;
	style.PopupRounding    = 3.0f;
	style.GrabRounding     = 3.0f;
	style.TabRounding      = 3.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 1.0f;

	ImVec4 *c                         = style.Colors;
	c[ImGuiCol_Text]                  = base0;
	c[ImGuiCol_TextDisabled]          = ImVec4(base01.x, base01.y, base01.z, 1.0f);
	c[ImGuiCol_WindowBg]              = base03;
	c[ImGuiCol_ChildBg]               = base03;
	c[ImGuiCol_PopupBg]               = ImVec4(base02.x, base02.y, base02.z, 0.98f);
	c[ImGuiCol_Border]                = base02;
	c[ImGuiCol_BorderShadow]          = RGBA(0x000000, 0.0f);
	c[ImGuiCol_FrameBg]               = base02;
	c[ImGuiCol_FrameBgHovered]        = base01;
	c[ImGuiCol_FrameBgActive]         = base00;
	c[ImGuiCol_TitleBg]               = base02;
	c[ImGuiCol_TitleBgActive]         = base01;
	c[ImGuiCol_TitleBgCollapsed]      = base02;
	c[ImGuiCol_MenuBarBg]             = base02;
	c[ImGuiCol_ScrollbarBg]           = base02;
	c[ImGuiCol_ScrollbarGrab]         = base01;
	c[ImGuiCol_ScrollbarGrabHovered]  = base00;
	c[ImGuiCol_ScrollbarGrabActive]   = blue;
	c[ImGuiCol_CheckMark]             = cyan;
	c[ImGuiCol_SliderGrab]            = cyan;
	c[ImGuiCol_SliderGrabActive]      = blue;
	c[ImGuiCol_Button]                = base01;
	c[ImGuiCol_ButtonHovered]         = base00;
	c[ImGuiCol_ButtonActive]          = blue;
	c[ImGuiCol_Header]                = base01;
	c[ImGuiCol_HeaderHovered]         = base00;
	c[ImGuiCol_HeaderActive]          = base00;
	c[ImGuiCol_Separator]             = base01;
	c[ImGuiCol_SeparatorHovered]      = base00;
	c[ImGuiCol_SeparatorActive]       = blue;
	c[ImGuiCol_ResizeGrip]            = ImVec4(base1.x, base1.y, base1.z, 0.12f);
	c[ImGuiCol_ResizeGripHovered]     = ImVec4(cyan.x, cyan.y, cyan.z, 0.67f);
	c[ImGuiCol_ResizeGripActive]      = blue;
	c[ImGuiCol_Tab]                   = base01;
	c[ImGuiCol_TabHovered]            = base00;
	c[ImGuiCol_TabActive]             = base02;
	c[ImGuiCol_TabUnfocused]          = base01;
	c[ImGuiCol_TabUnfocusedActive]    = base02;
	c[ImGuiCol_TableHeaderBg]         = base01;
	c[ImGuiCol_TableBorderStrong]     = base00;
	c[ImGuiCol_TableBorderLight]      = ImVec4(base00.x, base00.y, base00.z, 0.6f);
	c[ImGuiCol_TableRowBg]            = ImVec4(base02.x, base02.y, base02.z, 0.2f);
	c[ImGuiCol_TableRowBgAlt]         = ImVec4(base02.x, base02.y, base02.z, 0.35f);
	c[ImGuiCol_TextSelectedBg]        = ImVec4(cyan.x, cyan.y, cyan.z, 0.30f);
	c[ImGuiCol_DragDropTarget]        = yellow;
	c[ImGuiCol_NavHighlight]          = blue;
	c[ImGuiCol_NavWindowingHighlight] = ImVec4(base2.x, base2.y, base2.z, 0.70f);
	c[ImGuiCol_NavWindowingDimBg]     = ImVec4(base03.x, base03.y, base03.z, 0.60f);
	c[ImGuiCol_ModalWindowDimBg]      = ImVec4(base03.x, base03.y, base03.z, 0.60f);
	c[ImGuiCol_PlotLines]             = cyan;
	c[ImGuiCol_PlotLinesHovered]      = blue;
	c[ImGuiCol_PlotHistogram]         = yellow;
	c[ImGuiCol_PlotHistogramHovered]  = orange;
}


// Apply Solarized (Light)
static inline void
ApplySolarizedLightTheme()
{
	// Swap base shades for light mode
	const ImVec4 base03 = RGBA(0xfdf6e3);
	const ImVec4 base02 = RGBA(0xeee8d5);
	// base01/base00 not currently used in light variant
	const ImVec4 base0  = RGBA(0x657b83);
	const ImVec4 base1  = RGBA(0x586e75);
	const ImVec4 base2  = RGBA(0x073642);
	const ImVec4 yellow = RGBA(0xb58900);
	const ImVec4 orange = RGBA(0xcb4b16);
	const ImVec4 blue   = RGBA(0x268bd2);
	const ImVec4 cyan   = RGBA(0x2aa198);
	// Note: red, magenta, violet, green and base3 are intentionally omitted until used

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 3.0f;
	style.FrameRounding    = 3.0f;
	style.PopupRounding    = 3.0f;
	style.GrabRounding     = 3.0f;
	style.TabRounding      = 3.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 1.0f;

	ImVec4 *c                         = style.Colors;
	c[ImGuiCol_Text]                  = base0;
	c[ImGuiCol_TextDisabled]          = ImVec4(base1.x, base1.y, base1.z, 1.0f);
	c[ImGuiCol_WindowBg]              = base03;
	c[ImGuiCol_ChildBg]               = base03;
	c[ImGuiCol_PopupBg]               = ImVec4(base02.x, base02.y, base02.z, 0.98f);
	c[ImGuiCol_Border]                = base02;
	c[ImGuiCol_BorderShadow]          = RGBA(0x000000, 0.0f);
	c[ImGuiCol_FrameBg]               = base02;
	c[ImGuiCol_FrameBgHovered]        = base1;
	c[ImGuiCol_FrameBgActive]         = base0;
	c[ImGuiCol_TitleBg]               = base02;
	c[ImGuiCol_TitleBgActive]         = base1;
	c[ImGuiCol_TitleBgCollapsed]      = base02;
	c[ImGuiCol_MenuBarBg]             = base02;
	c[ImGuiCol_ScrollbarBg]           = base02;
	c[ImGuiCol_ScrollbarGrab]         = base1;
	c[ImGuiCol_ScrollbarGrabHovered]  = base0;
	c[ImGuiCol_ScrollbarGrabActive]   = blue;
	c[ImGuiCol_CheckMark]             = cyan;
	c[ImGuiCol_SliderGrab]            = cyan;
	c[ImGuiCol_SliderGrabActive]      = blue;
	c[ImGuiCol_Button]                = base1;
	c[ImGuiCol_ButtonHovered]         = base0;
	c[ImGuiCol_ButtonActive]          = blue;
	c[ImGuiCol_Header]                = base1;
	c[ImGuiCol_HeaderHovered]         = base0;
	c[ImGuiCol_HeaderActive]          = base0;
	c[ImGuiCol_Separator]             = base1;
	c[ImGuiCol_SeparatorHovered]      = base0;
	c[ImGuiCol_SeparatorActive]       = blue;
	c[ImGuiCol_ResizeGrip]            = ImVec4(base1.x, base1.y, base1.z, 0.12f);
	c[ImGuiCol_ResizeGripHovered]     = ImVec4(cyan.x, cyan.y, cyan.z, 0.67f);
	c[ImGuiCol_ResizeGripActive]      = blue;
	c[ImGuiCol_Tab]                   = base1;
	c[ImGuiCol_TabHovered]            = base0;
	c[ImGuiCol_TabActive]             = base2;
	c[ImGuiCol_TabUnfocused]          = base1;
	c[ImGuiCol_TabUnfocusedActive]    = base2;
	c[ImGuiCol_TableHeaderBg]         = base1;
	c[ImGuiCol_TableBorderStrong]     = base0;
	c[ImGuiCol_TableBorderLight]      = ImVec4(base0.x, base0.y, base0.z, 0.6f);
	c[ImGuiCol_TableRowBg]            = ImVec4(base02.x, base02.y, base02.z, 0.2f);
	c[ImGuiCol_TableRowBgAlt]         = ImVec4(base02.x, base02.y, base02.z, 0.35f);
	c[ImGuiCol_TextSelectedBg]        = ImVec4(cyan.x, cyan.y, cyan.z, 0.30f);
	c[ImGuiCol_DragDropTarget]        = yellow;
	c[ImGuiCol_NavHighlight]          = blue;
	c[ImGuiCol_NavWindowingHighlight] = ImVec4(base2.x, base2.y, base2.z, 0.70f);
	c[ImGuiCol_NavWindowingDimBg]     = ImVec4(base03.x, base03.y, base03.z, 0.60f);
	c[ImGuiCol_ModalWindowDimBg]      = ImVec4(base03.x, base03.y, base03.z, 0.60f);
	c[ImGuiCol_PlotLines]             = cyan;
	c[ImGuiCol_PlotLinesHovered]      = blue;
	c[ImGuiCol_PlotHistogram]         = yellow;
	c[ImGuiCol_PlotHistogramHovered]  = orange;
}


// Apply Gruvbox Dark (medium contrast) theme to the current ImGui style
static inline void
ApplyGruvboxDarkMediumTheme()
{
	// Gruvbox (dark, medium) palette
	const ImVec4 bg0 = RGBA(0x282828); // dark0
	const ImVec4 bg1 = RGBA(0x3C3836); // dark1
	const ImVec4 bg2 = RGBA(0x504945); // dark2
	const ImVec4 bg3 = RGBA(0x665C54); // dark3
	const ImVec4 fg1 = RGBA(0xEBDBB2); // light1
	const ImVec4 fg0 = RGBA(0xFBF1C7); // light0
	// accent colors (selected subset used throughout)
	const ImVec4 yellow = RGBA(0xFABD2F);
	const ImVec4 blue   = RGBA(0x83A598);
	const ImVec4 aqua   = RGBA(0x8EC07C);
	const ImVec4 orange = RGBA(0xFE8019);

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 4.0f;
	style.FrameRounding    = 3.0f;
	style.PopupRounding    = 4.0f;
	style.GrabRounding     = 3.0f;
	style.TabRounding      = 4.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 1.0f;

	ImVec4 *colors                = style.Colors;
	colors[ImGuiCol_Text]         = fg1;
	colors[ImGuiCol_TextDisabled] = ImVec4(fg1.x, fg1.y, fg1.z, 0.55f);
	colors[ImGuiCol_WindowBg]     = bg0;
	colors[ImGuiCol_ChildBg]      = bg0;
	colors[ImGuiCol_PopupBg]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	colors[ImGuiCol_Border]       = bg2;
	colors[ImGuiCol_BorderShadow] = RGBA(0x000000, 0.0f);

	colors[ImGuiCol_FrameBg]        = bg2;
	colors[ImGuiCol_FrameBgHovered] = bg3;
	colors[ImGuiCol_FrameBgActive]  = bg1;

	colors[ImGuiCol_TitleBg]          = bg1;
	colors[ImGuiCol_TitleBgActive]    = bg2;
	colors[ImGuiCol_TitleBgCollapsed] = bg1;

	colors[ImGuiCol_MenuBarBg]            = bg1;
	colors[ImGuiCol_ScrollbarBg]          = bg0;
	colors[ImGuiCol_ScrollbarGrab]        = bg3;
	colors[ImGuiCol_ScrollbarGrabHovered] = bg2;
	colors[ImGuiCol_ScrollbarGrabActive]  = bg1;

	colors[ImGuiCol_CheckMark]        = aqua;
	colors[ImGuiCol_SliderGrab]       = aqua;
	colors[ImGuiCol_SliderGrabActive] = blue;

	colors[ImGuiCol_Button]        = bg3;
	colors[ImGuiCol_ButtonHovered] = bg2;
	colors[ImGuiCol_ButtonActive]  = bg1;

	colors[ImGuiCol_Header]        = bg3;
	colors[ImGuiCol_HeaderHovered] = bg2;
	colors[ImGuiCol_HeaderActive]  = bg2;

	colors[ImGuiCol_Separator]        = bg2;
	colors[ImGuiCol_SeparatorHovered] = bg1;
	colors[ImGuiCol_SeparatorActive]  = blue;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(fg0.x, fg0.y, fg0.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(aqua.x, aqua.y, aqua.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive]  = blue;

	colors[ImGuiCol_Tab]                = bg2;
	colors[ImGuiCol_TabHovered]         = bg1;
	colors[ImGuiCol_TabActive]          = bg3;
	colors[ImGuiCol_TabUnfocused]       = bg2;
	colors[ImGuiCol_TabUnfocusedActive] = bg3;

	colors[ImGuiCol_TableHeaderBg]     = bg2;
	colors[ImGuiCol_TableBorderStrong] = bg1;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(bg1.x, bg1.y, bg1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(aqua.x, aqua.y, aqua.z, 0.30f);
	colors[ImGuiCol_DragDropTarget]        = yellow;
	colors[ImGuiCol_NavHighlight]          = blue;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(fg0.x, fg0.y, fg0.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(bg0.x, bg0.y, bg0.z, 0.60f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(bg0.x, bg0.y, bg0.z, 0.60f);

	colors[ImGuiCol_PlotLines]            = aqua;
	colors[ImGuiCol_PlotLinesHovered]     = blue;
	colors[ImGuiCol_PlotHistogram]        = yellow;
	colors[ImGuiCol_PlotHistogramHovered] = orange;
}


// Apply Gruvbox Light (medium contrast) theme to the current ImGui style
static inline void
ApplyGruvboxLightMediumTheme()
{
	// Gruvbox (light, medium) palette
	const ImVec4 bg0 = RGBA(0xFBF1C7); // light0
	const ImVec4 bg1 = RGBA(0xEBDBB2); // light1
	const ImVec4 bg2 = RGBA(0xD5C4A1); // light2
	const ImVec4 bg3 = RGBA(0xBDAE93); // light3
	const ImVec4 fg1 = RGBA(0x3C3836); // dark1 (text)
	const ImVec4 fg0 = RGBA(0x282828); // dark0
	// accent colors (selected subset used throughout)
	const ImVec4 yellow = RGBA(0xB57614);
	const ImVec4 blue   = RGBA(0x076678);
	const ImVec4 aqua   = RGBA(0x427B58);
	const ImVec4 orange = RGBA(0xAF3A03);

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 4.0f;
	style.FrameRounding    = 3.0f;
	style.PopupRounding    = 4.0f;
	style.GrabRounding     = 3.0f;
	style.TabRounding      = 4.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 1.0f;

	ImVec4 *colors                = style.Colors;
	colors[ImGuiCol_Text]         = fg1;
	colors[ImGuiCol_TextDisabled] = ImVec4(fg1.x, fg1.y, fg1.z, 0.55f);
	colors[ImGuiCol_WindowBg]     = bg0;
	colors[ImGuiCol_ChildBg]      = bg0;
	colors[ImGuiCol_PopupBg]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	colors[ImGuiCol_Border]       = bg2;
	colors[ImGuiCol_BorderShadow] = RGBA(0x000000, 0.0f);

	colors[ImGuiCol_FrameBg]        = bg2;
	colors[ImGuiCol_FrameBgHovered] = bg3;
	colors[ImGuiCol_FrameBgActive]  = bg1;

	colors[ImGuiCol_TitleBg]          = bg1;
	colors[ImGuiCol_TitleBgActive]    = bg2;
	colors[ImGuiCol_TitleBgCollapsed] = bg1;

	colors[ImGuiCol_MenuBarBg]            = bg1;
	colors[ImGuiCol_ScrollbarBg]          = bg0;
	colors[ImGuiCol_ScrollbarGrab]        = bg3;
	colors[ImGuiCol_ScrollbarGrabHovered] = bg2;
	colors[ImGuiCol_ScrollbarGrabActive]  = bg1;

	colors[ImGuiCol_CheckMark]        = aqua;
	colors[ImGuiCol_SliderGrab]       = aqua;
	colors[ImGuiCol_SliderGrabActive] = blue;

	colors[ImGuiCol_Button]        = bg3;
	colors[ImGuiCol_ButtonHovered] = bg2;
	colors[ImGuiCol_ButtonActive]  = bg1;

	colors[ImGuiCol_Header]        = bg3;
	colors[ImGuiCol_HeaderHovered] = bg2;
	colors[ImGuiCol_HeaderActive]  = bg2;

	colors[ImGuiCol_Separator]        = bg2;
	colors[ImGuiCol_SeparatorHovered] = bg1;
	colors[ImGuiCol_SeparatorActive]  = blue;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(fg0.x, fg0.y, fg0.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(aqua.x, aqua.y, aqua.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive]  = blue;

	colors[ImGuiCol_Tab]                = bg2;
	colors[ImGuiCol_TabHovered]         = bg1;
	colors[ImGuiCol_TabActive]          = bg3;
	colors[ImGuiCol_TabUnfocused]       = bg2;
	colors[ImGuiCol_TabUnfocusedActive] = bg3;

	colors[ImGuiCol_TableHeaderBg]     = bg2;
	colors[ImGuiCol_TableBorderStrong] = bg1;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(bg1.x, bg1.y, bg1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(aqua.x, aqua.y, aqua.z, 0.30f);
	colors[ImGuiCol_DragDropTarget]        = yellow;
	colors[ImGuiCol_NavHighlight]          = blue;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(fg0.x, fg0.y, fg0.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(bg0.x, bg0.y, bg0.z, 0.60f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(bg0.x, bg0.y, bg0.z, 0.60f);

	colors[ImGuiCol_PlotLines]            = aqua;
	colors[ImGuiCol_PlotLinesHovered]     = blue;
	colors[ImGuiCol_PlotHistogram]        = yellow;
	colors[ImGuiCol_PlotHistogramHovered] = orange;
}


// Apply a monochrome e-ink inspired theme (paper-like background, near-black text)
static inline void
ApplyEInkImGuiTheme()
{
	// E-Ink grayscale palette
	const ImVec4 paper  = RGBA(0xF2F2EE); // light paper
	const ImVec4 bg1    = RGBA(0xE6E6E2);
	const ImVec4 bg2    = RGBA(0xDADAD5);
	const ImVec4 bg3    = RGBA(0xCFCFCA);
	const ImVec4 ink    = RGBA(0x111111); // primary text (near black)
	const ImVec4 dim    = RGBA(0x666666); // disabled text
	const ImVec4 border = RGBA(0xB8B8B3);
	const ImVec4 accent = RGBA(0x222222); // controls/active

	ImGuiStyle &style = ImGui::GetStyle();
	// Flatter visuals: minimal rounding, subtle borders
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 0.0f;
	style.FrameRounding    = 0.0f;
	style.PopupRounding    = 0.0f;
	style.GrabRounding     = 0.0f;
	style.TabRounding      = 0.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 1.0f;

	ImVec4 *colors = style.Colors;

	colors[ImGuiCol_Text]         = ink;
	colors[ImGuiCol_TextDisabled] = ImVec4(dim.x, dim.y, dim.z, 1.0f);
	colors[ImGuiCol_WindowBg]     = paper;
	colors[ImGuiCol_ChildBg]      = paper;
	colors[ImGuiCol_PopupBg]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	colors[ImGuiCol_Border]       = border;
	colors[ImGuiCol_BorderShadow] = RGBA(0x000000, 0.0f);

	colors[ImGuiCol_FrameBg]        = bg2;
	colors[ImGuiCol_FrameBgHovered] = bg3;
	colors[ImGuiCol_FrameBgActive]  = bg1;

	colors[ImGuiCol_TitleBg]          = bg1;
	colors[ImGuiCol_TitleBgActive]    = bg2;
	colors[ImGuiCol_TitleBgCollapsed] = bg1;

	colors[ImGuiCol_MenuBarBg]            = bg1;
	colors[ImGuiCol_ScrollbarBg]          = paper;
	colors[ImGuiCol_ScrollbarGrab]        = bg3;
	colors[ImGuiCol_ScrollbarGrabHovered] = bg2;
	colors[ImGuiCol_ScrollbarGrabActive]  = bg1;

	colors[ImGuiCol_CheckMark]        = accent;
	colors[ImGuiCol_SliderGrab]       = accent;
	colors[ImGuiCol_SliderGrabActive] = ink;

	colors[ImGuiCol_Button]        = bg3;
	colors[ImGuiCol_ButtonHovered] = bg2;
	colors[ImGuiCol_ButtonActive]  = bg1;

	colors[ImGuiCol_Header]        = bg3;
	colors[ImGuiCol_HeaderHovered] = bg2;
	colors[ImGuiCol_HeaderActive]  = bg2;

	colors[ImGuiCol_Separator]        = border;
	colors[ImGuiCol_SeparatorHovered] = bg2;
	colors[ImGuiCol_SeparatorActive]  = accent;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(ink.x, ink.y, ink.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.50f);
	colors[ImGuiCol_ResizeGripActive]  = ink;

	colors[ImGuiCol_Tab]                = bg2;
	colors[ImGuiCol_TabHovered]         = bg1;
	colors[ImGuiCol_TabActive]          = bg3;
	colors[ImGuiCol_TabUnfocused]       = bg2;
	colors[ImGuiCol_TabUnfocusedActive] = bg3;

	colors[ImGuiCol_TableHeaderBg]     = bg2;
	colors[ImGuiCol_TableBorderStrong] = bg1;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(bg1.x, bg1.y, bg1.z, 0.20f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);

	// Selection should remain readable with black text; use a light mid-gray
	colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.74f, 0.74f, 0.72f, 0.65f); // ~#BDBDB8
	colors[ImGuiCol_DragDropTarget]        = accent;
	colors[ImGuiCol_NavHighlight]          = accent;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(ink.x, ink.y, ink.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);

	// Plots (grayscale)
	colors[ImGuiCol_PlotLines]            = accent;
	colors[ImGuiCol_PlotLinesHovered]     = ink;
	colors[ImGuiCol_PlotHistogram]        = accent;
	colors[ImGuiCol_PlotHistogramHovered] = ink;
}


// E-Ink Dark variant (for low-light; darker paper, lighter UI accents)
static inline void
ApplyEInkDarkImGuiTheme()
{
	const ImVec4 paper  = RGBA(0x202020);
	const ImVec4 bg1    = RGBA(0x2A2A2A);
	const ImVec4 bg2    = RGBA(0x333333);
	const ImVec4 bg3    = RGBA(0x3C3C3C);
	const ImVec4 ink    = RGBA(0xE8E8E8);
	const ImVec4 dim    = RGBA(0xA0A0A0);
	const ImVec4 border = RGBA(0x444444);
	const ImVec4 accent = RGBA(0xDDDDDD);

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 0.0f;
	style.FrameRounding    = 0.0f;
	style.PopupRounding    = 0.0f;
	style.GrabRounding     = 0.0f;
	style.TabRounding      = 0.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 1.0f;

	ImVec4 *colors                         = style.Colors;
	colors[ImGuiCol_Text]                  = ink;
	colors[ImGuiCol_TextDisabled]          = ImVec4(dim.x, dim.y, dim.z, 1.0f);
	colors[ImGuiCol_WindowBg]              = paper;
	colors[ImGuiCol_ChildBg]               = paper;
	colors[ImGuiCol_PopupBg]               = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	colors[ImGuiCol_Border]                = border;
	colors[ImGuiCol_BorderShadow]          = RGBA(0x000000, 0.0f);
	colors[ImGuiCol_FrameBg]               = bg2;
	colors[ImGuiCol_FrameBgHovered]        = bg3;
	colors[ImGuiCol_FrameBgActive]         = bg1;
	colors[ImGuiCol_TitleBg]               = bg1;
	colors[ImGuiCol_TitleBgActive]         = bg2;
	colors[ImGuiCol_TitleBgCollapsed]      = bg1;
	colors[ImGuiCol_MenuBarBg]             = bg1;
	colors[ImGuiCol_ScrollbarBg]           = paper;
	colors[ImGuiCol_ScrollbarGrab]         = bg3;
	colors[ImGuiCol_ScrollbarGrabHovered]  = bg2;
	colors[ImGuiCol_ScrollbarGrabActive]   = ink;
	colors[ImGuiCol_CheckMark]             = accent;
	colors[ImGuiCol_SliderGrab]            = accent;
	colors[ImGuiCol_SliderGrabActive]      = ink;
	colors[ImGuiCol_Button]                = bg3;
	colors[ImGuiCol_ButtonHovered]         = bg2;
	colors[ImGuiCol_ButtonActive]          = bg1;
	colors[ImGuiCol_Header]                = bg3;
	colors[ImGuiCol_HeaderHovered]         = bg2;
	colors[ImGuiCol_HeaderActive]          = bg2;
	colors[ImGuiCol_Separator]             = bg2;
	colors[ImGuiCol_SeparatorHovered]      = bg1;
	colors[ImGuiCol_SeparatorActive]       = ink;
	colors[ImGuiCol_ResizeGrip]            = ImVec4(ink.x, ink.y, ink.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered]     = ImVec4(accent.x, accent.y, accent.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive]      = ink;
	colors[ImGuiCol_Tab]                   = bg2;
	colors[ImGuiCol_TabHovered]            = bg1;
	colors[ImGuiCol_TabActive]             = bg3;
	colors[ImGuiCol_TabUnfocused]          = bg2;
	colors[ImGuiCol_TabUnfocusedActive]    = bg3;
	colors[ImGuiCol_TableHeaderBg]         = bg2;
	colors[ImGuiCol_TableBorderStrong]     = bg1;
	colors[ImGuiCol_TableBorderLight]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]            = ImVec4(bg1.x, bg1.y, bg1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]         = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);
	colors[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.30f);
	colors[ImGuiCol_DragDropTarget]        = accent;
	colors[ImGuiCol_NavHighlight]          = accent;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(ink.x, ink.y, ink.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_PlotLines]             = accent;
	colors[ImGuiCol_PlotLinesHovered]      = ink;
	colors[ImGuiCol_PlotHistogram]         = accent;
	colors[ImGuiCol_PlotHistogramHovered]  = ink;
}


// Theme abstraction and registry (generalized theme system)
class Theme {
public:
	virtual ~Theme() = default;

	virtual const char *Name() const = 0; // canonical name (e.g., "nord", "gruvbox-dark")
	virtual void Apply() const = 0; // apply to current ImGui style
	ThemeId Id();
};

namespace detail {
struct NordTheme final : Theme {
	const char *Name() const override
	{
		return "nord";
	}


	void Apply() const override
	{
		ApplyNordImGuiTheme();
	}


	ThemeId Id()
	{
		return ThemeId::Nord;
	}
};

struct GruvboxTheme final : Theme {
	const char *Name() const override
	{
		return "gruvbox";
	}


	void Apply() const override
	{
		if (gBackgroundMode == BackgroundMode::Light)
			ApplyGruvboxLightMediumTheme();
		else
			ApplyGruvboxDarkMediumTheme();
	}


	ThemeId Id()
	{
		// Legacy maps to dark; unified under base id GruvboxDarkMedium
		return ThemeId::GruvboxDarkMedium;
	}
};

struct EInkTheme final : Theme {
	const char *Name() const override
	{
		return "eink";
	}


	void Apply() const override
	{
		if (gBackgroundMode == BackgroundMode::Dark)
			ApplyEInkDarkImGuiTheme();
		else
			ApplyEInkImGuiTheme();
	}


	static ThemeId Id()
	{
		return ThemeId::EInk;
	}
};

struct SolarizedTheme final : Theme {
	const char *Name() const override
	{
		return "solarized";
	}


	void Apply() const override
	{
		if (gBackgroundMode == BackgroundMode::Light)
			ApplySolarizedLightTheme();
		else
			ApplySolarizedDarkTheme();
	}


	ThemeId Id()
	{
		return ThemeId::Solarized;
	}
};

struct Plan9Theme final : Theme {
	const char *Name() const override
	{
		return "plan9";
	}


	void Apply() const override
	{
		ApplyPlan9Theme();
	}


	ThemeId Id()
	{
		return ThemeId::Plan9;
	}
};
} // namespace detail

static inline const std::vector<std::unique_ptr<Theme> > &
ThemeRegistry()
{
	static std::vector<std::unique_ptr<Theme> > reg;
	if (reg.empty()) {
		// Alphabetical by canonical name: eink, gruvbox, nord, plan9, solarized
		reg.emplace_back(std::make_unique<detail::EInkTheme>());
		reg.emplace_back(std::make_unique<detail::GruvboxTheme>());
		reg.emplace_back(std::make_unique<detail::NordTheme>());
		reg.emplace_back(std::make_unique<detail::Plan9Theme>());
		reg.emplace_back(std::make_unique<detail::SolarizedTheme>());
	}
	return reg;
}


// Canonical theme name for a given ThemeId (via registry order)
static inline const char *
ThemeName(ThemeId id)
{
	const auto &reg = ThemeRegistry();
	size_t idx      = ThemeIndexFromId(id);
	if (idx < reg.size())
		return reg[idx]->Name();
	return "unknown";
}


// Helper to apply a theme by id and update current theme
static inline void
ApplyTheme(const ThemeId id)
{
	const auto &reg = ThemeRegistry();
	size_t idx      = ThemeIndexFromId(id);
	if (idx < reg.size()) {
		reg[idx]->Apply();
		gCurrentTheme      = id;
		gCurrentThemeIndex = idx;
	}
}


static inline ThemeId
CurrentTheme()
{
	return gCurrentTheme;
}


// Cycle helpers
static inline ThemeId
NextTheme()
{
	const auto &reg = ThemeRegistry();
	if (reg.empty())
		return gCurrentTheme;
	size_t nxt = (gCurrentThemeIndex + 1) % reg.size();
	ApplyTheme(ThemeIdFromIndex(nxt));
	return gCurrentTheme;
}


static inline ThemeId
PrevTheme()
{
	const auto &reg = ThemeRegistry();
	if (reg.empty())
		return gCurrentTheme;
	size_t prv = (gCurrentThemeIndex + reg.size() - 1) % reg.size();
	ApplyTheme(ThemeIdFromIndex(prv));
	return gCurrentTheme;
}


// Name-based API
static inline const Theme *
GetThemeByName(const std::string &name)
{
	const auto &reg = ThemeRegistry();
	for (const auto &t: reg) {
		if (name == t->Name())
			return t.get();
	}
	return nullptr;
}


static inline bool
ApplyThemeByName(const std::string &name)
{
	// Handle aliases and background-specific names
	std::string n = name;
	// lowercase copy
	std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) {
		return (char) std::tolower(c);
	});

	if (n == "gruvbox-dark") {
		SetBackgroundMode(BackgroundMode::Dark);
		n = "gruvbox";
	} else if (n == "gruvbox-light") {
		SetBackgroundMode(BackgroundMode::Light);
		n = "gruvbox";
	} else if (n == "solarized-dark") {
		SetBackgroundMode(BackgroundMode::Dark);
		n = "solarized";
	} else if (n == "solarized-light") {
		SetBackgroundMode(BackgroundMode::Light);
		n = "solarized";
	} else if (n == "eink-dark") {
		SetBackgroundMode(BackgroundMode::Dark);
		n = "eink";
	} else if (n == "eink-light") {
		SetBackgroundMode(BackgroundMode::Light);
		n = "eink";
	}
	// plan9 is a single theme; no light/dark aliases

	const auto &reg = ThemeRegistry();
	for (size_t i = 0; i < reg.size(); ++i) {
		if (n == reg[i]->Name()) {
			reg[i]->Apply();
			gCurrentThemeIndex = i;
			gCurrentTheme      = ThemeIdFromIndex(i);
			return true;
		}
	}
	return false;
}


static inline const char *
CurrentThemeName()
{
	const auto &reg = ThemeRegistry();
	if (gCurrentThemeIndex < reg.size())
		return reg[gCurrentThemeIndex]->Name();
	return "unknown";
}


// Helpers to map between legacy ThemeId and registry index
static inline size_t
ThemeIndexFromId(ThemeId id)
{
	switch (id) {
	case ThemeId::EInk:
		return 0;
	case ThemeId::GruvboxDarkMedium:
		return 1;
	case ThemeId::Nord:
		return 2;
	case ThemeId::Plan9:
		return 3;
	case ThemeId::Solarized:
		return 4;
	}
	return 0;
}


static inline ThemeId
ThemeIdFromIndex(size_t idx)
{
	switch (idx) {
	default:
	case 0:
		return ThemeId::EInk;
	case 1:
		return ThemeId::GruvboxDarkMedium; // unified gruvbox
	case 2:
		return ThemeId::Nord;
	case 3:
		return ThemeId::Plan9;
	case 4:
		return ThemeId::Solarized;
	}
}


// --- Syntax palette (v1): map TokenKind to ink color per current theme/background ---
static inline ImVec4
SyntaxInk(TokenKind k)
{
	// Basic palettes for dark/light backgrounds; tuned for Nord-ish defaults
	const bool dark = (GetBackgroundMode() == BackgroundMode::Dark);
	// Base text
	ImVec4 def = dark ? RGBA(0xD8DEE9) : RGBA(0x2E3440);
	switch (k) {
	case TokenKind::Keyword:
		return dark ? RGBA(0x81A1C1) : RGBA(0x5E81AC);
	case TokenKind::Type:
		return dark ? RGBA(0x8FBCBB) : RGBA(0x4C566A);
	case TokenKind::String:
		return dark ? RGBA(0xA3BE8C) : RGBA(0x6C8E5E);
	case TokenKind::Char:
		return dark ? RGBA(0xA3BE8C) : RGBA(0x6C8E5E);
	case TokenKind::Comment:
		return dark ? RGBA(0x616E88) : RGBA(0x7A869A);
	case TokenKind::Number:
		return dark ? RGBA(0xEBCB8B) : RGBA(0xB58900);
	case TokenKind::Preproc:
		return dark ? RGBA(0xD08770) : RGBA(0xAF3A03);
	case TokenKind::Constant:
		return dark ? RGBA(0xB48EAD) : RGBA(0x7B4B7F);
	case TokenKind::Function:
		return dark ? RGBA(0x88C0D0) : RGBA(0x3465A4);
	case TokenKind::Operator:
		return dark ? RGBA(0xECEFF4) : RGBA(0x2E3440);
	case TokenKind::Punctuation:
		return dark ? RGBA(0xECEFF4) : RGBA(0x2E3440);
	case TokenKind::Identifier:
		return def;
	case TokenKind::Whitespace:
		return def;
	case TokenKind::Error:
		return dark ? RGBA(0xBF616A) : RGBA(0xCC0000);
	case TokenKind::Default: default:
		return def;
	}
}
} // namespace kte