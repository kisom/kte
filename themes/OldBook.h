// themes/OldBook.h — Old Book (sepia) inspired ImGui theme (header-only)
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static void
ApplyOldBookTheme()
{
	// Sepia old paper aesthetic
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