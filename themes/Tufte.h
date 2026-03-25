// themes/Tufte.h — Edward Tufte inspired ImGui theme (header-only)
// Warm cream paper, dark ink, minimal chrome, restrained accent colors.
#pragma once
#include "ThemeHelpers.h"

// Light variant (primary — Tufte's books are fundamentally light)
static inline void
ApplyTufteLightTheme()
{
	// Tufte palette: warm cream paper with near-black ink
	const ImVec4 paper  = RGBA(0xFFFFF8); // Tufte's signature warm white
	const ImVec4 bg1    = RGBA(0xF4F0E8); // slightly darker cream
	const ImVec4 bg2    = RGBA(0xEAE6DE); // UI elements
	const ImVec4 bg3    = RGBA(0xDDD9D1); // hover/active
	const ImVec4 ink    = RGBA(0x111111); // near-black text
	const ImVec4 dim    = RGBA(0x6B6B6B); // disabled/secondary text
	const ImVec4 border = RGBA(0xD0CCC4); // subtle borders

	// Tufte uses color sparingly: muted red for emphasis, navy for links
	const ImVec4 red  = RGBA(0xA00000); // restrained dark red
	const ImVec4 blue = RGBA(0x1F3F6F); // dark navy

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 12.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 0.0f; // sharp edges — typographic, not app-like
	style.FrameRounding    = 0.0f;
	style.PopupRounding    = 0.0f;
	style.GrabRounding     = 0.0f;
	style.TabRounding      = 0.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 0.0f; // minimal frame borders

	ImVec4 *colors                = style.Colors;
	colors[ImGuiCol_Text]         = ink;
	colors[ImGuiCol_TextDisabled] = dim;
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
	colors[ImGuiCol_ScrollbarGrabActive]  = border;

	colors[ImGuiCol_CheckMark]        = ink;
	colors[ImGuiCol_SliderGrab]       = ink;
	colors[ImGuiCol_SliderGrabActive] = blue;

	colors[ImGuiCol_Button]        = bg2;
	colors[ImGuiCol_ButtonHovered] = bg3;
	colors[ImGuiCol_ButtonActive]  = bg1;

	colors[ImGuiCol_Header]        = bg2;
	colors[ImGuiCol_HeaderHovered] = bg3;
	colors[ImGuiCol_HeaderActive]  = bg3;

	colors[ImGuiCol_Separator]        = border;
	colors[ImGuiCol_SeparatorHovered] = bg3;
	colors[ImGuiCol_SeparatorActive]  = red;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(ink.x, ink.y, ink.z, 0.10f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(red.x, red.y, red.z, 0.50f);
	colors[ImGuiCol_ResizeGripActive]  = red;

	colors[ImGuiCol_Tab]                = bg2;
	colors[ImGuiCol_TabHovered]         = bg1;
	colors[ImGuiCol_TabActive]          = bg3;
	colors[ImGuiCol_TabUnfocused]       = bg2;
	colors[ImGuiCol_TabUnfocusedActive] = bg3;

	colors[ImGuiCol_TableHeaderBg]     = bg2;
	colors[ImGuiCol_TableBorderStrong] = border;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(border.x, border.y, border.z, 0.5f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(bg1.x, bg1.y, bg1.z, 0.0f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(bg1.x, bg1.y, bg1.z, 0.30f);

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(red.x, red.y, red.z, 0.15f);
	colors[ImGuiCol_DragDropTarget]        = red;
	colors[ImGuiCol_NavHighlight]          = red;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(ink.x, ink.y, ink.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.15f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.15f);
	colors[ImGuiCol_PlotLines]             = blue;
	colors[ImGuiCol_PlotLinesHovered]      = red;
	colors[ImGuiCol_PlotHistogram]         = blue;
	colors[ImGuiCol_PlotHistogramHovered]  = red;
}


// Dark variant — warm charcoal with cream ink, same restrained accents
static inline void
ApplyTufteDarkTheme()
{
	const ImVec4 bg0    = RGBA(0x1C1B19); // warm near-black
	const ImVec4 bg1    = RGBA(0x252420); // slightly lighter
	const ImVec4 bg2    = RGBA(0x302F2A); // UI elements
	const ImVec4 bg3    = RGBA(0x3D3C36); // hover/active
	const ImVec4 ink    = RGBA(0xEAE6DE); // cream text (inverted paper)
	const ImVec4 dim    = RGBA(0x9A9690); // disabled text
	const ImVec4 border = RGBA(0x4A4840); // subtle borders

	const ImVec4 red  = RGBA(0xD06060); // warmer red for dark bg
	const ImVec4 blue = RGBA(0x7098C0); // lighter navy for dark bg

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 12.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 0.0f;
	style.FrameRounding    = 0.0f;
	style.PopupRounding    = 0.0f;
	style.GrabRounding     = 0.0f;
	style.TabRounding      = 0.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 0.0f;

	ImVec4 *colors                = style.Colors;
	colors[ImGuiCol_Text]         = ink;
	colors[ImGuiCol_TextDisabled] = dim;
	colors[ImGuiCol_WindowBg]     = bg0;
	colors[ImGuiCol_ChildBg]      = bg0;
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
	colors[ImGuiCol_ScrollbarBg]          = bg0;
	colors[ImGuiCol_ScrollbarGrab]        = bg3;
	colors[ImGuiCol_ScrollbarGrabHovered] = border;
	colors[ImGuiCol_ScrollbarGrabActive]  = dim;

	colors[ImGuiCol_CheckMark]        = ink;
	colors[ImGuiCol_SliderGrab]       = ink;
	colors[ImGuiCol_SliderGrabActive] = blue;

	colors[ImGuiCol_Button]        = bg2;
	colors[ImGuiCol_ButtonHovered] = bg3;
	colors[ImGuiCol_ButtonActive]  = bg1;

	colors[ImGuiCol_Header]        = bg2;
	colors[ImGuiCol_HeaderHovered] = bg3;
	colors[ImGuiCol_HeaderActive]  = bg3;

	colors[ImGuiCol_Separator]        = border;
	colors[ImGuiCol_SeparatorHovered] = bg3;
	colors[ImGuiCol_SeparatorActive]  = red;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(ink.x, ink.y, ink.z, 0.10f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(red.x, red.y, red.z, 0.50f);
	colors[ImGuiCol_ResizeGripActive]  = red;

	colors[ImGuiCol_Tab]                = bg2;
	colors[ImGuiCol_TabHovered]         = bg1;
	colors[ImGuiCol_TabActive]          = bg3;
	colors[ImGuiCol_TabUnfocused]       = bg2;
	colors[ImGuiCol_TabUnfocusedActive] = bg3;

	colors[ImGuiCol_TableHeaderBg]     = bg2;
	colors[ImGuiCol_TableBorderStrong] = border;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(border.x, border.y, border.z, 0.5f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(bg1.x, bg1.y, bg1.z, 0.0f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(bg1.x, bg1.y, bg1.z, 0.30f);

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(red.x, red.y, red.z, 0.20f);
	colors[ImGuiCol_DragDropTarget]        = red;
	colors[ImGuiCol_NavHighlight]          = red;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(ink.x, ink.y, ink.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	colors[ImGuiCol_PlotLines]             = blue;
	colors[ImGuiCol_PlotLinesHovered]      = red;
	colors[ImGuiCol_PlotHistogram]         = blue;
	colors[ImGuiCol_PlotHistogramHovered]  = red;
}
