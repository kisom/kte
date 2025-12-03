// themes/OldBook.h — Old Book (sepia) inspired ImGui theme (header-only)
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

// Light variant (existing)
static inline void
ApplyOldBookLightTheme()
{
	// Sepia old paper aesthetic (light)
	const ImVec4 bg0     = RGBA(0xF5E6C8); // paper
	const ImVec4 bg1     = RGBA(0xEAD9B8);
	const ImVec4 bg2     = RGBA(0xDECBA6);
	const ImVec4 bg3     = RGBA(0xCDBA96);
	const ImVec4 ink     = RGBA(0x3B2F2F); // dark brown ink
	const ImVec4 inkSoft = RGBA(0x5A4540);

	// Accents: muted teal/red/blue like aged inks
	const ImVec4 teal   = RGBA(0x3F7F7A);
	const ImVec4 red    = RGBA(0xA65858);
	const ImVec4 blue   = RGBA(0x4F6D8C);
	const ImVec4 yellow = RGBA(0xB58B00);

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
	colors[ImGuiCol_Text]         = ink;
	colors[ImGuiCol_TextDisabled] = ImVec4(ink.x, ink.y, ink.z, 0.55f);
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

	colors[ImGuiCol_ResizeGrip]        = ImVec4(inkSoft.x, inkSoft.y, inkSoft.z, 0.12f);
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

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(red.x, red.y, red.z, 0.25f);
	colors[ImGuiCol_DragDropTarget]        = red;
	colors[ImGuiCol_NavHighlight]          = red;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(inkSoft.x, inkSoft.y, inkSoft.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.15f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.15f);
	colors[ImGuiCol_PlotLines]             = teal;
	colors[ImGuiCol_PlotLinesHovered]      = blue;
	colors[ImGuiCol_PlotHistogram]         = yellow;
	colors[ImGuiCol_PlotHistogramHovered]  = red;
}


// Dark variant (new)
static inline void
ApplyOldBookDarkTheme()
{
	// Old Book 8 (vim-oldbook8) — exact dark palette mapping
	// Source: oldbook8.vim (background=dark)
	const ImVec4 bg0 = RGBA(0x3C4855); // Normal guibg
	const ImVec4 bg1 = RGBA(0x445160); // Fold/Pmenu bg
	const ImVec4 bg2 = RGBA(0x626C77); // StatusLine/MatchParen bg
	const ImVec4 bg3 = RGBA(0x85939B); // Delimiter (used as a lighter hover tier)

	const ImVec4 fg0 = RGBA(0xD5D4D2); // Normal guifg
	const ImVec4 fg1 = RGBA(0x626C77); // Comment/secondary text
	const ImVec4 fg2 = RGBA(0xA5A6A4); // Type/Function (used for active controls)

	// Accents from scheme
	const ImVec4 yellow  = RGBA(0xDDD668); // Search
	const ImVec4 yellow2 = RGBA(0xD5BC02); // IncSearch
	const ImVec4 cyan    = RGBA(0x87D7FF); // Visual selection
	const ImVec4 green   = RGBA(0x5BB899); // DiffAdd
	const ImVec4 red     = RGBA(0xDB6C6C); // Error/DiffDelete

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
	colors[ImGuiCol_TextDisabled] = fg1;
	colors[ImGuiCol_WindowBg]     = bg0;
	colors[ImGuiCol_ChildBg]      = bg0;
	colors[ImGuiCol_PopupBg]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	colors[ImGuiCol_Border]       = bg2;
	colors[ImGuiCol_BorderShadow] = RGBA(0x000000, 0.0f);

	colors[ImGuiCol_FrameBg]        = bg1;
	colors[ImGuiCol_FrameBgHovered] = bg2;
	colors[ImGuiCol_FrameBgActive]  = fg2;

	colors[ImGuiCol_TitleBg]          = bg1;
	colors[ImGuiCol_TitleBgActive]    = bg2;
	colors[ImGuiCol_TitleBgCollapsed] = bg1;

	colors[ImGuiCol_MenuBarBg]            = bg1;
	colors[ImGuiCol_ScrollbarBg]          = bg0;
	colors[ImGuiCol_ScrollbarGrab]        = bg2;
	colors[ImGuiCol_ScrollbarGrabHovered] = bg3;
	colors[ImGuiCol_ScrollbarGrabActive]  = fg2;

	colors[ImGuiCol_CheckMark]        = yellow;
	colors[ImGuiCol_SliderGrab]       = yellow;
	colors[ImGuiCol_SliderGrabActive] = cyan;

	colors[ImGuiCol_Button]        = bg2;
	colors[ImGuiCol_ButtonHovered] = bg3;
	colors[ImGuiCol_ButtonActive]  = fg2;

	colors[ImGuiCol_Header]        = bg2;
	colors[ImGuiCol_HeaderHovered] = bg3;
	colors[ImGuiCol_HeaderActive]  = bg3;

	colors[ImGuiCol_Separator]        = bg2;
	colors[ImGuiCol_SeparatorHovered] = yellow;
	colors[ImGuiCol_SeparatorActive]  = yellow2;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(fg1.x, fg1.y, fg1.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(cyan.x, cyan.y, cyan.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive]  = cyan;

	colors[ImGuiCol_Tab]                = bg1;
	colors[ImGuiCol_TabHovered]         = bg2;
	colors[ImGuiCol_TabActive]          = bg2;
	colors[ImGuiCol_TabUnfocused]       = bg1;
	colors[ImGuiCol_TabUnfocusedActive] = bg2;

	colors[ImGuiCol_TableHeaderBg]     = bg2;
	colors[ImGuiCol_TableBorderStrong] = bg1;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(bg1.x, bg1.y, bg1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(cyan.x, cyan.y, cyan.z, 0.28f);
	colors[ImGuiCol_DragDropTarget]        = yellow;
	colors[ImGuiCol_NavHighlight]          = yellow;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(fg1.x, fg1.y, fg1.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
	colors[ImGuiCol_PlotLines]             = green;
	colors[ImGuiCol_PlotLinesHovered]      = cyan;
	colors[ImGuiCol_PlotHistogram]         = yellow;
	colors[ImGuiCol_PlotHistogramHovered]  = red;
}


static void
ApplyOldBookTheme()
{
	// Back-compat: keep calling light by default; GUITheme dispatches variants
	ApplyOldBookLightTheme();
}