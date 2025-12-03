// themes/LCARS.h — LCARS-inspired ImGui theme (header-only)
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static void
ApplyLcarsTheme()
{
	// LCARS-inspired palette: dark background with vibrant pastel accents
	const ImVec4 bg0 = RGBA(0x0E0E10); // near-black background
	const ImVec4 bg1 = RGBA(0x1A1A1F);
	const ImVec4 bg2 = RGBA(0x26262C);
	const ImVec4 bg3 = RGBA(0x33333A);

	const ImVec4 fg0 = RGBA(0xECECEC); // primary text
	const ImVec4 fg1 = RGBA(0xBFBFC4);

	// LCARS accent colors (approximations)
	const ImVec4 lcars_orange = RGBA(0xFF9966);
	const ImVec4 lcars_yellow = RGBA(0xFFCC66);
	const ImVec4 lcars_pink   = RGBA(0xFF99CC);
	const ImVec4 lcars_purple = RGBA(0xCC99FF);
	const ImVec4 lcars_blue   = RGBA(0x66CCFF);

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(10.0f, 10.0f);
	style.FramePadding     = ImVec2(8.0f, 6.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
	style.ItemSpacing      = ImVec2(8.0f, 8.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 16.0f;
	style.GrabMinSize      = 12.0f;
	style.WindowRounding   = 6.0f;
	style.FrameRounding    = 6.0f;
	style.PopupRounding    = 6.0f;
	style.GrabRounding     = 6.0f;
	style.TabRounding      = 6.0f;
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
	colors[ImGuiCol_FrameBgHovered] = ImVec4(lcars_purple.x, lcars_purple.y, lcars_purple.z, 0.35f);
	colors[ImGuiCol_FrameBgActive]  = ImVec4(lcars_orange.x, lcars_orange.y, lcars_orange.z, 0.35f);

	colors[ImGuiCol_TitleBg]          = bg1;
	colors[ImGuiCol_TitleBgActive]    = ImVec4(bg2.x, bg2.y, bg2.z, 1.0f);
	colors[ImGuiCol_TitleBgCollapsed] = bg1;

	colors[ImGuiCol_MenuBarBg]            = bg1;
	colors[ImGuiCol_ScrollbarBg]          = bg0;
	colors[ImGuiCol_ScrollbarGrab]        = bg3;
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(lcars_blue.x, lcars_blue.y, lcars_blue.z, 0.6f);
	colors[ImGuiCol_ScrollbarGrabActive]  = lcars_blue;

	colors[ImGuiCol_CheckMark]        = lcars_orange;
	colors[ImGuiCol_SliderGrab]       = lcars_orange;
	colors[ImGuiCol_SliderGrabActive] = lcars_pink;

	colors[ImGuiCol_Button]        = ImVec4(bg3.x, bg3.y, bg3.z, 1.0f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(lcars_orange.x, lcars_orange.y, lcars_orange.z, 0.35f);
	colors[ImGuiCol_ButtonActive]  = ImVec4(lcars_orange.x, lcars_orange.y, lcars_orange.z, 0.55f);

	colors[ImGuiCol_Header]        = ImVec4(lcars_purple.x, lcars_purple.y, lcars_purple.z, 0.30f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(lcars_blue.x, lcars_blue.y, lcars_blue.z, 0.35f);
	colors[ImGuiCol_HeaderActive]  = ImVec4(lcars_orange.x, lcars_orange.y, lcars_orange.z, 0.35f);

	colors[ImGuiCol_Separator]        = bg3;
	colors[ImGuiCol_SeparatorHovered] = lcars_blue;
	colors[ImGuiCol_SeparatorActive]  = lcars_pink;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(fg1.x, fg1.y, fg1.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(lcars_yellow.x, lcars_yellow.y, lcars_yellow.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive]  = lcars_orange;

	colors[ImGuiCol_Tab]                = bg2;
	colors[ImGuiCol_TabHovered]         = ImVec4(lcars_blue.x, lcars_blue.y, lcars_blue.z, 0.35f);
	colors[ImGuiCol_TabActive]          = ImVec4(lcars_purple.x, lcars_purple.y, lcars_purple.z, 0.35f);
	colors[ImGuiCol_TabUnfocused]       = bg2;
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(bg3.x, bg3.y, bg3.z, 1.0f);

	colors[ImGuiCol_TableHeaderBg]     = bg2;
	colors[ImGuiCol_TableBorderStrong] = bg1;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(bg1.x, bg1.y, bg1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(lcars_yellow.x, lcars_yellow.y, lcars_yellow.z, 0.30f);
	colors[ImGuiCol_DragDropTarget]        = lcars_orange;
	colors[ImGuiCol_NavHighlight]          = lcars_blue;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(fg0.x, fg0.y, fg0.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);

	colors[ImGuiCol_PlotLines]            = lcars_blue;
	colors[ImGuiCol_PlotLinesHovered]     = lcars_purple;
	colors[ImGuiCol_PlotHistogram]        = lcars_yellow;
	colors[ImGuiCol_PlotHistogramHovered] = lcars_orange;
}