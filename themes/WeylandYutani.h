// themes/WeylandYutani.h — Weyland-Yutani inspired ImGui theme (header-only)
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static inline void
ApplyWeylandYutaniTheme()
{
	// Corporate sci-fi aesthetic: near-black with hazard yellow accents
	const ImVec4 bg0 = RGBA(0x0A0A0A);
	const ImVec4 bg1 = RGBA(0x151515);
	const ImVec4 bg2 = RGBA(0x202020);
	const ImVec4 bg3 = RGBA(0x2B2B2B);

	const ImVec4 fg0 = RGBA(0xE6E6E6);
	const ImVec4 fg1 = RGBA(0xBDBDBD);

	const ImVec4 hazard = RGBA(0xFFC300); // Weyland-Yutani yellow
	const ImVec4 orange = RGBA(0xFF8C00);
	const ImVec4 cyan   = RGBA(0x4CC9F0);

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(10.0f, 8.0f);
	style.FramePadding     = ImVec2(8.0f, 5.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(8.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 2.0f;
	style.FrameRounding    = 2.0f;
	style.PopupRounding    = 2.0f;
	style.GrabRounding     = 2.0f;
	style.TabRounding      = 2.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 1.0f;

	ImVec4 *colors                = style.Colors;
	colors[ImGuiCol_Text]         = fg0;
	colors[ImGuiCol_TextDisabled] = ImVec4(fg0.x, fg0.y, fg0.z, 0.55f);
	colors[ImGuiCol_WindowBg]     = bg0;
	colors[ImGuiCol_ChildBg]      = bg0;
	colors[ImGuiCol_PopupBg]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	colors[ImGuiCol_Border]       = bg3;
	colors[ImGuiCol_BorderShadow] = RGBA(0x000000, 0.0f);

	colors[ImGuiCol_FrameBg]        = bg2;
	colors[ImGuiCol_FrameBgHovered] = ImVec4(hazard.x, hazard.y, hazard.z, 0.20f);
	colors[ImGuiCol_FrameBgActive]  = ImVec4(orange.x, orange.y, orange.z, 0.25f);

	colors[ImGuiCol_TitleBg]          = bg1;
	colors[ImGuiCol_TitleBgActive]    = bg2;
	colors[ImGuiCol_TitleBgCollapsed] = bg1;

	colors[ImGuiCol_MenuBarBg]            = bg1;
	colors[ImGuiCol_ScrollbarBg]          = bg0;
	colors[ImGuiCol_ScrollbarGrab]        = bg3;
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(hazard.x, hazard.y, hazard.z, 0.50f);
	colors[ImGuiCol_ScrollbarGrabActive]  = hazard;

	colors[ImGuiCol_CheckMark]        = hazard;
	colors[ImGuiCol_SliderGrab]       = hazard;
	colors[ImGuiCol_SliderGrabActive] = orange;

	colors[ImGuiCol_Button]        = bg3;
	colors[ImGuiCol_ButtonHovered] = ImVec4(hazard.x, hazard.y, hazard.z, 0.25f);
	colors[ImGuiCol_ButtonActive]  = ImVec4(hazard.x, hazard.y, hazard.z, 0.40f);

	colors[ImGuiCol_Header]        = ImVec4(hazard.x, hazard.y, hazard.z, 0.25f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(cyan.x, cyan.y, cyan.z, 0.30f);
	colors[ImGuiCol_HeaderActive]  = ImVec4(orange.x, orange.y, orange.z, 0.30f);

	colors[ImGuiCol_Separator]        = bg3;
	colors[ImGuiCol_SeparatorHovered] = hazard;
	colors[ImGuiCol_SeparatorActive]  = orange;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(fg1.x, fg1.y, fg1.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(hazard.x, hazard.y, hazard.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive]  = orange;

	colors[ImGuiCol_Tab]                = bg2;
	colors[ImGuiCol_TabHovered]         = ImVec4(hazard.x, hazard.y, hazard.z, 0.25f);
	colors[ImGuiCol_TabActive]          = ImVec4(cyan.x, cyan.y, cyan.z, 0.30f);
	colors[ImGuiCol_TabUnfocused]       = bg2;
	colors[ImGuiCol_TabUnfocusedActive] = bg3;

	colors[ImGuiCol_TableHeaderBg]     = bg2;
	colors[ImGuiCol_TableBorderStrong] = bg1;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(bg1.x, bg1.y, bg1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(hazard.x, hazard.y, hazard.z, 0.28f);
	colors[ImGuiCol_DragDropTarget]        = orange;
	colors[ImGuiCol_NavHighlight]          = cyan;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(fg0.x, fg0.y, fg0.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);

	colors[ImGuiCol_PlotLines]            = cyan;
	colors[ImGuiCol_PlotLinesHovered]     = hazard;
	colors[ImGuiCol_PlotHistogram]        = hazard;
	colors[ImGuiCol_PlotHistogramHovered] = orange;
}