/*
 * GUIRenderer - ImGui-based renderer for GUI mode
 */
#ifndef KTE_GUI_RENDERER_H
#define KTE_GUI_RENDERER_H

#include "Renderer.h"

class GUIRenderer final : public Renderer {
public:
	GUIRenderer() = default;

	~GUIRenderer() override = default;

	void Draw(Editor &ed) override;
};

#endif // KTE_GUI_RENDERER_H
