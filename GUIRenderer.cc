#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>

#include <imgui.h>
#include <regex>

#include "GUIRenderer.h"
#include "Buffer.h"
#include "Command.h"
#include "Editor.h"


// Version string expected to be provided by build system as KTE_VERSION_STR
#ifndef KTE_VERSION_STR
#  define KTE_VERSION_STR "dev"
#endif

// ImGui compatibility: some bundled ImGui versions (or builds without docking)
// don't define ImGuiWindowFlags_NoDocking. Treat it as 0 in that case.
#ifndef ImGuiWindowFlags_NoDocking
#  define ImGuiWindowFlags_NoDocking 0
#endif


void
GUIRenderer::Draw(Editor &ed)
{
	// Make the editor window occupy the entire GUI container/viewport
	ImGuiViewport *vp = ImGui::GetMainViewport();
	// On HiDPI/Retina, snap to integer pixels to prevent any draw vs hit-test
	// mismatches that can appear on the very first maximized frame.
	ImVec2 main_pos = vp->Pos;
	ImVec2 main_sz  = vp->Size;
	main_pos.x      = std::floor(main_pos.x + 0.5f);
	main_pos.y      = std::floor(main_pos.y + 0.5f);
	main_sz.x       = std::floor(main_sz.x + 0.5f);
	main_sz.y       = std::floor(main_sz.y + 0.5f);
	ImGui::SetNextWindowPos(main_pos);
	ImGui::SetNextWindowSize(main_sz);

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
			static long prev_buf_coloffs = -1; // previous frame's Buffer::Coloffs
			static float prev_scroll_y   = -1.0f; // previous frame's ImGui scroll Y in pixels
			static float prev_scroll_x   = -1.0f; // previous frame's ImGui scroll X in pixels

			const long buf_rowoffs = static_cast<long>(buf->Rowoffs());
			const long buf_coloffs = static_cast<long>(buf->Coloffs());
			const long scroll_top  = static_cast<long>(scroll_y / row_h);
			const long scroll_left = static_cast<long>(scroll_x / space_w);

			// Detect programmatic change (e.g., keyboard navigation ensured visibility)
			if (prev_buf_rowoffs >= 0 && buf_rowoffs != prev_buf_rowoffs) {
				ImGui::SetScrollY(static_cast<float>(buf_rowoffs) * row_h);
				scroll_y      = ImGui::GetScrollY();
				forced_scroll = true;
			}
			if (prev_buf_coloffs >= 0 && buf_coloffs != prev_buf_coloffs) {
				ImGui::SetScrollX(static_cast<float>(buf_coloffs) * space_w);
				scroll_x      = ImGui::GetScrollX();
				forced_scroll = true;
			}
			// If user scrolled, update buffer offsets accordingly
			if (prev_scroll_y >= 0.0f && scroll_y != prev_scroll_y) {
				if (Buffer *mbuf = const_cast<Buffer *>(buf)) {
					mbuf->SetOffsets(static_cast<std::size_t>(std::max(0L, scroll_top)),
					                 mbuf->Coloffs());
				}
			}
			if (prev_scroll_x >= 0.0f && scroll_x != prev_scroll_x) {
				if (Buffer *mbuf = const_cast<Buffer *>(buf)) {
					mbuf->SetOffsets(mbuf->Rowoffs(),
					                 static_cast<std::size_t>(std::max(0L, scroll_left)));
				}
			}

			// Update trackers for next frame
			prev_buf_rowoffs = static_cast<long>(buf->Rowoffs());
			prev_buf_coloffs = static_cast<long>(buf->Coloffs());
			prev_scroll_y    = ImGui::GetScrollY();
			prev_scroll_x    = ImGui::GetScrollX();
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
      // Compute viewport-relative row so (0) is top row of the visible area
      float vy_f = (mp.y - list_origin.y - scroll_y) / row_h;
      long vy    = static_cast<long>(vy_f);
      if (vy < 0)
          vy = 0;

			// Clamp vy within visible content height to avoid huge jumps
			ImVec2 cr_min = ImGui::GetWindowContentRegionMin();
			ImVec2 cr_max = ImGui::GetWindowContentRegionMax();
			float child_h = (cr_max.y - cr_min.y);
			long vis_rows = static_cast<long>(child_h / row_h);
			if (vis_rows < 1)
				vis_rows = 1;
			if (vy >= vis_rows)
				vy = vis_rows - 1;

            // Translate viewport row to buffer row using Buffer::Rowoffs
            std::size_t by = buf->Rowoffs() + static_cast<std::size_t>(vy);
            if (by >= lines.size()) {
                if (!lines.empty())
                    by = lines.size() - 1;
                else
                    by = 0;
            }

            // Compute desired pixel X inside the viewport content (subtract horizontal scroll)
            float px = (mp.x - list_origin.x - scroll_x);
            if (px < 0.0f)
                px = 0.0f;

            // Empty buffer guard: if there are no lines yet, just move to 0:0
            if (lines.empty()) {
                Execute(ed, CommandId::MoveCursorTo, std::string("0:0"));
            } else {
                // Convert pixel X to a render-column target including horizontal col offset
                // Use our own tab expansion of width 8 to match command layer logic.
                std::string line_clicked = static_cast<std::string>(lines[by]);
                const std::size_t tabw          = 8;
                // We iterate source columns computing absolute rendered column (rx_abs) from 0,
                // then translate to viewport-space by subtracting Coloffs.
                std::size_t coloffs = buf->Coloffs();
                std::size_t rx_abs  = 0; // absolute rendered column
                std::size_t i       = 0; // source column iterator

                // Fast-forward i until rx_abs >= coloffs to align with leftmost visible column
                if (!line_clicked.empty() && coloffs > 0) {
                    while (i < line_clicked.size() && rx_abs < coloffs) {
                        if (line_clicked[i] == '\t') {
                            rx_abs += (tabw - (rx_abs % tabw));
                        } else {
                            rx_abs += 1;
                        }
                        ++i;
                    }
                }

                // Now search for closest source column to clicked px within/after viewport
                std::size_t best_col = i; // default to first visible column
                float best_dist      = std::numeric_limits<float>::infinity();
                while (true) {
                    // For i in [current..size], evaluate candidate including the implicit end position
                    std::size_t rx_view = (rx_abs >= coloffs) ? (rx_abs - coloffs) : 0;
                    float rx_px         = static_cast<float>(rx_view) * space_w;
                    float dist          = std::fabs(px - rx_px);
                    if (dist <= best_dist) {
                        best_dist = dist;
                        best_col  = i;
                    }
                    if (i == line_clicked.size())
                        break;
                    // advance to next source column
                    if (line_clicked[i] == '\t') {
                        rx_abs += (tabw - (rx_abs % tabw));
                    } else {
                        rx_abs += 1;
                    }
                    ++i;
                }

                // Dispatch absolute buffer coordinates (row:col)
                char tmp[64];
                std::snprintf(tmp, sizeof(tmp), "%zu:%zu", by, best_col);
                Execute(ed, CommandId::MoveCursorTo, std::string(tmp));
            }
        }
		// Cache current horizontal offset in rendered columns
		const std::size_t coloffs_now = buf->Coloffs();
  for (std::size_t i = rowoffs; i < lines.size(); ++i) {
            // Capture the screen position before drawing the line
            ImVec2 line_pos         = ImGui::GetCursorScreenPos();
            std::string line = static_cast<std::string>(lines[i]);

            // Expand tabs to spaces with width=8 and apply horizontal scroll offset
            const std::size_t tabw = 8;
            std::string expanded;
            expanded.reserve(line.size() + 16);
            std::size_t rx_abs_draw = 0; // rendered column for drawing
            // Compute search highlight ranges for this line in source indices
            bool search_mode = ed.SearchActive() && !ed.SearchQuery().empty();
            std::vector<std::pair<std::size_t, std::size_t>> hl_src_ranges;
            if (search_mode) {
                // If we're in RegexSearch or RegexReplaceFind mode, compute ranges using regex; otherwise plain substring
                if (ed.PromptActive() && (ed.CurrentPromptKind() == Editor::PromptKind::RegexSearch || ed.CurrentPromptKind() == Editor::PromptKind::RegexReplaceFind)) {
                    try {
                        std::regex rx(ed.SearchQuery());
                        for (auto it = std::sregex_iterator(line.begin(), line.end(), rx);
                             it != std::sregex_iterator(); ++it) {
                            const auto &m = *it;
                            std::size_t sx = static_cast<std::size_t>(m.position());
                            std::size_t ex = sx + static_cast<std::size_t>(m.length());
                            hl_src_ranges.emplace_back(sx, ex);
                        }
                    } catch (const std::regex_error &) {
                        // ignore invalid patterns here; status line already shows the error
                    }
                } else {
                    const std::string &q = ed.SearchQuery();
                    std::size_t pos = 0;
                    while (!q.empty() && (pos = line.find(q, pos)) != std::string::npos) {
                        hl_src_ranges.emplace_back(pos, pos + q.size());
                        pos += q.size();
                    }
                }
            }
            auto src_to_rx = [&](std::size_t upto_src_exclusive) -> std::size_t {
                std::size_t rx = 0;
                std::size_t s  = 0;
                while (s < upto_src_exclusive && s < line.size()) {
                    if (line[s] == '\t')
                        rx += (tabw - (rx % tabw));
                    else
                        rx += 1;
                    ++s;
                }
                return rx;
            };
            // Draw background highlights (under text)
            if (search_mode && !hl_src_ranges.empty()) {
                // Current match emphasis
                bool has_current = ed.SearchMatchLen() > 0 && ed.SearchMatchY() == i;
                std::size_t cur_x = has_current ? ed.SearchMatchX() : 0;
                std::size_t cur_end = has_current ? (ed.SearchMatchX() + ed.SearchMatchLen()) : 0;
                for (const auto &rg : hl_src_ranges) {
                    std::size_t sx = rg.first, ex = rg.second;
                    std::size_t rx_start = src_to_rx(sx);
                    std::size_t rx_end   = src_to_rx(ex);
                    // Apply horizontal scroll offset
                    if (rx_end <= coloffs_now) continue; // fully left of view
                    std::size_t vx0 = (rx_start > coloffs_now) ? (rx_start - coloffs_now) : 0;
                    std::size_t vx1 = rx_end - coloffs_now;
                    ImVec2 p0 = ImVec2(line_pos.x + static_cast<float>(vx0) * space_w, line_pos.y);
                    ImVec2 p1 = ImVec2(line_pos.x + static_cast<float>(vx1) * space_w, line_pos.y + line_h);
                    // Choose color: current match stronger
                    bool is_current = has_current && sx == cur_x && ex == cur_end;
                    ImU32 col = is_current ? IM_COL32(255, 220, 120, 140) : IM_COL32(200, 200, 0, 90);
                    ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
                }
            }
            // Emit entire line (ImGui child scrolling will handle clipping)
            for (std::size_t src = 0; src < line.size(); ++src) {
                char c = line[src];
                if (c == '\t') {
                    std::size_t adv = (tabw - (rx_abs_draw % tabw));
                    // Emit spaces for the tab
                    expanded.append(adv, ' ');
                    rx_abs_draw += adv;
                } else {
                    expanded.push_back(c);
                    rx_abs_draw += 1;
                }
            }

            ImGui::TextUnformatted(expanded.c_str());

			// Draw a visible cursor indicator on the current line
			if (i == cy) {
				// Compute rendered X (rx) from source column with tab expansion
				std::size_t rx_abs = 0;
				for (std::size_t k = 0; k < std::min(cx, line.size()); ++k) {
					if (line[k] == '\t')
						rx_abs += (tabw - (rx_abs % tabw));
					else
						rx_abs += 1;
				}
				// Convert to viewport x by subtracting horizontal col offset
				std::size_t rx_viewport = (rx_abs > coloffs_now) ? (rx_abs - coloffs_now) : 0;
				ImVec2 p0 = ImVec2(line_pos.x + static_cast<float>(rx_viewport) * space_w, line_pos.y);
				ImVec2 p1 = ImVec2(p0.x + space_w, p0.y + line_h);
				ImU32 col = IM_COL32(200, 200, 255, 128); // soft highlight
				ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
			}
		}
		ImGui::EndChild();

  // Status bar spanning full width
  ImGui::Separator();

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
  // If a prompt is active, replace the entire status bar with the prompt text
  if (ed.PromptActive()) {
      std::string label = ed.PromptLabel();
      std::string ptext = ed.PromptText();
      auto kind         = ed.CurrentPromptKind();
      if (kind == Editor::PromptKind::OpenFile || kind == Editor::PromptKind::SaveAs ||
          kind == Editor::PromptKind::Chdir) {
          const char *home_c = std::getenv("HOME");
          if (home_c && *home_c) {
              std::string home(home_c);
              if (ptext.rfind(home, 0) == 0) {
                  std::string rest = ptext.substr(home.size());
                  if (rest.empty())
                      ptext = "~";
                  else if (!rest.empty() && (rest[0] == '/' || rest[0] == '\\'))
                      ptext = std::string("~") + rest;
              }
          }
      }

      float pad = 6.f;
      float left_x = p0.x + pad;
      float right_x = p1.x - pad;
      float max_px = std::max(0.0f, right_x - left_x);

      std::string prefix;
      if (!label.empty()) prefix = label + ": ";

      // Compose showing right-end of filename portion when too long for space
      std::string final_msg;
      ImVec2 prefix_sz = ImGui::CalcTextSize(prefix.c_str());
      float avail_px = std::max(0.0f, max_px - prefix_sz.x);
      if ((kind == Editor::PromptKind::OpenFile || kind == Editor::PromptKind::SaveAs || kind == Editor::PromptKind::Chdir) && avail_px > 0.0f) {
          // Trim from left until it fits by pixel width
          std::string tail = ptext;
          ImVec2 tail_sz = ImGui::CalcTextSize(tail.c_str());
          if (tail_sz.x > avail_px) {
              // Remove leading chars until it fits
              // Use a simple loop; text lengths are small here
              size_t start = 0;
              // To avoid O(n^2) worst-case, remove chunks
              while (start < tail.size()) {
                  // Estimate how many chars to skip based on ratio
                  float ratio = tail_sz.x / avail_px;
                  size_t skip = ratio > 1.5f ? std::min(tail.size() - start, (size_t)std::max<size_t>(1, (size_t)(tail.size() / 4))) : 1;
                  start += skip;
                  std::string candidate = tail.substr(start);
                  ImVec2 cand_sz = ImGui::CalcTextSize(candidate.c_str());
                  if (cand_sz.x <= avail_px) {
                      tail = candidate;
                      tail_sz = cand_sz;
                      break;
                  }
              }
              if (ImGui::CalcTextSize(tail.c_str()).x > avail_px && !tail.empty()) {
                  // As a last resort, ensure fit by chopping exactly
                  // binary reduce
                  size_t lo = 0, hi = tail.size();
                  while (lo < hi) {
                      size_t mid = (lo + hi) / 2;
                      std::string cand = tail.substr(mid);
                      if (ImGui::CalcTextSize(cand.c_str()).x <= avail_px) hi = mid; else lo = mid + 1;
                  }
                  tail = tail.substr(lo);
              }
          }
          final_msg = prefix + tail;
      } else {
          final_msg = prefix + ptext;
      }

      ImVec2 msg_sz = ImGui::CalcTextSize(final_msg.c_str());
      ImGui::PushClipRect(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), true);
      ImGui::SetCursorScreenPos(ImVec2(left_x, p0.y + (bar_h - msg_sz.y) * 0.5f));
      ImGui::TextUnformatted(final_msg.c_str());
      ImGui::PopClipRect();
      // Advance cursor to after the bar to keep layout consistent
      ImGui::Dummy(ImVec2(x1 - x0, bar_h));
  } else {
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
		// Insert buffer position prefix "[x/N] " before filename
		{
			std::size_t total = ed.BufferCount();
			if (total > 0) {
				std::size_t idx1 = ed.CurrentBufferIndex() + 1; // 1-based for display
				left += "[";
				left += std::to_string(static_cast<unsigned long long>(idx1));
				left += "/";
				left += std::to_string(static_cast<unsigned long long>(total));
				left += "] ";
			}
		}
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
	}

	ImGui::End();
	ImGui::PopStyleVar(3);

	// --- Visual File Picker overlay (GUI only) ---
	if (ed.FilePickerVisible()) {
		// Centered popup-style window that always fits within the current viewport
		ImGuiViewport *vp2 = ImGui::GetMainViewport();

		// Desired size, min size, and margins
		const ImVec2 want(800.0f, 500.0f);
		const ImVec2 min_sz(240.0f, 160.0f);
		const float margin = 20.0f; // space from viewport edges

		// Compute the maximum allowed size (viewport minus margins) and make sure it's not negative
		ImVec2 max_sz(std::max(32.0f, vp2->Size.x - 2.0f * margin),
		              std::max(32.0f, vp2->Size.y - 2.0f * margin));

		// Clamp desired size to [min_sz, max_sz]
		ImVec2 size(std::min(want.x, max_sz.x), std::min(want.y, max_sz.y));
		size.x = std::max(size.x, std::min(min_sz.x, max_sz.x));
		size.y = std::max(size.y, std::min(min_sz.y, max_sz.y));

		// Center within the viewport using the final size
		ImVec2 pos(vp2->Pos.x + std::max(margin, (vp2->Size.x - size.x) * 0.5f),
		           vp2->Pos.y + std::max(margin, (vp2->Size.y - size.y) * 0.5f));

		// On HiDPI displays (macOS Retina), ensure integer pixel alignment to avoid
		// potential hit-test vs draw mismatches from sub-pixel positions.
		pos.x  = std::floor(pos.x + 0.5f);
		pos.y  = std::floor(pos.y + 0.5f);
		size.x = std::floor(size.x + 0.5f);
		size.y = std::floor(size.y + 0.5f);

		ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(size, ImGuiCond_Always);
		ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
		                          ImGuiWindowFlags_NoDocking;
		bool open = true;
		if (ImGui::Begin("File Picker", &open, wflags)) {
			// Current directory
			std::string curdir = ed.FilePickerDir();
			if (curdir.empty()) {
				try {
					curdir = std::filesystem::current_path().string();
				} catch (...) {
					curdir = ".";
				}
				ed.SetFilePickerDir(curdir);
			}
			ImGui::TextUnformatted(curdir.c_str());
			ImGui::SameLine();
			if (ImGui::Button("Up")) {
				try {
					std::filesystem::path p(curdir);
					if (p.has_parent_path()) {
						ed.SetFilePickerDir(p.parent_path().string());
					}
				} catch (...) {}
			}
			ImGui::SameLine();
			if (ImGui::Button("Close")) {
				ed.SetFilePickerVisible(false);
			}

			ImGui::Separator();

			// Header
			ImGui::TextUnformatted("Name");
			ImGui::Separator();

			// Scrollable list
			ImGui::BeginChild("picker-list", ImVec2(0, 0), true);

			// Build entries: directories first then files, alphabetical
			struct Entry {
				std::string name;
				std::filesystem::path path;
				bool is_dir;
			};
			std::vector<Entry> entries;
			entries.reserve(256);
			// Optional parent entry
			try {
				std::filesystem::path base(curdir);
				std::error_code ec;
				for (auto it = std::filesystem::directory_iterator(base, ec);
				     !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
					const auto &p = it->path();
					std::string nm;
					try {
						nm = p.filename().string();
					} catch (...) {
						continue;
					}
					if (nm == "." || nm == "..")
						continue;
					bool is_dir = false;
					std::error_code ec2;
					is_dir = it->is_directory(ec2);
					entries.push_back({nm, p, is_dir});
				}
			} catch (...) {
				// ignore listing errors; show empty
			}
			std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
				if (a.is_dir != b.is_dir)
					return a.is_dir && !b.is_dir;
				return a.name < b.name;
			});

			// Draw rows
			int idx = 0;
			for (const auto &e: entries) {
				ImGui::PushID(idx++); // ensure unique/stable IDs even if names repeat
				std::string label;
				label.reserve(e.name.size() + 4);
				if (e.is_dir)
					label += "[";
				label += e.name;
				if (e.is_dir)
					label += "]";

				// Render selectable row
				ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);

				// Activate based strictly on hover + mouse, to avoid any off-by-one due to click routing
				if (ImGui::IsItemHovered()) {
					if (e.is_dir && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
						// Enter directory on double-click
						ed.SetFilePickerDir(e.path.string());
					} else if (!e.is_dir && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
						// Open file on single click
						std::string err;
						if (!ed.OpenFile(e.path.string(), err)) {
							ed.SetStatus(std::string("open: ") + err);
						} else {
							ed.SetStatus(std::string("Opened: ") + e.name);
						}
						ed.SetFilePickerVisible(false);
					}
				}
				ImGui::PopID();
			}

			ImGui::EndChild();
		}
		ImGui::End();
		if (!open) {
			ed.SetFilePickerVisible(false);
		}
	}
}
