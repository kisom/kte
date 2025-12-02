// themes/Solarized.h — Solarized Dark/Light ImGui themes (header-only)
#pragma once
#include "ThemeHelpers.h"


// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static void
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


static inline void
ApplySolarizedLightTheme()
{
	// Base colors from Ethan Schoonover Solarized (light variant)
	const ImVec4 base3  = RGBA(0xfdf6e3);
	const ImVec4 base2  = RGBA(0xeee8d5);
	const ImVec4 base1  = RGBA(0x93a1a1);
	const ImVec4 base0  = RGBA(0x839496);
	const ImVec4 base00 = RGBA(0x657b83);
	const ImVec4 base01 = RGBA(0x586e75);
	const ImVec4 base02 = RGBA(0x073642);
	const ImVec4 base03 = RGBA(0x002b36);
	const ImVec4 yellow = RGBA(0xb58900);
	const ImVec4 orange = RGBA(0xcb4b16);
	const ImVec4 blue   = RGBA(0x268bd2);
	const ImVec4 cyan   = RGBA(0x2aa198);

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
	c[ImGuiCol_Text]                  = base00;
	c[ImGuiCol_TextDisabled]          = ImVec4(base01.x, base01.y, base01.z, 1.0f);
	c[ImGuiCol_WindowBg]              = base3;
	c[ImGuiCol_ChildBg]               = base3;
	c[ImGuiCol_PopupBg]               = ImVec4(base2.x, base2.y, base2.z, 0.98f);
	c[ImGuiCol_Border]                = base1;
	c[ImGuiCol_BorderShadow]          = RGBA(0x000000, 0.0f);
	c[ImGuiCol_FrameBg]               = base2;
	c[ImGuiCol_FrameBgHovered]        = base1;
	c[ImGuiCol_FrameBgActive]         = base0;
	c[ImGuiCol_TitleBg]               = base2;
	c[ImGuiCol_TitleBgActive]         = base1;
	c[ImGuiCol_TitleBgCollapsed]      = base2;
	c[ImGuiCol_MenuBarBg]             = base2;
	c[ImGuiCol_ScrollbarBg]           = base2;
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