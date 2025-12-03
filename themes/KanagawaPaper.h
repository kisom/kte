// themes/KanagawaPaper.h — Kanagawa Paper (light) inspired ImGui theme (header-only)
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static void
ApplyKanagawaPaperLightTheme()
{
	// Approximate Kanagawa Paper (light) palette
	const ImVec4 bg0 = RGBA(0xF2EAD3); // paper
	const ImVec4 bg1 = RGBA(0xE6DBC3);
	const ImVec4 bg2 = RGBA(0xD9CEB5);
	const ImVec4 bg3 = RGBA(0xCBBE9E);
	const ImVec4 fg0 = RGBA(0x3A3A3A); // ink
	const ImVec4 fg1 = RGBA(0x1F1F1F);

	// Accents (muted teal/orange/blue)
	const ImVec4 teal   = RGBA(0x3A7D7C);
	const ImVec4 orange = RGBA(0xC4746E);
	const ImVec4 blue   = RGBA(0x487AA1);
	const ImVec4 yellow = RGBA(0xB58900);

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
	colors[ImGuiCol_Text]         = fg0;
	colors[ImGuiCol_TextDisabled] = ImVec4(fg0.x, fg0.y, fg0.z, 0.55f);
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

	colors[ImGuiCol_CheckMark]        = teal;
	colors[ImGuiCol_SliderGrab]       = teal;
	colors[ImGuiCol_SliderGrabActive] = blue;

	colors[ImGuiCol_Button]        = bg3;
	colors[ImGuiCol_ButtonHovered] = bg2;
	colors[ImGuiCol_ButtonActive]  = bg1;

	colors[ImGuiCol_Header]        = bg3;
	colors[ImGuiCol_HeaderHovered] = bg2;
	colors[ImGuiCol_HeaderActive]  = bg2;

	colors[ImGuiCol_Separator]        = bg2;
	colors[ImGuiCol_SeparatorHovered] = bg1;
	colors[ImGuiCol_SeparatorActive]  = teal;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(fg1.x, fg1.y, fg1.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(teal.x, teal.y, teal.z, 0.67f);
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
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(fg1.x, fg1.y, fg1.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.25f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.25f);
	colors[ImGuiCol_PlotLines]             = teal;
	colors[ImGuiCol_PlotLinesHovered]      = blue;
	colors[ImGuiCol_PlotHistogram]         = yellow;
	colors[ImGuiCol_PlotHistogramHovered]  = orange;
}


static void
ApplyKanagawaPaperDarkTheme()
{
	// Approximate Kanagawa (dark) with paper-inspired warmth
	const ImVec4 bg0 = RGBA(0x1F1E1A); // deep warm black
	const ImVec4 bg1 = RGBA(0x25241F);
	const ImVec4 bg2 = RGBA(0x2E2C26);
	const ImVec4 bg3 = RGBA(0x3A372F);
	const ImVec4 fg0 = RGBA(0xDCD7BA); // warm ivory ink (kanagawa fg)
	const ImVec4 fg1 = RGBA(0xC8C093);

	// Accents similar to kanagawa-wave
	const ImVec4 teal   = RGBA(0x7AA89F);
	const ImVec4 orange = RGBA(0xFFA066);
	const ImVec4 blue   = RGBA(0x7E9CD8);
	const ImVec4 yellow = RGBA(0xE6C384);

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

	ImVec4 *c                         = style.Colors;
	c[ImGuiCol_Text]                  = fg0;
	c[ImGuiCol_TextDisabled]          = ImVec4(fg1.x, fg1.y, fg1.z, 0.6f);
	c[ImGuiCol_WindowBg]              = bg0;
	c[ImGuiCol_ChildBg]               = bg0;
	c[ImGuiCol_PopupBg]               = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	c[ImGuiCol_Border]                = bg2;
	c[ImGuiCol_BorderShadow]          = RGBA(0x000000, 0.0f);
	c[ImGuiCol_FrameBg]               = bg2;
	c[ImGuiCol_FrameBgHovered]        = bg3;
	c[ImGuiCol_FrameBgActive]         = bg1;
	c[ImGuiCol_TitleBg]               = bg1;
	c[ImGuiCol_TitleBgActive]         = bg2;
	c[ImGuiCol_TitleBgCollapsed]      = bg1;
	c[ImGuiCol_MenuBarBg]             = bg1;
	c[ImGuiCol_ScrollbarBg]           = bg0;
	c[ImGuiCol_ScrollbarGrab]         = bg3;
	c[ImGuiCol_ScrollbarGrabHovered]  = bg2;
	c[ImGuiCol_ScrollbarGrabActive]   = bg1;
	c[ImGuiCol_CheckMark]             = teal;
	c[ImGuiCol_SliderGrab]            = teal;
	c[ImGuiCol_SliderGrabActive]      = blue;
	c[ImGuiCol_Button]                = bg3;
	c[ImGuiCol_ButtonHovered]         = bg2;
	c[ImGuiCol_ButtonActive]          = bg1;
	c[ImGuiCol_Header]                = bg3;
	c[ImGuiCol_HeaderHovered]         = bg2;
	c[ImGuiCol_HeaderActive]          = bg2;
	c[ImGuiCol_Separator]             = bg2;
	c[ImGuiCol_SeparatorHovered]      = bg1;
	c[ImGuiCol_SeparatorActive]       = teal;
	c[ImGuiCol_ResizeGrip]            = ImVec4(fg1.x, fg1.y, fg1.z, 0.15f);
	c[ImGuiCol_ResizeGripHovered]     = ImVec4(teal.x, teal.y, teal.z, 0.67f);
	c[ImGuiCol_ResizeGripActive]      = blue;
	c[ImGuiCol_Tab]                   = bg2;
	c[ImGuiCol_TabHovered]            = bg1;
	c[ImGuiCol_TabActive]             = bg3;
	c[ImGuiCol_TabUnfocused]          = bg2;
	c[ImGuiCol_TabUnfocusedActive]    = bg3;
	c[ImGuiCol_TableHeaderBg]         = bg2;
	c[ImGuiCol_TableBorderStrong]     = bg1;
	c[ImGuiCol_TableBorderLight]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	c[ImGuiCol_TableRowBg]            = ImVec4(bg1.x, bg1.y, bg1.z, 0.12f);
	c[ImGuiCol_TableRowBgAlt]         = ImVec4(bg1.x, bg1.y, bg1.z, 0.22f);
	c[ImGuiCol_TextSelectedBg]        = ImVec4(orange.x, orange.y, orange.z, 0.28f);
	c[ImGuiCol_DragDropTarget]        = orange;
	c[ImGuiCol_NavHighlight]          = orange;
	c[ImGuiCol_NavWindowingHighlight] = ImVec4(fg1.x, fg1.y, fg1.z, 0.70f);
	c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	c[ImGuiCol_PlotLines]             = teal;
	c[ImGuiCol_PlotLinesHovered]      = blue;
	c[ImGuiCol_PlotHistogram]         = yellow;
	c[ImGuiCol_PlotHistogramHovered]  = orange;
}


// Back-compat name kept; select by global background
static inline void
ApplyKanagawaPaperTheme()
{
	if (GetBackgroundMode() == BackgroundMode::Dark)
		ApplyKanagawaPaperDarkTheme();
	else
		ApplyKanagawaPaperLightTheme();
}