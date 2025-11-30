#include "GUIRenderer.h"

#include "Editor.h"
#include "Buffer.h"
#include "Command.h"

#include <imgui.h>
#include <cstdio>

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
		                  ImGuiWindowFlags_HorizontalScrollbar);
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
		// When the user scrolls and the cursor is off-screen, move it to the nearest visible row
		{
			static float prev_scroll_y = -1.0f;
			float child_h              = ImGui::GetWindowHeight(); // child window height
			long first_row             = static_cast<long>(scroll_y / row_h);
			long vis_rows              = static_cast<long>(child_h / row_h);
			if (vis_rows < 1)
				vis_rows = 1;
			long last_row = first_row + vis_rows - 1;
			if (prev_scroll_y >= 0.0f && scroll_y != prev_scroll_y) {
				long cyr = static_cast<long>(cy);
				if (cyr < first_row || cyr > last_row) {
					long new_row = (cyr < first_row) ? first_row : last_row;
					if (new_row < 0)
						new_row = 0;
					if (new_row >= static_cast<long>(lines.size())) {
						new_row = static_cast<long>(lines.empty() ? 0 : (lines.size() - 1));
					}
					// Clamp column to line length
					std::size_t new_col = 0;
					if (!lines.empty()) {
						const std::string &l = lines[static_cast<std::size_t>(new_row)];
						new_col              = std::min<std::size_t>(cx, l.size());
					}
					char tmp2[64];
					std::snprintf(tmp2, sizeof(tmp2), "%ld:%zu", new_row, new_col);
					Execute(ed, CommandId::MoveCursorTo, std::string(tmp2));
					// refresh local variables after move
					cy = buf->Cury();
					cx = buf->Curx();
				}
			}
			prev_scroll_y = scroll_y;
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
		// Build status string: "kge v<version> | <filename>*"
		const char *fname = (buf->IsFileBacked()) ? buf->Filename().c_str() : "(new)";
		bool dirty        = buf->Dirty();
		int row1          = static_cast<int>(buf->Cury()) + 1;
		int col1          = static_cast<int>(buf->Curx()) + 1;
		bool have_mark    = buf->MarkSet();
		int mrow1         = have_mark ? static_cast<int>(buf->MarkCury()) + 1 : 0;
		int mcol1         = have_mark ? static_cast<int>(buf->MarkCurx()) + 1 : 0;
		char left[512];
		if (have_mark) {
			std::snprintf(left, sizeof(left), " kge %s | %s%s | %d:%d | mk %d:%d ",
			              KTE_VERSION_STR, fname, dirty ? "*" : "", row1, col1, mrow1, mcol1);
		} else {
			std::snprintf(left, sizeof(left), " kge %s | %s%s | %d:%d ",
			              KTE_VERSION_STR, fname, dirty ? "*" : "", row1, col1);
		}

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
		// Place status text within the bar
		// Draw status text (left-aligned)
		ImVec2 left_sz = ImGui::CalcTextSize(left);
		ImGui::SetCursorScreenPos(ImVec2(p0.x + 6.f, p0.y + (bar_h - left_sz.y) * 0.5f));
		ImGui::TextUnformatted(left);
		// Advance cursor to after the bar to keep layout consistent
		ImGui::Dummy(ImVec2(x1 - x0, bar_h));
	}

	ImGui::End();
	ImGui::PopStyleVar(3);
}
