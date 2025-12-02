// themes/EInk.h — Monochrome e-ink inspired ImGui themes (header-only)
#pragma once
#include "imgui.h"
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static void
ApplyEInkImGuiTheme()
{
	// E-Ink grayscale palette (light background)
	const ImVec4 paper  = RGBA(0xF2F2EE); // light paper
	const ImVec4 bg1    = RGBA(0xE6E6E2);
	const ImVec4 bg2    = RGBA(0xDADAD5);
	const ImVec4 bg3    = RGBA(0xCFCFCA);
	const ImVec4 ink    = RGBA(0x111111); // primary text (near black)
	const ImVec4 dim    = RGBA(0x666666); // disabled text
	const ImVec4 border = RGBA(0xB8B8B3);
	const ImVec4 accent = RGBA(0x222222); // controls/active

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
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

	ImVec4 *colors                         = style.Colors;
	colors[ImGuiCol_Text]                  = ink;
	colors[ImGuiCol_TextDisabled]          = ImVec4(dim.x, dim.y, dim.z, 1.0f);
	colors[ImGuiCol_WindowBg]              = paper;
	colors[ImGuiCol_ChildBg]               = paper;
	colors[ImGuiCol_PopupBg]               = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	colors[ImGuiCol_Border]                = border;
	colors[ImGuiCol_BorderShadow]          = RGBA(0x000000, 0.0f);
	colors[ImGuiCol_FrameBg]               = bg2;
	colors[ImGuiCol_FrameBgHovered]        = bg3;
	colors[ImGuiCol_FrameBgActive]         = bg1;
	colors[ImGuiCol_TitleBg]               = bg1;
	colors[ImGuiCol_TitleBgActive]         = bg2;
	colors[ImGuiCol_TitleBgCollapsed]      = bg1;
	colors[ImGuiCol_MenuBarBg]             = bg1;
	colors[ImGuiCol_ScrollbarBg]           = paper;
	colors[ImGuiCol_ScrollbarGrab]         = bg3;
	colors[ImGuiCol_ScrollbarGrabHovered]  = bg2;
	colors[ImGuiCol_ScrollbarGrabActive]   = bg1;
	colors[ImGuiCol_CheckMark]             = accent;
	colors[ImGuiCol_SliderGrab]            = accent;
	colors[ImGuiCol_SliderGrabActive]      = ink;
	colors[ImGuiCol_Button]                = bg3;
	colors[ImGuiCol_ButtonHovered]         = bg2;
	colors[ImGuiCol_ButtonActive]          = bg1;
	colors[ImGuiCol_Header]                = bg3;
	colors[ImGuiCol_HeaderHovered]         = bg2;
	colors[ImGuiCol_HeaderActive]          = bg2;
	colors[ImGuiCol_Separator]             = border;
	colors[ImGuiCol_SeparatorHovered]      = bg2;
	colors[ImGuiCol_SeparatorActive]       = accent;
	colors[ImGuiCol_ResizeGrip]            = ImVec4(ink.x, ink.y, ink.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered]     = ImVec4(accent.x, accent.y, accent.z, 0.50f);
	colors[ImGuiCol_ResizeGripActive]      = ink;
	colors[ImGuiCol_Tab]                   = bg2;
	colors[ImGuiCol_TabHovered]            = bg1;
	colors[ImGuiCol_TabActive]             = bg3;
	colors[ImGuiCol_TabUnfocused]          = bg2;
	colors[ImGuiCol_TabUnfocusedActive]    = bg3;
	colors[ImGuiCol_TableHeaderBg]         = bg2;
	colors[ImGuiCol_TableBorderStrong]     = bg1;
	colors[ImGuiCol_TableBorderLight]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]            = ImVec4(bg1.x, bg1.y, bg1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]         = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);
	colors[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.30f);
	colors[ImGuiCol_DragDropTarget]        = accent;
	colors[ImGuiCol_NavHighlight]          = accent;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(ink.x, ink.y, ink.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_PlotLines]             = accent;
	colors[ImGuiCol_PlotLinesHovered]      = ink;
	colors[ImGuiCol_PlotHistogram]         = accent;
	colors[ImGuiCol_PlotHistogramHovered]  = ink;
}


