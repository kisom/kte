// themes/Plan9.h — Plan 9 acme-inspired ImGui theme (header-only)
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static void
ApplyPlan9Theme()
{
	// Acme-like colors
	const ImVec4 paper  = RGBA(0xFFFFE8); // pale yellow paper
	const ImVec4 pane   = RGBA(0xFFF4C1); // slightly deeper for frames
	const ImVec4 ink    = RGBA(0x000000); // black text
	constexpr auto dim  = ImVec4(0, 0, 0, 0.60f);
	const ImVec4 border = RGBA(0x000000); // 1px black
	const ImVec4 blue   = RGBA(0x0064FF); // acme-ish blue accents
	const ImVec4 blueH  = RGBA(0x4C8DFF); // hover/active

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(6.0f, 6.0f);
	style.FramePadding     = ImVec2(5.0f, 3.0f);
	style.CellPadding      = ImVec2(5.0f, 3.0f);
	style.ItemSpacing      = ImVec2(6.0f, 6.0f);
	style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
	style.ScrollbarSize    = 14.0f;
	style.GrabMinSize      = 10.0f;
	style.WindowRounding   = 0.0f;
	style.FrameRounding    = 0.0f;
	style.PopupRounding    = 0.0f;
	style.GrabRounding     = 0.0f;
	style.TabRounding      = 0.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize  = 1.0f;

	ImVec4 *c                         = style.Colors;
	c[ImGuiCol_Text]                  = ink;
	c[ImGuiCol_TextDisabled]          = dim;
	c[ImGuiCol_WindowBg]              = paper;
	c[ImGuiCol_ChildBg]               = paper;
	c[ImGuiCol_PopupBg]               = ImVec4(pane.x, pane.y, pane.z, 0.98f);
	c[ImGuiCol_Border]                = border;
	c[ImGuiCol_BorderShadow]          = RGBA(0x000000, 0.0f);
	c[ImGuiCol_FrameBg]               = pane;
	c[ImGuiCol_FrameBgHovered]        = RGBA(0xFFEBA0);
	c[ImGuiCol_FrameBgActive]         = RGBA(0xFFE387);
	c[ImGuiCol_TitleBg]               = pane;
	c[ImGuiCol_TitleBgActive]         = RGBA(0xFFE8A6);
	c[ImGuiCol_TitleBgCollapsed]      = pane;
	c[ImGuiCol_MenuBarBg]             = pane;
	c[ImGuiCol_ScrollbarBg]           = paper;
	c[ImGuiCol_ScrollbarGrab]         = RGBA(0xEADFA5);
	c[ImGuiCol_ScrollbarGrabHovered]  = RGBA(0xE2D37F);
	c[ImGuiCol_ScrollbarGrabActive]   = RGBA(0xD8C757);
	c[ImGuiCol_CheckMark]             = blue;
	c[ImGuiCol_SliderGrab]            = blue;
	c[ImGuiCol_SliderGrabActive]      = blueH;
	c[ImGuiCol_Button]                = RGBA(0xFFF1B0);
	c[ImGuiCol_ButtonHovered]         = RGBA(0xFFE892);
	c[ImGuiCol_ButtonActive]          = RGBA(0xFFE072);
	c[ImGuiCol_Header]                = RGBA(0xFFF1B0);
	c[ImGuiCol_HeaderHovered]         = RGBA(0xFFE892);
	c[ImGuiCol_HeaderActive]          = RGBA(0xFFE072);
	c[ImGuiCol_Separator]             = border;
	c[ImGuiCol_SeparatorHovered]      = blue;
	c[ImGuiCol_SeparatorActive]       = blueH;
	c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0.12f);
	c[ImGuiCol_ResizeGripHovered]     = ImVec4(blue.x, blue.y, blue.z, 0.67f);
	c[ImGuiCol_ResizeGripActive]      = blueH;
	c[ImGuiCol_Tab]                   = RGBA(0xFFE8A6);
	c[ImGuiCol_TabHovered]            = RGBA(0xFFE072);
	c[ImGuiCol_TabActive]             = RGBA(0xFFD859);
	c[ImGuiCol_TabUnfocused]          = RGBA(0xFFE8A6);
	c[ImGuiCol_TabUnfocusedActive]    = RGBA(0xFFD859);
	c[ImGuiCol_TableHeaderBg]         = RGBA(0xFFE8A6);
	c[ImGuiCol_TableBorderStrong]     = border;
	c[ImGuiCol_TableBorderLight]      = ImVec4(0, 0, 0, 0.35f);
	c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0.04f);
	c[ImGuiCol_TableRowBgAlt]         = ImVec4(0, 0, 0, 0.08f);
	c[ImGuiCol_TextSelectedBg]        = ImVec4(blueH.x, blueH.y, blueH.z, 0.35f);
	c[ImGuiCol_DragDropTarget]        = blue;
	c[ImGuiCol_NavHighlight]          = blue;
	c[ImGuiCol_NavWindowingHighlight] = ImVec4(0, 0, 0, 0.20f);
	c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, 0.20f);
	c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.20f);
	c[ImGuiCol_PlotLines]             = blue;
	c[ImGuiCol_PlotLinesHovered]      = blueH;
	c[ImGuiCol_PlotHistogram]         = blue;
	c[ImGuiCol_PlotHistogramHovered]  = blueH;
}