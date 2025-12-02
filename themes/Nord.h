// themes/Nord.h — Nord-inspired ImGui theme (header-only)
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static void
ApplyNordImGuiTheme()
{
	// Nord palette
	const ImVec4 nord0  = RGBA(0x2E3440); // darkest bg
	const ImVec4 nord1  = RGBA(0x3B4252);
	const ImVec4 nord2  = RGBA(0x434C5E);
	const ImVec4 nord3  = RGBA(0x4C566A);
	const ImVec4 nord4  = RGBA(0xD8DEE9);
	const ImVec4 nord6  = RGBA(0xECEFF4); // lightest
	const ImVec4 nord8  = RGBA(0x88C0D0); // cyan
	const ImVec4 nord9  = RGBA(0x81A1C1); // blue
	const ImVec4 nord10 = RGBA(0x5E81AC); // blue dark
	const ImVec4 nord12 = RGBA(0xD08770); // orange
	const ImVec4 nord13 = RGBA(0xEBCB8B); // yellow

	ImGuiStyle &style = ImGui::GetStyle();

	// Base style tweaks to suit Nord aesthetics
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

	ImVec4 *colors = style.Colors;

	colors[ImGuiCol_Text]         = nord4; // primary text
	colors[ImGuiCol_TextDisabled] = ImVec4(nord4.x, nord4.y, nord4.z, 0.55f);
	colors[ImGuiCol_WindowBg]     = nord0;
	colors[ImGuiCol_ChildBg]      = nord0;
	colors[ImGuiCol_PopupBg]      = ImVec4(nord1.x, nord1.y, nord1.z, 0.98f);
	colors[ImGuiCol_Border]       = nord2;
	colors[ImGuiCol_BorderShadow] = RGBA(0x000000, 0.0f);

	colors[ImGuiCol_FrameBg]        = nord2;
	colors[ImGuiCol_FrameBgHovered] = nord3;
	colors[ImGuiCol_FrameBgActive]  = nord1;

	colors[ImGuiCol_TitleBg]          = nord1;
	colors[ImGuiCol_TitleBgActive]    = nord2;
	colors[ImGuiCol_TitleBgCollapsed] = nord1;

	colors[ImGuiCol_MenuBarBg]            = nord1;
	colors[ImGuiCol_ScrollbarBg]          = nord10;
	colors[ImGuiCol_ScrollbarGrab]        = nord3;
	colors[ImGuiCol_ScrollbarGrabHovered] = nord2;
	colors[ImGuiCol_ScrollbarGrabActive]  = nord1;

	colors[ImGuiCol_CheckMark]        = nord8;
	colors[ImGuiCol_SliderGrab]       = nord8;
	colors[ImGuiCol_SliderGrabActive] = nord9;

	colors[ImGuiCol_Button]        = nord3;
	colors[ImGuiCol_ButtonHovered] = nord2;
	colors[ImGuiCol_ButtonActive]  = nord1;

	colors[ImGuiCol_Header]        = nord3;
	colors[ImGuiCol_HeaderHovered] = nord10;
	colors[ImGuiCol_HeaderActive]  = nord10;

	colors[ImGuiCol_Separator]        = nord2;
	colors[ImGuiCol_SeparatorHovered] = nord10;
	colors[ImGuiCol_SeparatorActive]  = nord9;

	colors[ImGuiCol_ResizeGrip]        = ImVec4(nord6.x, nord6.y, nord6.z, 0.12f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(nord8.x, nord8.y, nord8.z, 0.67f);
	colors[ImGuiCol_ResizeGripActive]  = nord9;

	colors[ImGuiCol_Tab]                = nord2;
	colors[ImGuiCol_TabHovered]         = nord10;
	colors[ImGuiCol_TabActive]          = nord3;
	colors[ImGuiCol_TabUnfocused]       = nord2;
	colors[ImGuiCol_TabUnfocusedActive] = nord3;

	// Docking colors omitted for compatibility

	colors[ImGuiCol_TableHeaderBg]     = nord2;
	colors[ImGuiCol_TableBorderStrong] = nord1;
	colors[ImGuiCol_TableBorderLight]  = ImVec4(nord1.x, nord1.y, nord1.z, 0.6f);
	colors[ImGuiCol_TableRowBg]        = ImVec4(nord1.x, nord1.y, nord1.z, 0.2f);
	colors[ImGuiCol_TableRowBgAlt]     = ImVec4(nord1.x, nord1.y, nord1.z, 0.35f);

	colors[ImGuiCol_TextSelectedBg]        = ImVec4(nord8.x, nord8.y, nord8.z, 0.35f);
	colors[ImGuiCol_DragDropTarget]        = nord13;
	colors[ImGuiCol_NavHighlight]          = nord9;
	colors[ImGuiCol_NavWindowingHighlight] = ImVec4(nord6.x, nord6.y, nord6.z, 0.7f);
	colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(nord0.x, nord0.y, nord0.z, 0.6f);
	colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(nord0.x, nord0.y, nord0.z, 0.6f);

	// Plots
	colors[ImGuiCol_PlotLines]            = nord8;
	colors[ImGuiCol_PlotLinesHovered]     = nord9;
	colors[ImGuiCol_PlotHistogram]        = nord13;
	colors[ImGuiCol_PlotHistogramHovered] = nord12;
}