/*
 * ImGuiRenderer - ImGui-based renderer for GUI mode
 */
#pragma once
#include "Renderer.h"

class ImGuiRenderer final : public Renderer {
public:
	ImGuiRenderer() = default;

	~ImGuiRenderer() override = default;

	void Draw(Editor &ed) override;
};