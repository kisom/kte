#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>

#include <imgui.h>
#include <regex>

#include "ImGuiRenderer.h"
#include "Highlight.h"
#include "GUITheme.h"
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
ImGuiRenderer::Draw(Editor &ed)
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
	                         | ImGuiWindowFlags_NoScrollbar
	                         | ImGuiWindowFlags_NoScrollWithMouse
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
		const auto &lines   = buf->Rows();
		std::size_t cy      = buf->Cury();
		std::size_t cx      = buf->Curx();
		const float line_h  = ImGui::GetTextLineHeight();
		const float row_h   = ImGui::GetTextLineHeightWithSpacing();
		const float space_w = ImGui::CalcTextSize(" ").x;

		// Two-way sync between Buffer::Rowoffs and ImGui scroll position:
		// - If command layer changed Buffer::Rowoffs since last frame, drive ImGui scroll from it.
		// - Otherwise, propagate ImGui scroll to Buffer::Rowoffs so command layer has an up-to-date view.
		const long buf_rowoffs = static_cast<long>(buf->Rowoffs());
		const long buf_coloffs = static_cast<long>(buf->Coloffs());

		// Detect programmatic change (e.g., page_down command changed rowoffs)
		// Use SetNextWindowScroll BEFORE BeginChild to set initial scroll position
		if (prev_buf_rowoffs_ >= 0 && buf_rowoffs != prev_buf_rowoffs_) {
			float target_y = static_cast<float>(buf_rowoffs) * row_h;
			ImGui::SetNextWindowScroll(ImVec2(-1.0f, target_y));
		}
		if (prev_buf_coloffs_ >= 0 && buf_coloffs != prev_buf_coloffs_) {
			float target_x = static_cast<float>(buf_coloffs) * space_w;
			float target_y = static_cast<float>(buf_rowoffs) * row_h;
			ImGui::SetNextWindowScroll(ImVec2(target_x, target_y));
		}

		// Reserve space for status bar at bottom.
		// We calculate a height that is an exact multiple of the line height
		// to avoid partial lines and "scroll past end" jitter.
		float total_avail_h = ImGui::GetContentRegionAvail().y;
		float wanted_bar_h  = ImGui::GetFrameHeight();
		float child_h_plan  = std::max(0.0f, std::floor((total_avail_h - wanted_bar_h) / row_h) * row_h);
		float real_bar_h    = total_avail_h - child_h_plan;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGui::BeginChild("scroll", ImVec2(0, child_h_plan), false,
		                  ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		// Get child window position and scroll for click handling
		ImVec2 child_window_pos = ImGui::GetWindowPos();
		float scroll_y          = ImGui::GetScrollY();
		float scroll_x          = ImGui::GetScrollX();
		std::size_t rowoffs     = 0; // we render from the first line; scrolling is handled by ImGui

		// Synchronize buffer offsets from ImGui scroll if user scrolled manually
		bool forced_scroll = false;
		{
			const long scroll_top  = static_cast<long>(scroll_y / row_h);
			const long scroll_left = static_cast<long>(scroll_x / space_w);

			// Check if rowoffs was programmatically changed this frame
			if (prev_buf_rowoffs_ >= 0 && buf_rowoffs != prev_buf_rowoffs_) {
				forced_scroll = true;
			}

			// If user scrolled (not programmatic), update buffer offsets accordingly
			if (prev_scroll_y_ >= 0.0f && scroll_y != prev_scroll_y_ && !forced_scroll) {
				if (Buffer *mbuf = const_cast<Buffer *>(buf)) {
					mbuf->SetOffsets(static_cast<std::size_t>(std::max(0L, scroll_top)),
					                 mbuf->Coloffs());
				}
			}
			if (prev_scroll_x_ >= 0.0f && scroll_x != prev_scroll_x_ && !forced_scroll) {
				if (Buffer *mbuf = const_cast<Buffer *>(buf)) {
					mbuf->SetOffsets(mbuf->Rowoffs(),
					                 static_cast<std::size_t>(std::max(0L, scroll_left)));
				}
			}

			// Update trackers for next frame
			prev_scroll_y_ = scroll_y;
			prev_scroll_x_ = scroll_x;
		}
		prev_buf_rowoffs_ = buf_rowoffs;
		prev_buf_coloffs_ = buf_coloffs;
		// Cache current horizontal offset in rendered columns for click handling
		const std::size_t coloffs_now = buf->Coloffs();

		// Mark selection state (mark -> cursor), in source coordinates
		bool sel_active    = false;
		std::size_t sel_sy = 0, sel_sx = 0, sel_ey = 0, sel_ex = 0;
		if (buf->MarkSet()) {
			sel_sy = buf->MarkCury();
			sel_sx = buf->MarkCurx();
			sel_ey = buf->Cury();
			sel_ex = buf->Curx();
			if (sel_sy > sel_ey || (sel_sy == sel_ey && sel_sx > sel_ex)) {
				std::swap(sel_sy, sel_ey);
				std::swap(sel_sx, sel_ex);
			}
			sel_active = !(sel_sy == sel_ey && sel_sx == sel_ex);
		}
		// Visual-line selection: full-line highlight range
		const bool vsel_active    = buf->VisualLineActive();
		const std::size_t vsel_sy = vsel_active ? buf->VisualLineStartY() : 0;
		const std::size_t vsel_ey = vsel_active ? buf->VisualLineEndY() : 0;

		// (mouse_selecting__ is a member variable)
		auto mouse_pos_to_buf       = [&]() -> std::pair<std::size_t, std::size_t> {
			ImVec2 mp = ImGui::GetIO().MousePos;
			// Convert mouse pos to buffer row
			float content_y = (mp.y - child_window_pos.y) + scroll_y;
			long by_l       = static_cast<long>(content_y / row_h);
			if (by_l < 0)
				by_l = 0;
			std::size_t by = static_cast<std::size_t>(by_l);
			if (by >= lines.size())
				by = lines.empty() ? 0 : (lines.size() - 1);

			if (lines.empty())
				return {0, 0};

			// Expand tabs for the clicked line
			std::string line_clicked = static_cast<std::string>(lines[by]);
			const std::size_t tabw   = 8;
			std::string click_expanded;
			click_expanded.reserve(line_clicked.size() + 16);
			std::size_t click_rx = 0;
			// Map: source column -> expanded column
			std::vector<std::size_t> src_to_exp;
			src_to_exp.reserve(line_clicked.size() + 1);
			for (std::size_t ci = 0; ci < line_clicked.size(); ++ci) {
				src_to_exp.push_back(click_rx);
				if (line_clicked[ci] == '\t') {
					std::size_t adv = (tabw - (click_rx % tabw));
					click_expanded.append(adv, ' ');
					click_rx += adv;
				} else {
					click_expanded.push_back(line_clicked[ci]);
					click_rx += 1;
				}
			}
			src_to_exp.push_back(click_rx); // past-end position

			// Pixel x relative to the line start (accounting for scroll)
			float visual_x = mp.x - child_window_pos.x;
			if (visual_x < 0.0f)
				visual_x = 0.0f;
			// Add scroll offset in pixels
			visual_x += scroll_x;

			// Find the source column whose expanded position is closest
			// to the click pixel, using actual text measurement.
			std::size_t best_col  = 0;
			float best_dist       = std::numeric_limits<float>::infinity();
			for (std::size_t ci = 0; ci <= line_clicked.size(); ++ci) {
				std::size_t exp_col = src_to_exp[ci];
				float px = 0.0f;
				if (exp_col > 0 && !click_expanded.empty()) {
					std::size_t end = std::min(click_expanded.size(), exp_col);
					px = ImGui::CalcTextSize(click_expanded.c_str(),
					                         click_expanded.c_str() + end).x;
				}
				float dist = std::fabs(visual_x - px);
				if (dist < best_dist) {
					best_dist = dist;
					best_col  = ci;
				}
			}
			return {by, best_col};
		};

		// Mouse-driven selection: set mark on double-click or drag, update cursor on any press/drag
		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			mouse_selecting_ = true;
			auto [by, bx]   = mouse_pos_to_buf();
			char tmp[64];
			std::snprintf(tmp, sizeof(tmp), "%zu:%zu", by, bx);
			Execute(ed, CommandId::MoveCursorTo, std::string(tmp));

			// Only set mark on double click.
			// Dragging will also set the mark if not already set (handled below).
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				if (Buffer *mbuf = const_cast<Buffer *>(buf)) {
					mbuf->SetMark(bx, by);
				}
			}
		}
		if (mouse_selecting_ && ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			auto [by, bx] = mouse_pos_to_buf();
			// If we are dragging (mouse moved while down), ensure mark is set to start selection
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
				if (Buffer *mbuf = const_cast<Buffer *>(buf)) {
					if (!mbuf->MarkSet()) {
						// We'd need to convert click_pos to buf coords, but it's complex here.
						// Setting it to where the cursor was *before* we started moving it
						// in this frame is a good approximation, or just using current.
						mbuf->SetMark(mbuf->Curx(), mbuf->Cury());
					}
				}
			}
			char tmp[64];
			std::snprintf(tmp, sizeof(tmp), "%zu:%zu", by, bx);
			Execute(ed, CommandId::MoveCursorTo, std::string(tmp));
		}
		if (mouse_selecting_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			mouse_selecting_ = false;
		}
		for (std::size_t i = rowoffs; i < lines.size(); ++i) {
			// Capture the screen position before drawing the line
			ImVec2 line_pos  = ImGui::GetCursorScreenPos();
			std::string line = static_cast<std::string>(lines[i]);

			// Expand tabs to spaces with width=8
			const std::size_t tabw = 8;
			std::string expanded;
			expanded.reserve(line.size() + 16);
			std::size_t rx_abs_draw = 0;
			for (std::size_t src = 0; src < line.size(); ++src) {
				char c = line[src];
				if (c == '\t') {
					std::size_t adv = (tabw - (rx_abs_draw % tabw));
					expanded.append(adv, ' ');
					rx_abs_draw += adv;
				} else {
					expanded.push_back(c);
					rx_abs_draw += 1;
				}
			}

			// Helper: convert a rendered column position to pixel x offset
			// relative to the visible line start, using actual text measurement
			// so proportional fonts render correctly.
			auto rx_to_px = [&](std::size_t rx_col) -> float {
				if (rx_col <= coloffs_now)
					return 0.0f;
				std::size_t start = coloffs_now;
				std::size_t end   = std::min(expanded.size(), rx_col);
				if (start >= expanded.size() || end <= start)
					return 0.0f;
				return ImGui::CalcTextSize(expanded.c_str() + start,
				                           expanded.c_str() + end).x;
			};

			// Compute search highlight ranges for this line in source indices
			bool search_mode = ed.SearchActive() && !ed.SearchQuery().empty();
			std::vector<std::pair<std::size_t, std::size_t> > hl_src_ranges;
			if (search_mode) {
				// If we're in RegexSearch or RegexReplaceFind mode, compute ranges using regex; otherwise plain substring
				if (ed.PromptActive() && (
					    ed.CurrentPromptKind() == Editor::PromptKind::RegexSearch || ed.
					    CurrentPromptKind() == Editor::PromptKind::RegexReplaceFind)) {
					try {
						std::regex rx(ed.SearchQuery());
						for (auto it = std::sregex_iterator(line.begin(), line.end(), rx);
						     it != std::sregex_iterator(); ++it) {
							const auto &m  = *it;
							std::size_t sx = static_cast<std::size_t>(m.position());
							std::size_t ex = sx + static_cast<std::size_t>(m.length());
							hl_src_ranges.emplace_back(sx, ex);
						}
					} catch (const std::regex_error &) {
						// ignore invalid patterns here; status line already shows the error
					}
				} else {
					const std::string &q = ed.SearchQuery();
					std::size_t pos      = 0;
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
				bool has_current    = ed.SearchMatchLen() > 0 && ed.SearchMatchY() == i;
				std::size_t cur_x   = has_current ? ed.SearchMatchX() : 0;
				std::size_t cur_end = has_current ? (ed.SearchMatchX() + ed.SearchMatchLen()) : 0;
				for (const auto &rg: hl_src_ranges) {
					std::size_t sx       = rg.first, ex = rg.second;
					std::size_t rx_start = src_to_rx(sx);
					std::size_t rx_end   = src_to_rx(ex);
					// Apply horizontal scroll offset
					if (rx_end <= coloffs_now)
						continue; // fully left of view
					ImVec2 p0 = ImVec2(line_pos.x + rx_to_px(rx_start), line_pos.y);
					ImVec2 p1 = ImVec2(line_pos.x + rx_to_px(rx_end),
					                   line_pos.y + line_h);
					// Choose color: current match stronger
					bool is_current = has_current && sx == cur_x && ex == cur_end;
					ImU32 col       = is_current
						                  ? IM_COL32(255, 220, 120, 140)
						                  : IM_COL32(200, 200, 0, 90);
					ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
				}
			}

			// Draw selection background (over search highlight; under text)
			if (sel_active) {
				bool line_has  = false;
				std::size_t sx = 0, ex = 0;
				if (i < sel_sy || i > sel_ey) {
					line_has = false;
				} else if (sel_sy == sel_ey) {
					sx       = sel_sx;
					ex       = sel_ex;
					line_has = ex > sx;
				} else if (i == sel_sy) {
					sx       = sel_sx;
					ex       = line.size();
					line_has = ex > sx;
				} else if (i == sel_ey) {
					sx       = 0;
					ex       = std::min(sel_ex, line.size());
					line_has = ex > sx;
				} else {
					sx       = 0;
					ex       = line.size();
					line_has = ex > sx;
				}
				if (line_has) {
					std::size_t rx_start = src_to_rx(sx);
					std::size_t rx_end   = src_to_rx(ex);
					if (rx_end > coloffs_now) {
						ImVec2 p0 = ImVec2(line_pos.x + rx_to_px(rx_start),
						                   line_pos.y);
						ImVec2 p1 = ImVec2(line_pos.x + rx_to_px(rx_end),
						                   line_pos.y + line_h);
						ImU32 col = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
						ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
					}
				}
			}
			if (vsel_active && i >= vsel_sy && i <= vsel_ey) {
				// Visual-line (multi-cursor) mode: highlight only the per-line cursor spot.
				const std::size_t spot_sx  = std::min(buf->Curx(), line.size());
				const std::size_t rx_start = src_to_rx(spot_sx);
				std::size_t rx_end         = rx_start;
				if (spot_sx < line.size()) {
					rx_end = src_to_rx(spot_sx + 1);
				} else {
					// EOL spot: draw a 1-cell highlight just past the last character.
					rx_end = rx_start + 1;
				}
				if (rx_end > coloffs_now) {
					ImVec2 p0 = ImVec2(line_pos.x + rx_to_px(rx_start),
					                   line_pos.y);
					ImVec2 p1 = ImVec2(line_pos.x + rx_to_px(rx_end),
					                   line_pos.y + line_h);
					ImU32 col = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
					ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
				}
			}
			// Draw syntax-colored runs (text above background highlights)
			if (buf->SyntaxEnabled() && buf->Highlighter() && buf->Highlighter()->HasHighlighter()) {
				kte::LineHighlight lh = buf->Highlighter()->GetLine(
					*buf, static_cast<int>(i), buf->Version());
				// Sanitize spans defensively: clamp to [0, line.size()], ensure end>=start, drop empties
				struct SSpan {
					std::size_t s;
					std::size_t e;
					kte::TokenKind k;
				};
				std::vector<SSpan> spans;
				spans.reserve(lh.spans.size());
				const std::size_t line_len = line.size();
				for (const auto &sp: lh.spans) {
					int s_raw = sp.col_start;
					int e_raw = sp.col_end;
					if (e_raw < s_raw)
						std::swap(e_raw, s_raw);
					std::size_t s = static_cast<std::size_t>(std::max(
						0, std::min(s_raw, static_cast<int>(line_len))));
					std::size_t e = static_cast<std::size_t>(std::max(
						static_cast<int>(s), std::min(e_raw, static_cast<int>(line_len))));
					if (e <= s)
						continue;
					spans.push_back(SSpan{s, e, sp.kind});
				}
				std::sort(spans.begin(), spans.end(), [](const SSpan &a, const SSpan &b) {
					return a.s < b.s;
				});

				// Helper to convert a src column to expanded rx position
				auto src_to_rx_full = [&](std::size_t sidx) -> std::size_t {
					std::size_t rx = 0;
					for (std::size_t k = 0; k < sidx && k < line.size(); ++k) {
						rx += (line[k] == '\t') ? (tabw - (rx % tabw)) : 1;
					}
					return rx;
				};

				for (const auto &sp: spans) {
					std::size_t rx_s = src_to_rx_full(sp.s);
					std::size_t rx_e = src_to_rx_full(sp.e);
					if (rx_e <= coloffs_now)
						continue; // fully left of viewport
					// Clamp to visible portion and expanded length
					std::size_t draw_start = (rx_s > coloffs_now) ? rx_s : coloffs_now;
					if (draw_start >= expanded.size())
						continue; // fully right of expanded text
					std::size_t draw_end = std::min<std::size_t>(rx_e, expanded.size());
					if (draw_end <= draw_start)
						continue;
					// Screen position via actual text measurement
					ImU32 col = ImGui::GetColorU32(kte::SyntaxInk(sp.k));
					ImVec2 p = ImVec2(line_pos.x + rx_to_px(draw_start),
					                  line_pos.y);
					ImGui::GetWindowDrawList()->AddText(
						p, col, expanded.c_str() + draw_start, expanded.c_str() + draw_end);
				}
				// We drew text via draw list (no layout advance). Manually advance the cursor to the next line.
				// Use row_h (with spacing) to match click calculation and ensure consistent line positions.
				ImGui::SetCursorScreenPos(ImVec2(line_pos.x, line_pos.y + row_h));
			} else {
				// No syntax: draw as one run, accounting for horizontal scroll offset
				if (coloffs_now < expanded.size()) {
					ImVec2 p = ImVec2(line_pos.x, line_pos.y);
					ImGui::GetWindowDrawList()->AddText(
						p, ImGui::GetColorU32(ImGuiCol_Text),
						expanded.c_str() + coloffs_now);
					ImGui::SetCursorScreenPos(ImVec2(line_pos.x, line_pos.y + row_h));
				} else {
					// Line is fully scrolled out of view horizontally
					ImGui::SetCursorScreenPos(ImVec2(line_pos.x, line_pos.y + row_h));
				}
			}

			// Draw a visible cursor indicator on the current line
			if (i == cy) {
				std::size_t rx_abs = src_to_rx(cx);
				float cursor_px = rx_to_px(rx_abs);
				ImVec2 p0 = ImVec2(line_pos.x + cursor_px, line_pos.y);
				ImVec2 p1 = ImVec2(p0.x + space_w, p0.y + line_h);
				ImU32 col = IM_COL32(200, 200, 255, 128); // soft highlight
				ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
			}
		}
		// Synchronize cursor and scrolling after rendering all lines so content size is known.
		{
			float child_h_actual = ImGui::GetWindowHeight();
			float child_w_actual = ImGui::GetWindowWidth();
			float scroll_y_now   = ImGui::GetScrollY();
			float scroll_x_now   = ImGui::GetScrollX();

			long first_row = static_cast<long>(scroll_y_now / row_h);
			long vis_rows  = static_cast<long>(std::round(child_h_actual / row_h));
			if (vis_rows < 1)
				vis_rows = 1;
			long last_row = first_row + vis_rows - 1;

			long cyr = static_cast<long>(cy);
			if (cyr < first_row) {
				float target = static_cast<float>(cyr) * row_h;
				if (target < 0.f)
					target = 0.f;
				float max_y = ImGui::GetScrollMaxY();
				if (max_y >= 0.f && target > max_y)
					target = max_y;
				ImGui::SetScrollY(target);
				first_row = static_cast<long>(target / row_h);
				last_row  = first_row + vis_rows - 1;
			} else if (cyr > last_row) {
				long new_first = cyr - vis_rows + 1;
				if (new_first < 0)
					new_first = 0;
				float target = static_cast<float>(new_first) * row_h;
				float max_y  = ImGui::GetScrollMaxY();
				if (target < 0.f)
					target = 0.f;
				if (max_y >= 0.f && target > max_y)
					target = max_y;
				ImGui::SetScrollY(target);
				first_row = static_cast<long>(target / row_h);
				last_row  = first_row + vis_rows - 1;
			}

			// Horizontal scroll: ensure cursor is visible (pixel-based for proportional fonts)
			float cursor_px_abs = 0.0f;
			if (cy < lines.size()) {
				std::string cur_line   = static_cast<std::string>(lines[cy]);
				const std::size_t tabw = 8;
				// Expand tabs for cursor line to measure pixel position
				std::string cur_expanded;
				cur_expanded.reserve(cur_line.size() + 16);
				std::size_t cur_rx = 0;
				for (std::size_t ci = 0; ci < cur_line.size(); ++ci) {
					if (cur_line[ci] == '\t') {
						std::size_t adv = tabw - (cur_rx % tabw);
						cur_expanded.append(adv, ' ');
						cur_rx += adv;
					} else {
						cur_expanded.push_back(cur_line[ci]);
						cur_rx += 1;
					}
				}
				// Compute rendered column of cursor
				std::size_t cursor_rx = 0;
				for (std::size_t ci = 0; ci < cx && ci < cur_line.size(); ++ci) {
					if (cur_line[ci] == '\t')
						cursor_rx += tabw - (cursor_rx % tabw);
					else
						cursor_rx += 1;
				}
				std::size_t exp_end = std::min(cur_expanded.size(), cursor_rx);
				if (exp_end > 0)
					cursor_px_abs = ImGui::CalcTextSize(cur_expanded.c_str(),
					                                    cur_expanded.c_str() + exp_end).x;
			}
			if (cursor_px_abs < scroll_x_now || cursor_px_abs > scroll_x_now + child_w_actual) {
				float target_x = cursor_px_abs - (child_w_actual / 2.0f);
				if (target_x < 0.f)
					target_x = 0.f;
				float max_x = ImGui::GetScrollMaxX();
				if (max_x >= 0.f && target_x > max_x)
					target_x = max_x;
				ImGui::SetScrollX(target_x);
			}

			if (buf->SyntaxEnabled() && buf->Highlighter() && buf->Highlighter()->HasHighlighter()) {
				int fr = static_cast<int>(std::max(0L, first_row));
				int rc = static_cast<int>(std::max(1L, vis_rows));
				buf->Highlighter()->PrefetchViewport(*buf, fr, rc, buf->Version());
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar(2); // WindowPadding, ItemSpacing

		// Status bar area starting right after the scroll child
		ImVec2 win_pos = ImGui::GetWindowPos();
		ImVec2 win_sz  = ImGui::GetWindowSize();
		float x0       = win_pos.x;
		float x1       = win_pos.x + win_sz.x;
		float y0       = ImGui::GetCursorScreenPos().y;
		float bar_h    = real_bar_h;

		ImVec2 p0(x0, y0);
		ImVec2 p1(x1, y0 + bar_h);
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

			float pad     = 6.f;
			float left_x  = p0.x + pad;
			float right_x = p1.x - pad;
			float max_px  = std::max(0.0f, right_x - left_x);

			std::string prefix;
			if (kind == Editor::PromptKind::Command) {
				prefix = ": ";
			} else if (!label.empty()) {
				prefix = label + ": ";
			}

			// Compose showing right-end of filename portion when too long for space
			std::string final_msg;
			ImVec2 prefix_sz = ImGui::CalcTextSize(prefix.c_str());
			float avail_px   = std::max(0.0f, max_px - prefix_sz.x);
			if ((kind == Editor::PromptKind::OpenFile || kind == Editor::PromptKind::SaveAs || kind ==
			     Editor::PromptKind::Chdir) && avail_px > 0.0f) {
				// Trim from left until it fits by pixel width
				std::string tail = ptext;
				ImVec2 tail_sz   = ImGui::CalcTextSize(tail.c_str());
				if (tail_sz.x > avail_px) {
					// Remove leading chars until it fits
					// Use a simple loop; text lengths are small here
					size_t start = 0;
					// To avoid O(n^2) worst-case, remove chunks
					while (start < tail.size()) {
						// Estimate how many chars to skip based on ratio
						float ratio = tail_sz.x / avail_px;
						size_t skip = ratio > 1.5f
							              ? std::min(tail.size() - start,
								              (size_t) std::max<size_t>(
									              1, (size_t) (tail.size() / 4)))
							              : 1;
						start                 += skip;
						std::string candidate = tail.substr(start);
						ImVec2 cand_sz        = ImGui::CalcTextSize(candidate.c_str());
						if (cand_sz.x <= avail_px) {
							tail    = candidate;
							tail_sz = cand_sz;
							break;
						}
					}
					if (ImGui::CalcTextSize(tail.c_str()).x > avail_px && !tail.empty()) {
						// As a last resort, ensure fit by chopping exactly
						// binary reduce
						size_t lo = 0, hi = tail.size();
						while (lo < hi) {
							size_t mid       = (lo + hi) / 2;
							std::string cand = tail.substr(mid);
							if (ImGui::CalcTextSize(cand.c_str()).x <= avail_px)
								hi = mid;
							else
								lo = mid + 1;
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
			ImGui::SetCursorScreenPos(ImVec2(left_x, y0 + (bar_h - msg_sz.y) * 0.5f));
			ImGui::TextUnformatted(final_msg.c_str());
			ImGui::PopClipRect();
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
					left             += "[";
					left             += std::to_string(static_cast<unsigned long long>(idx1));
					left             += "/";
					left             += std::to_string(static_cast<unsigned long long>(total));
					left             += "] ";
				}
			}
			left += fname;
			if (buf->Dirty())
				left += " *";
			// Append total line count as "<n>L"
			{
				unsigned long lcount = static_cast<unsigned long>(buf->Rows().size());
				left                 += " ";
				left                 += std::to_string(lcount);
				left                 += "L";
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
					ImGui::SetCursorScreenPos(ImVec2(left_x, y0 + (bar_h - left_sz.y) * 0.5f));
					ImGui::PushClipRect(ImVec2(left_x, y0), ImVec2(right_x - pad, y0 + bar_h),
					                    true);
					ImGui::TextUnformatted(left.c_str());
					ImGui::PopClipRect();
				}
			} else {
				// Draw left normally
				ImGui::SetCursorScreenPos(ImVec2(left_x, y0 + (bar_h - left_sz.y) * 0.5f));
				ImGui::TextUnformatted(left.c_str());
			}

			// Draw right
			ImGui::SetCursorScreenPos(ImVec2(std::max(right_x, left_x),
			                                 y0 + (bar_h - right_sz.y) * 0.5f));
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
					ImGui::PushClipRect(ImVec2(mid_left, y0), ImVec2(mid_right, y0 + bar_h), true);
					ImGui::SetCursorScreenPos(ImVec2(msg_x, y0 + (bar_h - msg_sz.y) * 0.5f));
					ImGui::TextUnformatted(msg.c_str());
					ImGui::PopClipRect();
				}
			}
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
						ed.RequestOpenFile(e.path.string());
						(void) ed.ProcessPendingOpens();
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