// themes/Orbital.h — Orbital (dark) inspired ImGui theme (header-only)
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static inline void
ApplyOrbitalTheme()
{
	// Orbital: deep-space dark with cool blues and magentas
	const ImVec4 bg0 = RGBA(0x0C0F12); // background
	const ImVec4 bg1 = RGBA(0x12161B);
	const ImVec4 bg2 = RGBA(0x192029);
	const ImVec4 bg3 = RGBA(0x212A36);

	const ImVec4 fg0 = RGBA(0xC8D3E5); // primary text
	const ImVec4 fg1 = RGBA(0x9FB2CC); // secondary text

	const ImVec4 blue    = RGBA(0x6AA2FF); // primary accent
	const ImVec4 cyan    = RGBA(0x5AD1E6);
	const ImVec4 magenta = RGBA(0xD681F8);
	const ImVec4 yellow  = RGBA(0xE6C17D);

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

	ImVec4 *c                = style.Colors;
	c[ImGuiCol_Text]         = fg0;
	c[ImGuiCol_TextDisabled] = ImVec4(fg1.x, fg1.y, fg1.z, 0.7f);
	c[ImGuiCol_WindowBg]     = bg0;
	c[ImGuiCol_ChildBg]      = bg0;
	c[ImGuiCol_PopupBg]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	c[ImGuiCol_Border]       = bg2;
	c[ImGuiCol_BorderShadow] = RGBA(0x000000, 0.0f);

	c[ImGuiCol_FrameBg]        = bg2;
	c[ImGuiCol_FrameBgHovered] = bg3;
	c[ImGuiCol_FrameBgActive]  = bg1;

	c[ImGuiCol_TitleBg]          = bg1;
	c[ImGuiCol_TitleBgActive]    = bg2;
	c[ImGuiCol_TitleBgCollapsed] = bg1;

	c[ImGuiCol_MenuBarBg]            = bg1;
	c[ImGuiCol_ScrollbarBg]          = bg0;
	c[ImGuiCol_ScrollbarGrab]        = bg3;
	c[ImGuiCol_ScrollbarGrabHovered] = bg2;
	c[ImGuiCol_ScrollbarGrabActive]  = bg1;

	c[ImGuiCol_CheckMark]        = cyan;
	c[ImGuiCol_SliderGrab]       = cyan;
	c[ImGuiCol_SliderGrabActive] = blue;

	c[ImGuiCol_Button]        = ImVec4(blue.x, blue.y, blue.z, 0.18f);
	c[ImGuiCol_ButtonHovered] = ImVec4(blue.x, blue.y, blue.z, 0.28f);
	c[ImGuiCol_ButtonActive]  = ImVec4(blue.x, blue.y, blue.z, 0.40f);

	c[ImGuiCol_Header]        = ImVec4(magenta.x, magenta.y, magenta.z, 0.18f);
	c[ImGuiCol_HeaderHovered] = ImVec4(magenta.x, magenta.y, magenta.z, 0.28f);
	c[ImGuiCol_HeaderActive]  = ImVec4(magenta.x, magenta.y, magenta.z, 0.40f);

	c[ImGuiCol_Separator]        = bg2;
	c[ImGuiCol_SeparatorHovered] = cyan;
	c[ImGuiCol_SeparatorActive]  = blue;

	c[ImGuiCol_ResizeGrip]        = ImVec4(fg1.x, fg1.y, fg1.z, 0.10f);
	c[ImGuiCol_ResizeGripHovered] = ImVec4(cyan.x, cyan.y, cyan.z, 0.67f);
	c[ImGuiCol_ResizeGripActive]  = blue;

	c[ImGuiCol_Tab]                = bg2;
	c[ImGuiCol_TabHovered]         = bg1;
	c[ImGuiCol_TabActive]          = bg3;
	c[ImGuiCol_TabUnfocused]       = bg2;
	c[ImGuiCol_TabUnfocusedActive] = bg3;

	c[ImGuiCol_TableHeaderBg]     = bg2;
	c[ImGuiCol_TableBorderStrong] = bg1;
	c[ImGuiCol_TableBorderLight]  = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	c[ImGuiCol_TableRowBg]        = ImVec4(bg1.x, bg1.y, bg1.z, 0.12f);
	c[ImGuiCol_TableRowBgAlt]     = ImVec4(bg1.x, bg1.y, bg1.z, 0.22f);

	c[ImGuiCol_TextSelectedBg]        = ImVec4(blue.x, blue.y, blue.z, 0.28f);
	c[ImGuiCol_DragDropTarget]        = yellow;
	c[ImGuiCol_NavHighlight]          = blue;
	c[ImGuiCol_NavWindowingHighlight] = ImVec4(fg1.x, fg1.y, fg1.z, 0.70f);
	c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	c[ImGuiCol_PlotLines]             = cyan;
	c[ImGuiCol_PlotLinesHovered]      = blue;
	c[ImGuiCol_PlotHistogram]         = yellow;
	c[ImGuiCol_PlotHistogramHovered]  = magenta;
}