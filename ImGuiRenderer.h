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

private:
	// Per-window scroll tracking for two-way sync between Buffer offsets and ImGui scroll.
	// These must be per-instance (not static) so each window maintains independent state.
	long prev_buf_rowoffs_ = -1;
	long prev_buf_coloffs_ = -1;
	float prev_scroll_y_   = -1.0f;
	float prev_scroll_x_   = -1.0f;
	bool mouse_selecting_  = false;
};
