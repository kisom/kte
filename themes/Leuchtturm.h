// themes/Leuchtturm.h — Fountain pen on cream paper, brass and leather (header-only)
// Inspired by Kaweco Brass/Bronze Sport pens on Leuchtturm1917 notebook paper.
// Light: warm cream paper with blue-black fountain pen ink.
// Dark: leather case and patinated metal.
#pragma once
#include "ThemeHelpers.h"

static inline void
ApplyLeuchtturmLightTheme()
{
	// Notebook paper and fountain pen ink
	const ImVec4 paper  = RGBA(0xF2ECDF); // Leuchtturm cream paper
	const ImVec4 bg1    = RGBA(0xE8E2D5); // slightly darker cream
	const ImVec4 bg2    = RGBA(0xDDD7CA); // UI elements
	const ImVec4 bg3    = RGBA(0xD1CBBD); // hover/active
	const ImVec4 ink    = RGBA(0x040720); // blue-black fountain pen ink
	const ImVec4 dim    = RGBA(0x7A756A); // faded text (like printed headers)
	const ImVec4 border = RGBA(0xCCC6B4); // faint ruled lines

	// Metal accents from the pens
	const ImVec4 brass = RGBA(0x6B5E2A); // dark patinated brass
	const ImVec4 brown = RGBA(0x5C3D28); // leather/bronze

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
	colors[ImGuiCol_SliderGrabActive] = brass;

	colors[ImGuiCol_Button]        = bg2;
	colors[ImGuiCol_ButtonHovered] = bg3;
	colors[ImGuiCol_ButtonActive]  = bg1;

	colors[ImGuiCol_Header]        = bg2;
	colors[ImGuiCol_HeaderHovered] = bg3;
	colors[ImGuiCol_HeaderActive]  = bg3;

	colors[ImGuiCol_Separator]        = border;
	colors[ImGuiCol_SeparatorHovered] = bg3;
	colors[ImGuiCol_SeparatorActive]  = brass;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(ink.x, ink.y, ink.z, 0.10f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(brass.x, brass.y, brass.z, 0.50f);
	colors[ImGuiCol_ResizeGripActive]  = brass;

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

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(brass.x, brass.y, brass.z, 0.18f);
	colors[ImGuiCol_DragDropTarget]        = brass;
	colors[ImGuiCol_NavHighlight]          = brass;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(ink.x, ink.y, ink.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.15f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.15f);
	colors[ImGuiCol_PlotLines]             = brown;
	colors[ImGuiCol_PlotLinesHovered]      = brass;
	colors[ImGuiCol_PlotHistogram]         = brown;
	colors[ImGuiCol_PlotHistogramHovered]  = brass;
}


// Dark variant — leather pen case with warm metal and cream accents
static inline void
ApplyLeuchtturmDarkTheme()
{
	const ImVec4 bg0    = RGBA(0x1C1610); // dark leather
	const ImVec4 bg1    = RGBA(0x251E16); // slightly lighter
	const ImVec4 bg2    = RGBA(0x30281E); // UI elements
	const ImVec4 bg3    = RGBA(0x3E3428); // hover/active
	const ImVec4 ink    = RGBA(0xE5DDD0); // warm cream text
	const ImVec4 dim    = RGBA(0x978E7C); // secondary text
	const ImVec4 border = RGBA(0x4A3E30); // subtle borders

	const ImVec4 brass = RGBA(0xB8A060); // polished brass
	const ImVec4 brown = RGBA(0x8B6848); // bronze pen

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

	colors[ImGuiCol_CheckMark]        = brass;
	colors[ImGuiCol_SliderGrab]       = brass;
	colors[ImGuiCol_SliderGrabActive] = brown;

	colors[ImGuiCol_Button]        = bg2;
	colors[ImGuiCol_ButtonHovered] = bg3;
	colors[ImGuiCol_ButtonActive]  = bg1;

	colors[ImGuiCol_Header]        = bg2;
	colors[ImGuiCol_HeaderHovered] = bg3;
	colors[ImGuiCol_HeaderActive]  = bg3;

	colors[ImGuiCol_Separator]        = border;
	colors[ImGuiCol_SeparatorHovered] = bg3;
	colors[ImGuiCol_SeparatorActive]  = brass;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(ink.x, ink.y, ink.z, 0.10f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(brass.x, brass.y, brass.z, 0.50f);
	colors[ImGuiCol_ResizeGripActive]  = brass;

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

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(brass.x, brass.y, brass.z, 0.22f);
	colors[ImGuiCol_DragDropTarget]        = brass;
	colors[ImGuiCol_NavHighlight]          = brass;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(ink.x, ink.y, ink.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	colors[ImGuiCol_PlotLines]             = brass;
	colors[ImGuiCol_PlotLinesHovered]      = brown;
	colors[ImGuiCol_PlotHistogram]         = brass;
	colors[ImGuiCol_PlotHistogramHovered]  = brown;
}