static inline void
ApplyEInkDarkImGuiTheme()
{
	// E-Ink dark variant (dark background, light ink)
	const ImVec4 paper  = RGBA(0x1A1A1A);
	const ImVec4 bg1    = RGBA(0x222222);
	const ImVec4 bg2    = RGBA(0x2B2B2B);
	const ImVec4 bg3    = RGBA(0x343434);
	const ImVec4 ink    = RGBA(0xEDEDEA);
	const ImVec4 dim    = RGBA(0xB5B5B3);
	const ImVec4 border = RGBA(0x444444);
	const ImVec4 accent = RGBA(0xDDDDDD);

	ImGuiStyle &style      = ImGui::GetStyle();
	style.WindowPadding    = ImVec2(8.0f, 8.0f);
	style.FramePadding     = ImVec2(6.0f, 4.0f);
	style.CellPadding      = ImVec2(6.0f, 4.0f);
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

	ImVec4 *colors                         = style.Colors;
	colors[ImGuiCol_Text]                  = ink;
	colors[ImGuiCol_TextDisabled]          = ImVec4(dim.x, dim.y, dim.z, 1.0f);
	colors[ImGuiCol_WindowBg]              = paper;
	colors[ImGuiCol_ChildBg]               = paper;
	colors[ImGuiCol_PopupBg]               = ImVec4(bg1.x, bg1.y, bg1.z, 0.98f);
	colors[ImGuiCol_Border]                = border;
	colors[ImGuiCol_BorderShadow]          = RGBA(0x000000, 0.0f);
	colors[ImGuiCol_FrameBg]               = bg2;
	colors[ImGuiCol_FrameBgHovered]        = bg3;
	colors[ImGuiCol_FrameBgActive]         = bg1;
	colors[ImGuiCol_TitleBg]               = bg1;
	colors[ImGuiCol_TitleBgActive]         = bg2;
	colors[ImGuiCol_TitleBgCollapsed]      = bg1;
	colors[ImGuiCol_MenuBarBg]             = bg1;
	colors[ImGuiCol_ScrollbarBg]           = paper;
	colors[ImGuiCol_ScrollbarGrab]         = bg3;
	colors[ImGuiCol_ScrollbarGrabHovered]  = bg2;
	colors[ImGuiCol_ScrollbarGrabActive]   = bg1;
	colors[ImGuiCol_CheckMark]             = accent;
	colors[ImGuiCol_SliderGrab]            = accent;
	colors[ImGuiCol_SliderGrabActive]      = ink;
	colors[ImGuiCol_Button]                = bg3;
	colors[ImGuiCol_ButtonHovered]         = bg2;
	colors[ImGuiCol_ButtonActive]          = bg1;
	colors[ImGuiCol_Header]                = bg3;
	colors[ImGuiCol_HeaderHovered]         = bg2;
	colors[ImGuiCol_HeaderActive]          = bg2;
	colors[ImGuiCol_Separator]             = border;
	colors[ImGuiCol_SeparatorHovered]      = bg2;
	colors[ImGuiCol_SeparatorActive]       = accent;
	colors[ImGuiCol_ResizeGrip]            = ImVec4(ink.x, ink.y, ink.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered]     = ImVec4(accent.x, accent.y, accent.z, 0.50f);
	colors[ImGuiCol_ResizeGripActive]      = ink;
	colors[ImGuiCol_Tab]                   = bg2;
	colors[ImGuiCol_TabHovered]            = bg1;
	colors[ImGuiCol_TabActive]             = bg3;
	colors[ImGuiCol_TabUnfocused]          = bg2;
	colors[ImGuiCol_TabUnfocusedActive]    = bg3;
	colors[ImGuiCol_TableHeaderBg]         = bg2;
	colors[ImGuiCol_TableBorderStrong]     = bg1;
	colors[ImGuiCol_TableBorderLight]      = ImVec4(bg1.x, bg1.y, bg1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]            = ImVec4(bg1.x, bg1.y, bg1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]         = ImVec4(bg1.x, bg1.y, bg1.z, 0.35f);
	colors[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.30f);
	colors[ImGuiCol_DragDropTarget]        = accent;
	colors[ImGuiCol_NavHighlight]          = accent;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(ink.x, ink.y, ink.z, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
	colors[ImGuiCol_PlotLines]             = accent;
	colors[ImGuiCol_PlotLinesHovered]      = ink;
	colors[ImGuiCol_PlotHistogram]         = accent;
	colors[ImGuiCol_PlotHistogramHovered]  = ink;
}