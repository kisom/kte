#include "GUIRenderer.h"

#include "Editor.h"
#include "Buffer.h"
#include "Command.h"

#include <imgui.h>
#include <cstdio>
#include <string>
#include <filesystem>

// Version string expected to be provided by build system as KTE_VERSION_STR
#ifndef KTE_VERSION_STR
#  define KTE_VERSION_STR "dev"
#endif


void
GUIRenderer::Draw(Editor &ed)
{
	// Make the editor window occupy the entire GUI container/viewport
	ImGuiViewport *vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->Pos);
	ImGui::SetNextWindowSize(vp->Size);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
	                         | ImGuiWindowFlags_NoResize
	                         | ImGuiWindowFlags_NoMove
	                         | ImGuiWindowFlags_NoCollapse
	                         | ImGuiWindowFlags_NoSavedSettings
	                         | ImGuiWindowFlags_NoBringToFrontOnFocus
	                         | ImGuiWindowFlags_NoNavFocus;

	// Reduce padding so the buffer content uses the whole area
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.f, 6.f));

	ImGui::Begin("kte", nullptr, flags);

	const Buffer *buf = ed.CurrentBuffer();
	if (!buf) {
		ImGui::TextUnformatted("[no buffer]");
	} else {
		const auto &lines = buf->Rows();
		// Reserve space for status bar at bottom
		ImGui::BeginChild("scroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false,
		                  ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		// Detect click-to-move inside this scroll region
		ImVec2 list_origin  = ImGui::GetCursorScreenPos();
		float scroll_y      = ImGui::GetScrollY();
		float scroll_x      = ImGui::GetScrollX();
		std::size_t rowoffs = 0; // we render from the first line; scrolling is handled by ImGui
		std::size_t cy      = buf->Cury();
		std::size_t cx      = buf->Curx();
		const float line_h  = ImGui::GetTextLineHeight();
		const float row_h   = ImGui::GetTextLineHeightWithSpacing();
		const float space_w = ImGui::CalcTextSize(" ").x;
		// Two-way sync between Buffer::Rowoffs and ImGui scroll position:
		// - If command layer changed Buffer::Rowoffs since last frame, drive ImGui scroll from it.
		// - Otherwise, propagate ImGui scroll to Buffer::Rowoffs so command layer has an up-to-date view.
		// This prevents clicks/wheel from being immediately overridden by stale offsets.
		bool forced_scroll = false;
		{
			static long prev_buf_rowoffs = -1; // previous frame's Buffer::Rowoffs
			static float prev_scroll_y   = -1.0f; // previous frame's ImGui scroll Y in pixels

			const long buf_rowoffs = static_cast<long>(buf->Rowoffs());
			const long scroll_top  = static_cast<long>(scroll_y / row_h);

			// Detect programmatic change (e.g., keyboard navigation ensured visibility)
			if (prev_buf_rowoffs >= 0 && buf_rowoffs != prev_buf_rowoffs) {
				ImGui::SetScrollY(static_cast<float>(buf_rowoffs) * row_h);
				scroll_y      = ImGui::GetScrollY();
				forced_scroll = true;
			} else {
				// If user scrolled (scroll_y changed), update buffer row offset accordingly
				if (prev_scroll_y >= 0.0f && scroll_y != prev_scroll_y) {
					if (Buffer *mbuf = const_cast<Buffer *>(buf)) {
						// Keep horizontal offset owned by GUI; only update vertical offset here
						mbuf->SetOffsets(static_cast<std::size_t>(std::max(0L, scroll_top)),
						                 mbuf->Coloffs());
					}
				}
			}

			// Update trackers for next frame
			prev_buf_rowoffs = static_cast<long>(buf->Rowoffs());
			prev_scroll_y    = ImGui::GetScrollY();
		}
		// Synchronize cursor and scrolling.
		// Ensure the cursor is visible even on the first frame or when it didn't move,
		// unless we already forced scrolling from Buffer::Rowoffs this frame.
		{
			// Compute visible row range using the child window height
			float child_h  = ImGui::GetWindowHeight();
			long first_row = static_cast<long>(scroll_y / row_h);
			long vis_rows  = static_cast<long>(child_h / row_h);
			if (vis_rows < 1)
				vis_rows = 1;
			long last_row = first_row + vis_rows - 1;

			if (!forced_scroll) {
				long cyr = static_cast<long>(cy);
				if (cyr < first_row || cyr > last_row) {
					float target = (static_cast<float>(cyr) - std::max(0L, vis_rows / 2)) * row_h;
					float max_y  = ImGui::GetScrollMaxY();
					if (target < 0.f)
						target = 0.f;
					if (max_y >= 0.f && target > max_y)
						target = max_y;
					ImGui::SetScrollY(target);
					// refresh local variables
					scroll_y  = ImGui::GetScrollY();
					first_row = static_cast<long>(scroll_y / row_h);
					last_row  = first_row + vis_rows - 1;
				}
			}
		}
		// Handle mouse click before rendering to avoid dependent on drawn items
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			ImVec2 mp = ImGui::GetIO().MousePos;
			// Map Y to row
			float rel_y = scroll_y + (mp.y - list_origin.y);
			long row    = static_cast<long>(rel_y / row_h);
			if (row < 0)
				row = 0;
			if (row >= static_cast<long>(lines.size()))
				row = static_cast<long>(lines.empty() ? 0 : (lines.size() - 1));
			// Map X to column by measuring text width
			std::size_t col = 0;
			if (!lines.empty()) {
				const std::string &line = lines[static_cast<std::size_t>(row)];
				float rel_x             = scroll_x + (mp.x - list_origin.x);
				if (rel_x <= 0.0f) {
					col = 0;
				} else {
					float prev_w = 0.0f;
					for (std::size_t i = 1; i <= line.size(); ++i) {
						ImVec2 sz = ImGui::CalcTextSize(
							line.c_str(), line.c_str() + static_cast<long>(i));
						if (sz.x >= rel_x) {
							// Pick closer between i-1 and i
							float d_prev = rel_x - prev_w;
							float d_curr = sz.x - rel_x;
							col          = (d_prev <= d_curr) ? (i - 1) : i;
							break;
						}
						prev_w = sz.x;
						if (i == line.size()) {
							// clicked beyond EOL
							float eol_w = sz.x;
							col         = (rel_x > eol_w + space_w * 0.5f)
								      ? line.size()
								      : line.size();
						}
					}
				}
			}
			// Dispatch command to move cursor
			char tmp[64];
			std::snprintf(tmp, sizeof(tmp), "%ld:%zu", row, col);
			Execute(ed, CommandId::MoveCursorTo, std::string(tmp));
		}
		for (std::size_t i = rowoffs; i < lines.size(); ++i) {
			// Capture the screen position before drawing the line
			ImVec2 line_pos         = ImGui::GetCursorScreenPos();
			const std::string &line = lines[i];
			ImGui::TextUnformatted(line.c_str());

			// Draw a visible cursor indicator on the current line
			if (i == cy) {
				// Compute X offset by measuring text width up to cursor column
				std::size_t px_count = std::min(cx, line.size());
				ImVec2 pre_sz        = ImGui::CalcTextSize(line.c_str(),
				                                    line.c_str() + static_cast<long>(px_count));
				ImVec2 p0 = ImVec2(line_pos.x + pre_sz.x, line_pos.y);
				ImVec2 p1 = ImVec2(p0.x + space_w, p0.y + line_h);
				ImU32 col = IM_COL32(200, 200, 255, 128); // soft highlight
				ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
			}
		}
		ImGui::EndChild();

		// Status bar spanning full width
		ImGui::Separator();

		// Build three segments: left (app/version/buffer/dirty), middle (message), right (cursor/mark)
		// Compute full content width and draw a filled background rectangle
		ImVec2 win_pos = ImGui::GetWindowPos();
		ImVec2 cr_min  = ImGui::GetWindowContentRegionMin();
		ImVec2 cr_max  = ImGui::GetWindowContentRegionMax();
		float x0       = win_pos.x + cr_min.x;
		float x1       = win_pos.x + cr_max.x;
		ImVec2 cursor  = ImGui::GetCursorScreenPos();
		float bar_h    = ImGui::GetFrameHeight();
		ImVec2 p0(x0, cursor.y);
		ImVec2 p1(x1, cursor.y + bar_h);
		ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_HeaderActive);
		ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, bg_col);
		// Build left text
		std::string left;
		left.reserve(256);
		left += "kge"; // GUI app name
		left += " ";
		left += KTE_VERSION_STR;
		std::string fname;
		try {
			fname = ed.DisplayNameFor(*buf);
		} catch (...) {
			fname = buf->Filename();
			try {
				fname = std::filesystem::path(fname).filename().string();
			} catch (...) {}
		}
		left += "  ";
		left += fname;
		if (buf->Dirty())
			left += " *";
		// Append total line count as "<n>L"
		{
			unsigned long lcount = static_cast<unsigned long>(buf->Rows().size());
			left += " ";
			left += std::to_string(lcount);
			left += "L";
		}

		// Build right text (cursor/mark)
		int row1       = static_cast<int>(buf->Cury()) + 1;
		int col1       = static_cast<int>(buf->Curx()) + 1;
		bool have_mark = buf->MarkSet();
		int mrow1      = have_mark ? static_cast<int>(buf->MarkCury()) + 1 : 0;
		int mcol1      = have_mark ? static_cast<int>(buf->MarkCurx()) + 1 : 0;
		char rbuf[128];
		if (have_mark)
			std::snprintf(rbuf, sizeof(rbuf), "%d,%d | M: %d,%d", row1, col1, mrow1, mcol1);
		else
			std::snprintf(rbuf, sizeof(rbuf), "%d,%d | M: not set", row1, col1);
		std::string right = rbuf;

		// Middle message: if a prompt is active, show "Label: text"; otherwise show status
		std::string msg;
		if (ed.PromptActive()) {
			msg = ed.PromptLabel();
			if (!msg.empty())
				msg += ": ";
			msg += ed.PromptText();
		} else {
			msg = ed.Status();
		}

		// Measurements
		ImVec2 left_sz  = ImGui::CalcTextSize(left.c_str());
		ImVec2 right_sz = ImGui::CalcTextSize(right.c_str());
		float pad       = 6.f;
		float left_x    = p0.x + pad;
		float right_x   = p1.x - pad - right_sz.x;
		if (right_x < left_x + left_sz.x + pad) {
			// Not enough room; clip left to fit
			float max_left = std::max(0.0f, right_x - left_x - pad);
			if (max_left < left_sz.x && max_left > 10.0f) {
				// Render a clipped left using a child region
				ImGui::SetCursorScreenPos(ImVec2(left_x, p0.y + (bar_h - left_sz.y) * 0.5f));
				ImGui::PushClipRect(ImVec2(left_x, p0.y), ImVec2(right_x - pad, p1.y), true);
				ImGui::TextUnformatted(left.c_str());
				ImGui::PopClipRect();
			}
		} else {
			// Draw left normally
			ImGui::SetCursorScreenPos(ImVec2(left_x, p0.y + (bar_h - left_sz.y) * 0.5f));
			ImGui::TextUnformatted(left.c_str());
		}

		// Draw right
		ImGui::SetCursorScreenPos(ImVec2(std::max(right_x, left_x), p0.y + (bar_h - right_sz.y) * 0.5f));
		ImGui::TextUnformatted(right.c_str());

		// Draw middle message centered in remaining space
		if (!msg.empty()) {
			float mid_left  = left_x + left_sz.x + pad;
			float mid_right = std::max(right_x - pad, mid_left);
			float mid_w     = std::max(0.0f, mid_right - mid_left);
			if (mid_w > 1.0f) {
				ImVec2 msg_sz = ImGui::CalcTextSize(msg.c_str());
				float msg_x   = mid_left + std::max(0.0f, (mid_w - msg_sz.x) * 0.5f);
				// Clip to middle region
				ImGui::PushClipRect(ImVec2(mid_left, p0.y), ImVec2(mid_right, p1.y), true);
				ImGui::SetCursorScreenPos(ImVec2(msg_x, p0.y + (bar_h - msg_sz.y) * 0.5f));
				ImGui::TextUnformatted(msg.c_str());
				ImGui::PopClipRect();
			}
		}
		// Advance cursor to after the bar to keep layout consistent
		ImGui::Dummy(ImVec2(x1 - x0, bar_h));
	}

	ImGui::End();
	ImGui::PopStyleVar(3);
}
