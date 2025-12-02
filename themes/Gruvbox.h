// themes/Gruvbox.h — Gruvbox Dark/Light (medium) ImGui themes (header-only)
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static void
ApplyGruvboxDarkMediumTheme()
{
	// Gruvbox (dark, medium) palette
	const ImVec4 bg0 = RGBA(0x282828); // dark0
	const ImVec4 bg1 = RGBA(0x3C3836); // dark1
	const ImVec4 bg2 = RGBA(0x504945); // dark2
	const ImVec4 bg3 = RGBA(0x665C54); // dark3
	const ImVec4 fg1 = RGBA(0xEBDBB2); // light1
	const ImVec4 fg0 = RGBA(0xFBF1C7); // light0
	// accent colors
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

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(orange.x, orange.y, orange.z, 0.30f);
	colors[ImGuiCol_DragDropTarget]        = orange;
	colors[ImGuiCol_NavHighlight]          = orange;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(fg0.x, fg0.y, fg0.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_PlotLines]             = aqua;
	colors[ImGuiCol_PlotLinesHovered]      = blue;
	colors[ImGuiCol_PlotHistogram]         = yellow;
	colors[ImGuiCol_PlotHistogramHovered]  = orange;
}


static inline void
ApplyGruvboxLightMediumTheme()
{
	// Gruvbox (light, medium) palette
	const ImVec4 bg0 = RGBA(0xFBF1C7); // light0
	const ImVec4 bg1 = RGBA(0xEBDBB2); // light1
	const ImVec4 bg2 = RGBA(0xD5C4A1); // light2
	const ImVec4 bg3 = RGBA(0xBDAE93); // light3
	const ImVec4 fg1 = RGBA(0x3C3836); // dark1
	const ImVec4 fg0 = RGBA(0x282828); // dark0
	// accents
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

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(orange.x, orange.y, orange.z, 0.30f);
	colors[ImGuiCol_DragDropTarget]        = orange;
	colors[ImGuiCol_NavHighlight]          = orange;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(fg0.x, fg0.y, fg0.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_PlotLines]             = aqua;
	colors[ImGuiCol_PlotLinesHovered]      = blue;
	colors[ImGuiCol_PlotHistogram]         = yellow;
	colors[ImGuiCol_PlotHistogramHovered]  = orange;
}