// themes/Amber.h — Vim Amber inspired ImGui theme (header-only)
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static inline void
ApplyAmberTheme()
{
	// Amber: dark background with amber/orange highlights and warm text
	const ImVec4 bg0 = RGBA(0x141414);
	const ImVec4 bg1 = RGBA(0x1E1E1E);
	const ImVec4 bg2 = RGBA(0x2A2A2A);
	const ImVec4 bg3 = RGBA(0x343434);

	const ImVec4 text  = RGBA(0xE6D5A3); // warm parchment-like text
	const ImVec4 text2 = RGBA(0xFFF2C2);

	const ImVec4 amber  = RGBA(0xFFB000);
	const ImVec4 orange = RGBA(0xFF8C32);
	const ImVec4 yellow = RGBA(0xFFD166);
	const ImVec4 teal   = RGBA(0x3AAFA9);
	const ImVec4 cyan   = RGBA(0x4CC9F0);

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
	colors[ImGuiCol_Text]         = text;
	colors[ImGuiCol_TextDisabled] = ImVec4(text.x, text.y, text.z, 0.55f);
	colors[ImGuiCol_WindowBg]     = bg0;
	colors[ImGuiCol_ChildBg]      = bg0;
	colors[ImGuiCol_PopupBg]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	colors[ImGuiCol_Border]       = bg3;
	colors[ImGuiCol_BorderShadow] = RGBA(0x000000, 0.0f);

	colors[ImGuiCol_FrameBg]        = bg2;
	colors[ImGuiCol_FrameBgHovered] = ImVec4(amber.x, amber.y, amber.z, 0.20f);
	colors[ImGuiCol_FrameBgActive]  = ImVec4(orange.x, orange.y, orange.z, 0.25f);

	colors[ImGuiCol_TitleBg]          = bg1;
	colors[ImGuiCol_TitleBgActive]    = bg2;
	colors[ImGuiCol_TitleBgCollapsed] = bg1;

	colors[ImGuiCol_MenuBarBg]            = bg1;
	colors[ImGuiCol_ScrollbarBg]          = bg0;
	colors[ImGuiCol_ScrollbarGrab]        = bg3;
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(amber.x, amber.y, amber.z, 0.50f);
	colors[ImGuiCol_ScrollbarGrabActive]  = amber;

	colors[ImGuiCol_CheckMark]        = amber;
	colors[ImGuiCol_SliderGrab]       = amber;
	colors[ImGuiCol_SliderGrabActive] = orange;

	colors[ImGuiCol_Button]        = bg3;
	colors[ImGuiCol_ButtonHovered] = ImVec4(amber.x, amber.y, amber.z, 0.25f);
	colors[ImGuiCol_ButtonActive]  = ImVec4(amber.x, amber.y, amber.z, 0.40f);

	colors[ImGuiCol_Header]        = ImVec4(amber.x, amber.y, amber.z, 0.25f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(yellow.x, yellow.y, yellow.z, 0.30f);
	colors[ImGuiCol_HeaderActive]  = ImVec4(orange.x, orange.y, orange.z, 0.30f);

	colors[ImGuiCol_Separator]        = bg3;
	colors[ImGuiCol_SeparatorHovered] = amber;
	colors[ImGuiCol_SeparatorActive]  = orange;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(text2.x, text2.y, text2.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(amber.x, amber.y, amber.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive]  = orange;

	colors[ImGuiCol_Tab]                = bg2;
	colors[ImGuiCol_TabHovered]         = ImVec4(amber.x, amber.y, amber.z, 0.25f);
	colors[ImGuiCol_TabActive]          = ImVec4(yellow.x, yellow.y, yellow.z, 0.30f);
	colors[ImGuiCol_TabUnfocused]       = bg2;
	colors[ImGuiCol_TabUnfocusedActive] = bg3;

	colors[ImGuiCol_TableHeaderBg]     = bg2;
	colors[ImGuiCol_TableBorderStrong] = bg1;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(bg1.x, bg1.y, bg1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(amber.x, amber.y, amber.z, 0.28f);
	colors[ImGuiCol_DragDropTarget]        = orange;
	colors[ImGuiCol_NavHighlight]          = cyan;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(text2.x, text2.y, text2.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);

	colors[ImGuiCol_PlotLines]            = teal;
	colors[ImGuiCol_PlotLinesHovered]     = cyan;
	colors[ImGuiCol_PlotHistogram]        = amber;
	colors[ImGuiCol_PlotHistogramHovered] = orange;
}