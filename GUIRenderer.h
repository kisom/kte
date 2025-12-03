/*
 * GUIRenderer - ImGui-based renderer for GUI mode
 */
#pragma once
#include "Renderer.h"

class GUIRenderer final : public Renderer {
public:
	GUIRenderer() = default;

	~GUIRenderer() override = default;

	void Draw(Editor &ed) override;
};