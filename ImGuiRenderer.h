/*
 * ImGuiRenderer - ImGui-based renderer for GUI mode
 */
#pragma once
#include <cstdint>
#include <string>
#include "Renderer.h"

struct ImFont;
class Buffer;

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

	// Max-line-width cache for the horizontal scrollbar. Measuring every line
	// every frame is prohibitively expensive on large files; we only update the
	// running max from visible lines and reset when buffer/version/font changes.
	const Buffer *max_width_buf_ = nullptr;
	std::uint64_t max_width_version_ = 0;
	ImFont *max_width_font_ = nullptr;
	float max_width_font_size_ = 0.0f;
	float max_width_px_ = 0.0f;
};
