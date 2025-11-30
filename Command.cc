#include <algorithm>

#include "Command.h"
#include "Editor.h"
#include "Buffer.h"
#include "UndoSystem.h"
// Note: Command layer must remain UI-agnostic. Do not include frontend/IO headers here.


// Keep buffer viewport offsets so that the cursor stays within the visible
// window based on the editor's current dimensions. The bottom row is reserved
// for the status line.
static std::size_t
compute_render_x(const std::string &line, std::size_t curx, std::size_t tabw)
{
	std::size_t rx = 0;
	for (std::size_t i = 0; i < curx && i < line.size(); ++i) {
		if (line[i] == '\t') {
			rx += tabw - (rx % tabw);
		} else {
			rx += 1;
		}
	}
	return rx;
}


static void
ensure_cursor_visible(const Editor &ed, Buffer &buf)
{
	const std::size_t rows = ed.Rows();
	const std::size_t cols = ed.Cols();
	if (rows == 0 || cols == 0)
		return;

	const std::size_t content_rows = rows > 0 ? rows - 1 : 0; // last row = status
	const std::size_t cury         = buf.Cury();
	const std::size_t curx         = buf.Curx();
	std::size_t rowoffs            = buf.Rowoffs();
	std::size_t coloffs            = buf.Coloffs();

	// Vertical scrolling
	if (cury < rowoffs) {
		rowoffs = cury;
	} else if (content_rows > 0 && cury >= rowoffs + content_rows) {
		rowoffs = cury - content_rows + 1;
	}

	// Clamp vertical offset to available content
	const auto total_rows = buf.Rows().size();
	if (content_rows < total_rows) {
		std::size_t max_rowoffs = total_rows - content_rows;
		if (rowoffs > max_rowoffs)
			rowoffs = max_rowoffs;
	} else {
		rowoffs = 0;
	}

	// Horizontal scrolling (use rendered columns with tabs expanded)
	std::size_t rx    = 0;
	const auto &lines = buf.Rows();
	if (cury < lines.size()) {
		rx = compute_render_x(lines[cury], curx, 8);
	}
	if (rx < coloffs) {
		coloffs = rx;
	} else if (rx >= coloffs + cols) {
		coloffs = rx - cols + 1;
	}

	buf.SetOffsets(rowoffs, coloffs);
	buf.SetRenderX(rx);
}


static void
ensure_at_least_one_line(Buffer &buf)
{
	if (buf.Rows().empty()) {
		buf.Rows().emplace_back("");
		buf.SetDirty(true);
	}
}


// --- UI/status helpers ---
static bool
cmd_uarg_status(CommandContext &ctx)
{
	// ctx.arg should contain the digits/minus entered so far (may be empty)
	ctx.editor.SetStatus(std::string("C-u ") + ctx.arg);
	return true;
}


// Helper: compute ordered region between mark and cursor. Returns false if no mark set or zero-length.
static bool
compute_mark_region(Buffer &buf, std::size_t &sx, std::size_t &sy, std::size_t &ex, std::size_t &ey)
{
	if (!buf.MarkSet())
		return false;
	std::size_t cx = buf.Curx();
	std::size_t cy = buf.Cury();
	std::size_t mx = buf.MarkCurx();
	std::size_t my = buf.MarkCury();
	if (cy < my || (cy == my && cx < mx)) {
		sy = cy;
		sx = cx;
		ey = my;
		ex = mx;
	} else {
		sy = my;
		sx = mx;
		ey = cy;
		ex = cx;
	}
	if (sy == ey && sx == ex)
		return false; // empty region
	return true;
}


// Helper: extract text from [sx,sy) to [ex,ey) without modifying buffer. Newlines inserted between lines.
static std::string
extract_region_text(const Buffer &buf, std::size_t sx, std::size_t sy, std::size_t ex, std::size_t ey)
{
	const auto &rows = buf.Rows();
	if (sy >= rows.size())
		return std::string();
	if (ey >= rows.size())
		ey = rows.size() - 1;
	if (sy == ey) {
		const auto &line = rows[sy];
		std::size_t xs   = std::min(sx, line.size());
		std::size_t xe   = std::min(ex, line.size());
		if (xe < xs)
			std::swap(xs, xe);
		return line.substr(xs, xe - xs);
	}
	std::string out;
	// first line tail
	{
		const auto &line = rows[sy];
		std::size_t xs   = std::min(sx, line.size());
		out += line.substr(xs);
		out += '\n';
	}
	// middle lines full
	for (std::size_t y = sy + 1; y < ey; ++y) {
		out += rows[y];
		out += '\n';
	}
	// last line head
	{
		const auto &line = rows[ey];
		std::size_t xe   = std::min(ex, line.size());
		out += line.substr(0, xe);
	}
	return out;
}


// Helper: delete region and leave cursor at start (sx,sy). Adjust lines appropriately.
static void
delete_region(Buffer &buf, std::size_t sx, std::size_t sy, std::size_t ex, std::size_t ey)
{
	auto &rows = buf.Rows();
	if (rows.empty())
		return;
	if (sy >= rows.size())
		return;
	if (ey >= rows.size())
		ey = rows.size() - 1;
	if (sy == ey) {
		auto &line     = rows[sy];
		std::size_t xs = std::min(sx, line.size());
		std::size_t xe = std::min(ex, line.size());
		if (xe < xs)
			std::swap(xs, xe);
		line.erase(xs, xe - xs);
	} else {
		// Keep prefix of first and suffix of last then join
		std::string prefix = rows[sy].substr(0, std::min(sx, rows[sy].size()));
		std::string suffix;
		{
			const auto &last = rows[ey];
			std::size_t xe   = std::min(ex, last.size());
			suffix           = last.substr(xe);
		}
		rows[sy] = prefix + suffix;
		// erase middle lines and the last line
		rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(sy + 1),
		           rows.begin() + static_cast<std::ptrdiff_t>(ey + 1));
	}
	buf.SetCursor(sx, sy);
	buf.SetDirty(true);
}


// Insert arbitrary text at cursor, supporting newlines. Updates cursor position.
static void
insert_text_at_cursor(Buffer &buf, const std::string &text)
{
	auto &rows    = buf.Rows();
	std::size_t y = buf.Cury();
	std::size_t x = buf.Curx();
	if (y > rows.size())
		y = rows.size();
	if (rows.empty())
		rows.emplace_back("");
	if (y >= rows.size())
		rows.emplace_back("");

	std::size_t cur_y = y;
	std::size_t cur_x = x;

	std::string remain = text;
	while (true) {
		auto pos = remain.find('\n');
		if (pos == std::string::npos) {
			// insert remaining into current line
			if (cur_y >= rows.size())
				rows.emplace_back("");
			if (cur_x > rows[cur_y].size())
				cur_x = rows[cur_y].size();
			rows[cur_y].insert(cur_x, remain);
			cur_x += remain.size();
			break;
		}
		// insert segment before newline
		std::string seg = remain.substr(0, pos);
		if (cur_x > rows[cur_y].size())
			cur_x = rows[cur_y].size();
		rows[cur_y].insert(cur_x, seg);
		// split line at cur_x + seg.size()
		cur_x += seg.size();
		std::string after = rows[cur_y].substr(cur_x);
		rows[cur_y].erase(cur_x);
		// create new line after current with the 'after' tail
		rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(cur_y + 1), after);
		// move to start of next line
		cur_y += 1;
		cur_x = 0;
		// advance remain after newline
		remain.erase(0, pos + 1);
	}

	buf.SetCursor(cur_x, cur_y);
	buf.SetDirty(true);
}


static std::size_t
inverse_render_to_source_col(const std::string &line, std::size_t rx_target, std::size_t tabw)
{
	// Find source column that best matches given rendered x (with tabs expanded)
	std::size_t rx = 0;
	if (rx_target == 0)
		return 0;
	std::size_t best_col  = 0;
	std::size_t best_dist = rx_target; // max
	for (std::size_t i = 0; i <= line.size(); ++i) {
		std::size_t dist = (rx > rx_target) ? (rx - rx_target) : (rx_target - rx);
		if (dist <= best_dist) {
			best_dist = dist;
			best_col  = i;
		}
		if (i == line.size())
			break;
		if (line[i] == '\t') {
			rx += tabw - (rx % tabw);
		} else {
			rx += 1;
		}
		if (rx >= rx_target && i + 1 <= line.size()) {
			// next iteration will evaluate i+1 with updated rx
		}
	}
	if (best_col > line.size())
		best_col = line.size();
	return best_col;
}


// --- Direct cursor placement ---
static bool
cmd_move_cursor_to(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	ensure_at_least_one_line(*buf);
	// Accept either:
	//  - "row:col" (buffer coordinates)
	//  - "@row:col" (screen coordinates within viewport; translated using offsets)
	std::size_t row      = buf->Cury();
	std::size_t col      = buf->Curx();
	const std::string &a = ctx.arg;
	if (!a.empty()) {
		bool screen       = false;
		std::size_t start = 0;
		if (!a.empty() && a[0] == '@') {
			screen = true;
			start  = 1;
		}
		std::size_t p = a.find(':', start);
		if (p != std::string::npos) {
			try {
				long ay = std::stol(a.substr(start, p - start));
				long ax = std::stol(a.substr(p + 1));
				if (ay < 0)
					ay = 0;
				if (ax < 0)
					ax = 0;
				if (screen) {
					// Translate screen to buffer coordinates using offsets and tab expansion for x
					std::size_t vy  = static_cast<std::size_t>(ay);
					std::size_t vx  = static_cast<std::size_t>(ax);
					std::size_t bro = buf->Rowoffs();
					std::size_t bco = buf->Coloffs();
					std::size_t by  = bro + vy;
					// Clamp by to existing lines later
					auto &lines2 = buf->Rows();
					if (lines2.empty()) {
						lines2.emplace_back("");
					}
					if (by >= lines2.size())
						by = lines2.size() - 1;
					const std::string &line2 = lines2[by];
					std::size_t rx_target    = bco + vx;
					std::size_t sx           = inverse_render_to_source_col(line2, rx_target, 8);
					row                      = by;
					col                      = sx;
				} else {
					row = static_cast<std::size_t>(ay);
					col = static_cast<std::size_t>(ax);
				}
			} catch (...) {
				// ignore parse errors
			}
		}
	}
	auto &lines = buf->Rows();
	if (lines.empty()) {
		lines.emplace_back("");
	}
	if (row >= lines.size())
		row = lines.size() - 1;
	const std::string &line = lines[row];
	if (col > line.size())
		col = line.size();
	buf->SetCursor(col, row);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


// --- Search helpers (UI-agnostic) ---
static std::vector<std::pair<std::size_t, std::size_t> >
search_compute_matches(const Buffer &buf, const std::string &q)
{
	std::vector<std::pair<std::size_t, std::size_t> > out;
	if (q.empty())
		return out;
	const auto &rows = buf.Rows();
	for (std::size_t y = 0; y < rows.size(); ++y) {
		const std::string &line = rows[y];
		std::size_t pos         = 0;
		while (!q.empty() && (pos = line.find(q, pos)) != std::string::npos) {
			out.emplace_back(y, pos);
			pos += q.size();
		}
	}
	return out;
}


static void
search_apply_match(Editor &ed, Buffer &buf, const std::vector<std::pair<std::size_t, std::size_t> > &matches)
{
	const std::string &q = ed.SearchQuery();
	if (matches.empty()) {
		ed.SetSearchMatch(0, 0, 0);
		// Restore cursor to origin if present
		if (ed.SearchOriginSet()) {
			buf.SetCursor(ed.SearchOrigX(), ed.SearchOrigY());
			buf.SetOffsets(ed.SearchOrigRowoffs(), ed.SearchOrigColoffs());
		}
		ed.SetSearchIndex(-1);
		ed.SetStatus("Find: " + q);
		return;
	}
	int idx = ed.SearchIndex();
	if (idx < 0 || idx >= static_cast<int>(matches.size()))
		idx = 0;
	auto [y, x] = matches[static_cast<std::size_t>(idx)];
	ed.SetSearchMatch(y, x, q.size());
	buf.SetCursor(x, y);
	ensure_cursor_visible(ed, buf);
	char tmp[64];
	snprintf(tmp, sizeof(tmp), "%d/%zu", idx + 1, matches.size());
	ed.SetStatus(std::string("Find: ") + q + "  " + tmp);
	ed.SetSearchIndex(idx);
}


// --- File/Session commands ---
static bool
cmd_save(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to save");
		return false;
	}
	std::string err;
	// Allow saving directly to a filename if buffer was opened with a
	// non-existent path (not yet file-backed but has a filename).
	if (!buf->IsFileBacked()) {
		if (!buf->Filename().empty()) {
			if (!buf->SaveAs(buf->Filename(), err)) {
				ctx.editor.SetStatus(err);
				return false;
			}
			buf->SetDirty(false);
			ctx.editor.SetStatus("Saved " + buf->Filename());
			return true;
		}
		// If buffer has no name, prompt for a filename
		ctx.editor.StartPrompt(Editor::PromptKind::SaveAs, "Save as", "");
		ctx.editor.SetStatus("Save as: ");
		return true;
	}
	if (!buf->Save(err)) {
		ctx.editor.SetStatus(err);
		return false;
	}
	buf->SetDirty(false);
	ctx.editor.SetStatus("Saved " + buf->Filename());
	if (auto *u = buf->Undo())
		u->mark_saved();
	return true;
}


static bool
cmd_save_as(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to save");
		return false;
	}
	if (ctx.arg.empty()) {
		ctx.editor.SetStatus("save-as requires a filename");
		return false;
	}
	std::string err;
	if (!buf->SaveAs(ctx.arg, err)) {
		ctx.editor.SetStatus(err);
		return false;
	}
	ctx.editor.SetStatus("Saved as " + ctx.arg);
	if (auto *u = buf->Undo())
		u->mark_saved();
	return true;
}


static bool
cmd_quit(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	// If a confirmation is already pending, quit now without saving
	if (ctx.editor.QuitConfirmPending()) {
		ctx.editor.SetQuitConfirmPending(false);
		ctx.editor.SetQuitRequested(true);
		ctx.editor.SetStatus("Quit requested");
		return true;
	}
	// If current buffer exists and is dirty, warn and arm confirmation
	if (buf && buf->Dirty()) {
		ctx.editor.SetStatus("Unsaved changes. C-k q to quit without saving");
		ctx.editor.SetQuitConfirmPending(true);
		return true;
	}
	// Otherwise quit immediately
	ctx.editor.SetQuitRequested(true);
	ctx.editor.SetStatus("Quit requested");
	return true;
}


static bool
cmd_save_and_quit(CommandContext &ctx)
{
	// Try save current buffer (if any), then mark quit requested.
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (buf && buf->Dirty()) {
		std::string err;
		if (buf->IsFileBacked()) {
			if (buf->Save(err)) {
				buf->SetDirty(false);
			} else {
				ctx.editor.SetStatus(err);
				return false;
			}
		} else if (!buf->Filename().empty()) {
			if (buf->SaveAs(buf->Filename(), err)) {
				buf->SetDirty(false);
			} else {
				ctx.editor.SetStatus(err);
				return false;
			}
		} else {
			ctx.editor.SetStatus("Buffer not file-backed; use save-as before quitting");
			return false;
		}
	}
	ctx.editor.SetStatus("Save and quit requested");
	ctx.editor.SetQuitRequested(true);
	return true;
}


static bool
cmd_quit_now(CommandContext &ctx)
{
	ctx.editor.SetQuitRequested(true);
	ctx.editor.SetStatus("Quit requested");
	return true;
}


static bool
cmd_refresh(CommandContext &ctx)
{
	// If a generic prompt is active, cancel it
	if (ctx.editor.PromptActive()) {
		// If also in search mode, restore state
		if (ctx.editor.SearchActive()) {
			Buffer *buf = ctx.editor.CurrentBuffer();
			if (buf && ctx.editor.SearchOriginSet()) {
				buf->SetCursor(ctx.editor.SearchOrigX(), ctx.editor.SearchOrigY());
				buf->SetOffsets(ctx.editor.SearchOrigRowoffs(), ctx.editor.SearchOrigColoffs());
			}
			ctx.editor.SetSearchActive(false);
			ctx.editor.SetSearchQuery("");
			ctx.editor.SetSearchMatch(0, 0, 0);
			ctx.editor.ClearSearchOrigin();
			ctx.editor.SetSearchIndex(-1);
		}
		ctx.editor.CancelPrompt();
		ctx.editor.SetStatus("Canceled");
		return true;
	}
	// If in search mode (legacy flag), treat refresh/ESC/C-g as cancel
	if (ctx.editor.SearchActive()) {
		Buffer *buf = ctx.editor.CurrentBuffer();
		if (buf && ctx.editor.SearchOriginSet()) {
			buf->SetCursor(ctx.editor.SearchOrigX(), ctx.editor.SearchOrigY());
			buf->SetOffsets(ctx.editor.SearchOrigRowoffs(), ctx.editor.SearchOrigColoffs());
		}
		ctx.editor.SetSearchActive(false);
		ctx.editor.SetSearchQuery("");
		ctx.editor.SetSearchMatch(0, 0, 0);
		ctx.editor.ClearSearchOrigin();
		ctx.editor.SetSearchIndex(-1);
		ctx.editor.SetStatus("Find canceled");
		return true;
	}
	// Otherwise just a hint; renderer will redraw
	ctx.editor.SetStatus("");
	return true;
}


static bool
cmd_kprefix(CommandContext &ctx)
{
	// Close any pending edit batch before entering k-prefix
	if (Buffer *b = ctx.editor.CurrentBuffer()) {
		if (auto *u = b->Undo())
			u->commit();
	}
	// Show k-command mode hint in status
	ctx.editor.SetStatus("C-k _");
	return true;
}


static bool
cmd_unknown_kcommand(CommandContext &ctx)
{
	char ch = '?';
	if (!ctx.arg.empty()) {
		ch = ctx.arg[0];
	}
	char buf[64];
	std::snprintf(buf, sizeof(buf), "unknown k-command %c", ch);
	ctx.editor.SetStatus(buf);
	return true;
}


static bool
cmd_find_start(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to search");
		return false;
	}

	// Save original cursor/viewport to restore on cancel
	ctx.editor.SetSearchOrigin(buf->Curx(), buf->Cury(), buf->Rowoffs(), buf->Coloffs());

	// Enter search mode; start a generic prompt for search
	ctx.editor.SetSearchActive(true);
	ctx.editor.SetSearchQuery("");
	ctx.editor.SetSearchMatch(0, 0, 0);
	ctx.editor.SetSearchIndex(-1);
	ctx.editor.StartPrompt(Editor::PromptKind::Search, "Find", "");
	ctx.editor.SetStatus("Find: ");
	return true;
}


static bool
cmd_open_file_start(CommandContext &ctx)
{
	// Start a generic prompt to read a path
	ctx.editor.StartPrompt(Editor::PromptKind::OpenFile, "Open", "");
	ctx.editor.SetStatus("Open: ");
	return true;
}


// --- Buffers: switch/next/prev/close ---
static bool
cmd_buffer_switch_start(CommandContext &ctx)
{
	// If only one (or zero) buffer is open, do nothing per spec
	if (ctx.editor.BufferCount() <= 1) {
		ctx.editor.SetStatus("No other buffers open.");
		return true;
	}
	ctx.editor.StartPrompt(Editor::PromptKind::BufferSwitch, "Buffer", "");
	ctx.editor.SetStatus("Buffer: ");
	return true;
}


static std::string
buffer_display_name(const Buffer &b)
{
	if (!b.Filename().empty())
		return b.Filename();
	return std::string("<untitled>");
}


static std::string
buffer_basename(const Buffer &b)
{
	const std::string &p = b.Filename();
	if (p.empty())
		return std::string("<untitled>");
	auto pos = p.find_last_of("/\\");
	if (pos == std::string::npos)
		return p;
	return p.substr(pos + 1);
}


static bool
cmd_buffer_next(CommandContext &ctx)
{
	const auto cnt = ctx.editor.BufferCount();
	if (cnt <= 1) {
		ctx.editor.SetStatus("No other buffers open.");
		return true;
	}
	std::size_t idx = ctx.editor.CurrentBufferIndex();
	idx             = (idx + 1) % cnt;
	ctx.editor.SwitchTo(idx);
	const Buffer *b = ctx.editor.CurrentBuffer();
	ctx.editor.SetStatus(std::string("Switched: ") + (b ? buffer_display_name(*b) : std::string("")));
	return true;
}


static bool
cmd_buffer_prev(CommandContext &ctx)
{
	const auto cnt = ctx.editor.BufferCount();
	if (cnt <= 1) {
		ctx.editor.SetStatus("No other buffers open.");
		return true;
	}
	std::size_t idx = ctx.editor.CurrentBufferIndex();
	idx             = (idx + cnt - 1) % cnt;
	ctx.editor.SwitchTo(idx);
	const Buffer *b = ctx.editor.CurrentBuffer();
	ctx.editor.SetStatus(std::string("Switched: ") + (b ? buffer_display_name(*b) : std::string("")));
	return true;
}


static bool
cmd_buffer_close(CommandContext &ctx)
{
	if (ctx.editor.BufferCount() == 0)
		return true;
	std::size_t idx  = ctx.editor.CurrentBufferIndex();
	Buffer *b        = ctx.editor.CurrentBuffer();
	std::string name = b ? buffer_display_name(*b) : std::string("");
	if (b && b->Undo())
		b->Undo()->discard_pending();
	ctx.editor.CloseBuffer(idx);
	if (ctx.editor.BufferCount() == 0) {
		// Open a fresh empty buffer
		Buffer empty;
		ctx.editor.AddBuffer(std::move(empty));
		ctx.editor.SwitchTo(0);
	}
	const Buffer *cur = ctx.editor.CurrentBuffer();
	ctx.editor.SetStatus(std::string("Closed: ") + name + std::string("  Now: ")
	                     + (cur ? buffer_display_name(*cur) : std::string("")));
	return true;
}


// --- Editing ---
static bool
cmd_insert_text(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to edit");
		return false;
	}
	// If a prompt is active, edit prompt text
	if (ctx.editor.PromptActive()) {
		// Special-case: buffer switch prompt supports Tab-completion
		if (ctx.editor.CurrentPromptKind() == Editor::PromptKind::BufferSwitch && ctx.arg == "\t") {
			// Complete against buffer names (path and basename)
			const std::string prefix = ctx.editor.PromptText();
			std::vector<std::pair<std::string, std::size_t> > cands; // name, index
			const auto &bs = ctx.editor.Buffers();
			for (std::size_t i = 0; i < bs.size(); ++i) {
				std::string full = buffer_display_name(bs[i]);
				std::string base = buffer_basename(bs[i]);
				if (full.rfind(prefix, 0) == 0) {
					cands.emplace_back(full, i);
				}
				if (base.rfind(prefix, 0) == 0 && base != full) {
					cands.emplace_back(base, i);
				}
			}
			if (cands.empty()) {
				// no change
			} else if (cands.size() == 1) {
				ctx.editor.SetPromptText(cands[0].first);
			} else {
				// extend to longest common prefix
				std::string lcp = cands[0].first;
				for (std::size_t i = 1; i < cands.size(); ++i) {
					const std::string &s = cands[i].first;
					std::size_t j        = 0;
					while (j < lcp.size() && j < s.size() && lcp[j] == s[j])
						++j;
					lcp.resize(j);
					if (lcp.empty())
						break;
				}
				if (!lcp.empty() && lcp != ctx.editor.PromptText())
					ctx.editor.SetPromptText(lcp);
			}
			ctx.editor.SetStatus(ctx.editor.PromptLabel() + ": " + ctx.editor.PromptText());
			return true;
		}

		ctx.editor.AppendPromptText(ctx.arg);
		// If it's a search prompt, mirror text to search state
		if (ctx.editor.CurrentPromptKind() == Editor::PromptKind::Search) {
			ctx.editor.SetSearchQuery(ctx.editor.PromptText());
			auto matches = search_compute_matches(*buf, ctx.editor.SearchQuery());
			// Keep index stable unless out of range
			if (ctx.editor.SearchIndex() >= static_cast<int>(matches.size()))
				ctx.editor.SetSearchIndex(matches.empty() ? -1 : 0);
			search_apply_match(ctx.editor, *buf, matches);
		} else {
			// For other prompts, just echo label:text in status
			ctx.editor.SetStatus(ctx.editor.PromptLabel() + ": " + ctx.editor.PromptText());
		}
		return true;
	}
	// If in search mode, treat printable input as query update
	if (ctx.editor.SearchActive()) {
		std::string q = ctx.editor.SearchQuery();
		q += ctx.arg; // arg already printable text
		ctx.editor.SetSearchQuery(q);

		// Recompute matches and move cursor to current index
		const auto &rows = buf->Rows();
		std::vector<std::pair<std::size_t, std::size_t> > matches;
		if (!q.empty()) {
			for (std::size_t y = 0; y < rows.size(); ++y) {
				const std::string &line = rows[y];
				std::size_t pos         = 0;
				while (!q.empty() && (pos = line.find(q, pos)) != std::string::npos) {
					matches.emplace_back(y, pos);
					pos += q.size();
				}
			}
		}
		if (matches.empty()) {
			ctx.editor.SetSearchMatch(0, 0, 0);
			// Restore to origin if available
			if (ctx.editor.SearchOriginSet()) {
				buf->SetCursor(ctx.editor.SearchOrigX(), ctx.editor.SearchOrigY());
				buf->SetOffsets(ctx.editor.SearchOrigRowoffs(), ctx.editor.SearchOrigColoffs());
			}
			ctx.editor.SetSearchIndex(-1);
			ctx.editor.SetStatus("Find: " + q);
		} else {
			int idx = ctx.editor.SearchIndex();
			if (idx < 0 || idx >= static_cast<int>(matches.size()))
				idx = 0;
			auto [y, x] = matches[static_cast<std::size_t>(idx)];
			ctx.editor.SetSearchMatch(y, x, q.size());
			buf->SetCursor(x, y);
			ensure_cursor_visible(ctx.editor, *buf);
			char tmp[64];
			snprintf(tmp, sizeof(tmp), "%d/%zu", idx + 1, matches.size());
			ctx.editor.SetStatus(std::string("Find: ") + q + "  " + tmp);
			ctx.editor.SetSearchIndex(idx);
		}
		return true;
	}
	// Disallow newlines in InsertText; they should come via Newline
	if (ctx.arg.find('\n') != std::string::npos || ctx.arg.find('\r') != std::string::npos) {
		ctx.editor.SetStatus("InsertText arg must not contain newlines");
		return false;
	}
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	if (y >= rows.size()) {
		rows.resize(y + 1);
	}
	int repeat = ctx.count > 0 ? ctx.count : 1;
	for (int i = 0; i < repeat; ++i) {
		rows[y].insert(x, ctx.arg);
		x += ctx.arg.size();
	}
	buf->SetDirty(true);
	// Record undo after buffer modification but before cursor update
	if (auto *u = buf->Undo()) {
		u->Begin(UndoType::Insert);
		for (int i = 0; i < repeat; ++i) {
			u->Append(std::string_view(ctx.arg));
		}
	}
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_newline(CommandContext &ctx)
{
	// If a prompt is active, accept it and perform the associated action
	if (ctx.editor.PromptActive()) {
		Editor::PromptKind kind = ctx.editor.CurrentPromptKind();
		std::string value       = ctx.editor.PromptText();
		ctx.editor.AcceptPrompt();
		if (kind == Editor::PromptKind::Search) {
			// Finish search: keep cursor where it is, clear search UI prompt
			ctx.editor.SetSearchActive(false);
			ctx.editor.SetSearchMatch(0, 0, 0);
			ctx.editor.ClearSearchOrigin();
			ctx.editor.SetStatus("Find done");
			Buffer *b = ctx.editor.CurrentBuffer();
			if (b)
				ensure_cursor_visible(ctx.editor, *b);
		} else if (kind == Editor::PromptKind::OpenFile) {
			std::string err;
			if (value.empty()) {
				ctx.editor.SetStatus("Open canceled (empty)");
			} else if (!ctx.editor.OpenFile(value, err)) {
				ctx.editor.SetStatus(err.empty() ? std::string("Failed to open ") + value : err);
			} else {
				ctx.editor.SetStatus(std::string("Opened ") + value);
			}
		} else if (kind == Editor::PromptKind::BufferSwitch) {
			// Resolve to a buffer index by exact match against path or basename;
			// if multiple partial matches, prefer exact; if none, keep status.
			const auto &bs = ctx.editor.Buffers();
			std::vector<std::size_t> matches;
			for (std::size_t i = 0; i < bs.size(); ++i) {
				if (value == buffer_display_name(bs[i]) || value == buffer_basename(bs[i])) {
					matches.push_back(i);
				}
			}
			if (matches.empty()) {
				// Try prefix match if no exact
				for (std::size_t i = 0; i < bs.size(); ++i) {
					const std::string full = buffer_display_name(bs[i]);
					const std::string base = buffer_basename(bs[i]);
					if ((!value.empty() && full.rfind(value, 0) == 0) || (
						    !value.empty() && base.rfind(value, 0) == 0)) {
						matches.push_back(i);
					}
				}
			}
			if (matches.empty()) {
				ctx.editor.SetStatus("No such buffer: " + value);
			} else {
				ctx.editor.SwitchTo(matches[0]);
				const Buffer *cur = ctx.editor.CurrentBuffer();
				ctx.editor.SetStatus(std::string("Switched: ")
				                     + (cur ? buffer_display_name(*cur) : std::string("")));
			}
		} else if (kind == Editor::PromptKind::SaveAs) {
			if (value.empty()) {
				ctx.editor.SetStatus("Save canceled (empty filename)");
			} else {
				Buffer *buf = ctx.editor.CurrentBuffer();
				if (!buf) {
					ctx.editor.SetStatus("No buffer to save");
				} else {
					std::string err;
					if (!buf->SaveAs(value, err)) {
						ctx.editor.SetStatus(err);
					} else {
						buf->SetDirty(false);
						ctx.editor.SetStatus("Saved as " + value);
						if (auto *u = buf->Undo())
							u->mark_saved();
					}
				}
			}
		}
		return true;
	}
	// In search mode, Enter accepts the current match and exits search
	if (ctx.editor.SearchActive()) {
		ctx.editor.SetSearchActive(false);
		ctx.editor.SetSearchMatch(0, 0, 0);
		ctx.editor.ClearSearchOrigin();
		ctx.editor.SetStatus("Find done");
		Buffer *buf = ctx.editor.CurrentBuffer();
		if (buf)
			ensure_cursor_visible(ctx.editor, *buf);
		return true;
	}
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to edit");
		return false;
	}
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	for (int i = 0; i < repeat; ++i) {
		if (y >= rows.size())
			rows.resize(y + 1);
		auto &line = rows[y];
		std::string tail;
		if (x < line.size()) {
			tail = line.substr(x);
			line.erase(x);
		}
		rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(y + 1), tail);
		y += 1;
		x = 0;
	}
	buf->SetCursor(x, y);
	buf->SetDirty(true);
	// Record newline after buffer modification; commit immediately for single-step undo
	if (auto *u = buf->Undo()) {
		u->Begin(UndoType::Newline);
		u->commit();
	}
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_backspace(CommandContext &ctx)
{
	// If a prompt is active, backspace edits the prompt text
	if (ctx.editor.PromptActive()) {
		ctx.editor.BackspacePromptText();
		if (ctx.editor.CurrentPromptKind() == Editor::PromptKind::Search) {
			Buffer *buf2 = ctx.editor.CurrentBuffer();
			if (buf2) {
				ctx.editor.SetSearchQuery(ctx.editor.PromptText());
				auto matches = search_compute_matches(*buf2, ctx.editor.SearchQuery());
				search_apply_match(ctx.editor, *buf2, matches);
			}
		} else {
			ctx.editor.SetStatus(ctx.editor.PromptLabel() + ": " + ctx.editor.PromptText());
		}
		return true;
	}
	// In search mode, backspace edits the query
	if (ctx.editor.SearchActive()) {
		if (!ctx.editor.SearchQuery().empty()) {
			std::string q = ctx.editor.SearchQuery();
			q.pop_back();
			ctx.editor.SetSearchQuery(q);
		}
		Buffer *buf2 = ctx.editor.CurrentBuffer();
		if (buf2) {
			auto matches = search_compute_matches(*buf2, ctx.editor.SearchQuery());
			search_apply_match(ctx.editor, *buf2, matches);
		}
		return true;
	}
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to edit");
		return false;
	}
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	UndoSystem *u = buf->Undo();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	for (int i = 0; i < repeat; ++i) {
		if (x > 0) {
			// Delete character before cursor
			char deleted = rows[y][x - 1];
			rows[y].erase(x - 1, 1);
			--x;
			// Record undo after deletion and cursor update
			if (u) {
				u->Begin(UndoType::Delete);
				u->Append(deleted);
			}
		} else if (y > 0) {
			// join with previous line
			std::size_t prev_len = rows[y - 1].size();
			rows[y - 1] += rows[y];
			rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(y));
			y = y - 1;
			x = prev_len;
			// Record a newline deletion that joined lines; commit immediately
			if (u) {
				u->Begin(UndoType::Newline);
				u->commit();
			}
		} else {
			// at very start; nothing to do
			break;
		}
	}
	buf->SetCursor(x, y);
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_delete_char(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to edit");
		return false;
	}
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	UndoSystem *u = buf->Undo();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	for (int i = 0; i < repeat; ++i) {
		if (y >= rows.size())
			break;
		if (x < rows[y].size()) {
			// Forward delete at cursor
			char deleted = rows[y][x];
			rows[y].erase(x, 1);
			// Record undo after deletion (cursor stays at same position)
			if (u) {
				u->Begin(UndoType::Delete);
				u->Append(deleted);
			}
		} else if (y + 1 < rows.size()) {
			// join next line
			rows[y] += rows[y + 1];
			rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(y + 1));
			// Record newline deletion at end of this line; commit immediately
			if (u) {
				u->Begin(UndoType::Newline);
				u->commit();
			}
		} else {
			break;
		}
	}
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


// --- Undo/Redo ---
static bool
cmd_undo(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo()) {
		// Ensure pending batch is finalized so it can be undone
		u->commit();
		u->undo();
		// Keep cursor within buffer bounds
		ensure_cursor_visible(ctx.editor, *buf);
		ctx.editor.SetStatus("Undone");
		return true;
	}
	return false;
}


static bool
cmd_redo(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo()) {
		// Finalize any pending batch before redoing
		u->commit();
		u->redo();
		ensure_cursor_visible(ctx.editor, *buf);
		ctx.editor.SetStatus("Redone");
		return true;
	}
	return false;
}


static bool
cmd_kill_to_eol(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to edit");
		return false;
	}
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	std::string killed_total;
	for (int i = 0; i < repeat; ++i) {
		if (y >= rows.size())
			break;
		if (x < rows[y].size()) {
			// delete from cursor to end of line
			killed_total += rows[y].substr(x);
			rows[y].erase(x);
		} else if (y + 1 < rows.size()) {
			// at EOL: delete the newline (join with next line)
			killed_total += "\n";
			rows[y] += rows[y + 1];
			rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(y + 1));
		} else {
			// nothing to delete
			break;
		}
	}
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
	if (!killed_total.empty()) {
		if (ctx.editor.KillChain())
			ctx.editor.KillRingAppend(killed_total);
		else
			ctx.editor.KillRingPush(killed_total);
		ctx.editor.SetKillChain(true);
	}
	return true;
}


static bool
cmd_kill_line(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to edit");
		return false;
	}
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	(void) x; // cursor x will be reset to 0
	int repeat = ctx.count > 0 ? ctx.count : 1;
	std::string killed_total;
	for (int i = 0; i < repeat; ++i) {
		if (rows.empty())
			break;
		if (rows.size() == 1) {
			// last remaining line: clear its contents
			killed_total += rows[0];
			rows[0].Clear();
			y = 0;
		} else if (y < rows.size()) {
			// erase current line; keep y pointing at the next line
			killed_total += rows[y];
			killed_total += "\n";
			rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(y));
			if (y >= rows.size()) {
				// deleted last line; move to previous
				y = rows.size() - 1;
			}
		} else {
			// out of range
			y = rows.empty() ? 0 : rows.size() - 1;
		}
	}
	buf->SetCursor(0, y);
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
	if (!killed_total.empty()) {
		if (ctx.editor.KillChain())
			ctx.editor.KillRingAppend(killed_total);
		else
			ctx.editor.KillRingPush(killed_total);
		ctx.editor.SetKillChain(true);
	}
	return true;
}


static bool
cmd_yank(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to edit");
		return false;
	}
	std::string text = ctx.editor.KillRingHead();
	if (text.empty()) {
		ctx.editor.SetStatus("Kill ring is empty");
		return false;
	}
	ensure_at_least_one_line(*buf);
	int repeat = ctx.count > 0 ? ctx.count : 1;
	for (int i = 0; i < repeat; ++i) {
		insert_text_at_cursor(*buf, text);
	}
	ensure_cursor_visible(ctx.editor, *buf);
	// Start a new kill chain only from kill commands; yanking should break it
	ctx.editor.SetKillChain(false);
	return true;
}


// --- Marks/Regions and File boundaries ---
static bool
cmd_move_file_start(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	ensure_at_least_one_line(*buf);
	buf->SetCursor(0, 0);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_move_file_end(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = rows.empty() ? 0 : rows.size() - 1;
	std::size_t x = rows.empty() ? 0 : rows[y].size();
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_toggle_mark(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (buf->MarkSet()) {
		buf->ClearMark();
		ctx.editor.SetStatus("Mark cleared");
	} else {
		buf->SetMark(buf->Curx(), buf->Cury());
		ctx.editor.SetStatus("Mark set");
	}
	return true;
}


static bool
cmd_jump_to_mark(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (!buf->MarkSet()) {
		ctx.editor.SetStatus("Mark not set");
		return false;
	}
	std::size_t cx = buf->Curx();
	std::size_t cy = buf->Cury();
	std::size_t mx = buf->MarkCurx();
	std::size_t my = buf->MarkCury();
	buf->SetCursor(mx, my);
	buf->SetMark(cx, cy);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_kill_region(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	ensure_at_least_one_line(*buf);
	std::size_t sx, sy, ex, ey;
	if (!compute_mark_region(*buf, sx, sy, ex, ey)) {
		ctx.editor.SetStatus("No region to kill");
		return false;
	}
	std::string text = extract_region_text(*buf, sx, sy, ex, ey);
	delete_region(*buf, sx, sy, ex, ey);
	ensure_cursor_visible(ctx.editor, *buf);
	if (!text.empty()) {
		if (ctx.editor.KillChain())
			ctx.editor.KillRingAppend(text);
		else
			ctx.editor.KillRingPush(text);
		ctx.editor.SetKillChain(true);
	}

	buf->ClearMark();
	return true;
}


static bool
cmd_copy_region(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	ensure_at_least_one_line(*buf);
	std::size_t sx, sy, ex, ey;
	if (!compute_mark_region(*buf, sx, sy, ex, ey)) {
		ctx.editor.SetStatus("No region to copy");
		return false;
	}
	std::string text = extract_region_text(*buf, sx, sy, ex, ey);
	if (!text.empty()) {
		if (ctx.editor.KillChain())
			ctx.editor.KillRingAppend(text);
		else
			ctx.editor.KillRingPush(text);
		ctx.editor.SetKillChain(true);
	}

	buf->ClearMark();
	return true;
}


static bool
cmd_flush_kill_ring(CommandContext &ctx)
{
	ctx.editor.KillRingClear();
	ctx.editor.SetKillChain(false);
	ctx.editor.SetStatus("Kill ring cleared");
	return true;
}


// --- Navigation ---
// (helper removed)

static bool
cmd_move_left(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	// If a prompt is active and it's search, go to previous match
	if (ctx.editor.PromptActive() && ctx.editor.CurrentPromptKind() == Editor::PromptKind::Search) {
		auto matches = search_compute_matches(*buf, ctx.editor.SearchQuery());
		if (!matches.empty()) {
			int idx = ctx.editor.SearchIndex();
			if (idx < 0)
				idx = 0;
			idx = (idx - 1 + static_cast<int>(matches.size())) % static_cast<int>(matches.size());
			ctx.editor.SetSearchIndex(idx);
			search_apply_match(ctx.editor, *buf, matches);
		} else {
			search_apply_match(ctx.editor, *buf, matches);
		}
		return true;
	}
	if (ctx.editor.SearchActive()) {
		auto matches = search_compute_matches(*buf, ctx.editor.SearchQuery());
		if (!matches.empty()) {
			int idx = ctx.editor.SearchIndex();
			if (idx < 0)
				idx = 0;
			idx = (idx - 1 + static_cast<int>(matches.size())) % static_cast<int>(matches.size());
			ctx.editor.SetSearchIndex(idx);
			search_apply_match(ctx.editor, *buf, matches);
		} else {
			search_apply_match(ctx.editor, *buf, matches);
		}
		return true;
	}
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	while (repeat-- > 0) {
		if (x > 0) {
			--x;
		} else if (y > 0) {
			--y;
			x = rows[y].size();
		}
	}
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_move_right(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	if (ctx.editor.PromptActive() && ctx.editor.CurrentPromptKind() == Editor::PromptKind::Search) {
		auto matches = search_compute_matches(*buf, ctx.editor.SearchQuery());
		if (!matches.empty()) {
			int idx = ctx.editor.SearchIndex();
			if (idx < 0)
				idx = 0;
			idx = (idx + 1) % static_cast<int>(matches.size());
			ctx.editor.SetSearchIndex(idx);
			search_apply_match(ctx.editor, *buf, matches);
		} else {
			search_apply_match(ctx.editor, *buf, matches);
		}
		return true;
	}
	if (ctx.editor.SearchActive()) {
		auto matches = search_compute_matches(*buf, ctx.editor.SearchQuery());
		if (!matches.empty()) {
			int idx = ctx.editor.SearchIndex();
			if (idx < 0)
				idx = 0;
			idx = (idx + 1) % static_cast<int>(matches.size());
			ctx.editor.SetSearchIndex(idx);
			search_apply_match(ctx.editor, *buf, matches);
		} else {
			search_apply_match(ctx.editor, *buf, matches);
		}
		return true;
	}
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	while (repeat-- > 0) {
		if (y < rows.size() && x < rows[y].size()) {
			++x;
		} else if (y + 1 < rows.size()) {
			++y;
			x = 0;
		}
	}
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_move_up(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	if ((ctx.editor.PromptActive() && ctx.editor.CurrentPromptKind() == Editor::PromptKind::Search) || ctx.editor.
	    SearchActive()) {
		// Up == previous match
		auto matches = search_compute_matches(*buf, ctx.editor.SearchQuery());
		if (!matches.empty()) {
			int idx = ctx.editor.SearchIndex();
			if (idx < 0)
				idx = 0;
			idx = (idx - 1 + static_cast<int>(matches.size())) % static_cast<int>(matches.size());
			ctx.editor.SetSearchIndex(idx);
			search_apply_match(ctx.editor, *buf, matches);
		} else {
			search_apply_match(ctx.editor, *buf, matches);
		}
		return true;
	}
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	if (repeat > static_cast<int>(y))
		repeat = static_cast<int>(y);
	y -= static_cast<std::size_t>(repeat);
	if (x > rows[y].size())
		x = rows[y].size();
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_move_down(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	if ((ctx.editor.PromptActive() && ctx.editor.CurrentPromptKind() == Editor::PromptKind::Search) || ctx.editor.
	    SearchActive()) {
		// Down == next match
		auto matches = search_compute_matches(*buf, ctx.editor.SearchQuery());
		if (!matches.empty()) {
			int idx = ctx.editor.SearchIndex();
			if (idx < 0)
				idx = 0;
			idx = (idx + 1) % static_cast<int>(matches.size());
			ctx.editor.SetSearchIndex(idx);
			search_apply_match(ctx.editor, *buf, matches);
		} else {
			search_apply_match(ctx.editor, *buf, matches);
		}
		return true;
	}
	ensure_at_least_one_line(*buf);
	auto &rows           = buf->Rows();
	std::size_t y        = buf->Cury();
	std::size_t x        = buf->Curx();
	int repeat           = ctx.count > 0 ? ctx.count : 1;
	std::size_t max_down = rows.size() - 1 - y;
	if (repeat > static_cast<int>(max_down))
		repeat = static_cast<int>(max_down);
	y += static_cast<std::size_t>(repeat);
	if (x > rows[y].size())
		x = rows[y].size();
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_move_home(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	std::size_t y = buf->Cury();
	buf->SetCursor(0, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_move_end(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = (y < rows.size()) ? rows[y].size() : 0;
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_page_up(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	auto &rows               = buf->Rows();
	int repeat               = ctx.count > 0 ? ctx.count : 1;
	std::size_t content_rows = ctx.editor.Rows() > 0 ? ctx.editor.Rows() - 1 : 0;
	if (content_rows == 0)
		content_rows = 1;

	// Base on current top-of-screen (row offset)
	std::size_t rowoffs = buf->Rowoffs();
	while (repeat-- > 0) {
		if (rowoffs >= content_rows)
			rowoffs -= content_rows;
		else
			rowoffs = 0;
	}
	// Clamp to valid range
	if (rows.size() > content_rows) {
		std::size_t max_top = rows.size() - content_rows;
		if (rowoffs > max_top)
			rowoffs = max_top;
	} else {
		rowoffs = 0;
	}
	// Move cursor to first visible line, column 0
	std::size_t y = rowoffs;
	if (y >= rows.size())
		y = rows.empty() ? 0 : rows.size() - 1;
	buf->SetOffsets(rowoffs, 0);
	buf->SetCursor(0, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_page_down(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	auto &rows               = buf->Rows();
	int repeat               = ctx.count > 0 ? ctx.count : 1;
	std::size_t content_rows = ctx.editor.Rows() > 0 ? ctx.editor.Rows() - 1 : 0;
	if (content_rows == 0)
		content_rows = 1;

	std::size_t rowoffs = buf->Rowoffs();
	// Compute maximum top offset
	std::size_t max_top = 0;
	if (!rows.empty()) {
		if (rows.size() > content_rows)
			max_top = rows.size() - content_rows;
		else
			max_top = 0;
	}
	while (repeat-- > 0) {
		if (rowoffs + content_rows <= max_top)
			rowoffs += content_rows;
		else
			rowoffs = max_top;
	}
	// Move cursor to first visible line, column 0
	std::size_t y = std::min<std::size_t>(rowoffs, rows.empty() ? 0 : rows.size() - 1);
	buf->SetOffsets(rowoffs, 0);
	buf->SetCursor(0, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static inline bool
is_word_char(unsigned char c)
{
	return std::isalnum(c) || c == '_';
}


static bool
cmd_word_prev(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	while (repeat-- > 0) {
		if (y >= rows.size()) {
			y = rows.empty() ? 0 : rows.size() - 1;
			x = rows[y].size();
		}
		// If at start of line and not first line, move to end of previous line
		if (x == 0) {
			if (y == 0)
				break;
			--y;
			x = rows[y].size();
		}
		// Move left one first
		if (x > 0)
			--x;
		// Skip any whitespace leftwards
		while (y < rows.size() && (x > 0 || (x == 0 && y > 0))) {
			if (x == 0) {
				--y;
				x = rows[y].size();
				if (x == 0)
					continue;
			}
			unsigned char c = x > 0 ? static_cast<unsigned char>(rows[y][x - 1]) : 0;
			if (!std::isspace(c))
				break;
			--x;
		}
		// Skip word characters leftwards
		while (y < rows.size() && (x > 0 || (x == 0 && y > 0))) {
			if (x == 0)
				break;
			unsigned char c = static_cast<unsigned char>(rows[y][x - 1]);
			if (!is_word_char(c))
				break;
			--x;
		}
	}
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_word_next(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	while (repeat-- > 0) {
		if (y >= rows.size())
			break;
		// First, if currently on a word, skip to its end
		while (y < rows.size()) {
			if (x < rows[y].size() && is_word_char(static_cast<unsigned char>(rows[y][x]))) {
				++x;
				continue;
			}
			if (x >= rows[y].size()) {
				if (y + 1 >= rows.size())
					break;
				++y;
				x = 0;
				continue;
			}
			break;
		}
		// Then, skip any non-word characters (including punctuation and whitespace)
		while (y < rows.size()) {
			if (x < rows[y].size()) {
				unsigned char c = static_cast<unsigned char>(rows[y][x]);
				if (is_word_char(c))
					break;
				++x;
				continue;
			}
			if (x >= rows[y].size()) {
				if (y + 1 >= rows.size())
					break;
				++y;
				x = 0;
				continue;
			}
		}
	}
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_delete_word_prev(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	std::string killed_total;
	for (int i = 0; i < repeat; ++i) {
		if (y >= rows.size()) {
			y = rows.empty() ? 0 : rows.size() - 1;
			x = rows[y].size();
		}
		std::size_t start_y = y;
		std::size_t start_x = x;
		// If at start of line and not first line, move to end of previous line
		if (x == 0) {
			if (y == 0)
				break;
			--y;
			x = rows[y].size();
		}
		// Move left one first
		if (x > 0)
			--x;
		// Skip any whitespace leftwards
		while (y < rows.size() && (x > 0 || (x == 0 && y > 0))) {
			if (x == 0) {
				--y;
				x = rows[y].size();
				if (x == 0)
					continue;
			}
			unsigned char c = x > 0 ? static_cast<unsigned char>(rows[y][x - 1]) : 0;
			if (!std::isspace(c))
				break;
			--x;
		}
		// Skip word characters leftwards
		while (y < rows.size() && (x > 0 || (x == 0 && y > 0))) {
			if (x == 0)
				break;
			unsigned char c = static_cast<unsigned char>(rows[y][x - 1]);
			if (!is_word_char(c))
				break;
			--x;
		}
		// Now delete from (x, y) to (start_x, start_y)
		std::string deleted;
		if (y == start_y) {
			// same line
			if (x < start_x) {
				deleted = rows[y].substr(x, start_x - x);
				rows[y].erase(x, start_x - x);
			}
		} else {
			// spans multiple lines
			// First, collect text from (x, y) to end of line y
			deleted = rows[y].substr(x);
			rows[y].erase(x);
			// Then collect complete lines between y and start_y
			for (std::size_t ly = y + 1; ly < start_y; ++ly) {
				deleted += "\n";
				deleted += rows[ly];
			}
			// Finally, collect from beginning of start_y to start_x
			if (start_y < rows.size()) {
				deleted += "\n";
				deleted += rows[start_y].substr(0, start_x);
				rows[y] += rows[start_y].substr(start_x);
				// Remove lines from y+1 to start_y inclusive
				rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(y + 1),
				           rows.begin() + static_cast<std::ptrdiff_t>(start_y + 1));
			}
		}
		// Prepend to killed_total (since we're deleting backwards)
		killed_total = deleted + killed_total;
	}
	buf->SetCursor(x, y);
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
	if (!killed_total.empty()) {
		if (ctx.editor.KillChain())
			ctx.editor.KillRingAppend(killed_total);
		else
			ctx.editor.KillRingPush(killed_total);
		ctx.editor.SetKillChain(true);
	}
	return true;
}


static bool
cmd_delete_word_next(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	std::string killed_total;
	for (int i = 0; i < repeat; ++i) {
		if (y >= rows.size())
			break;
		std::size_t start_y = y;
		std::size_t start_x = x;
		// First, if currently on a word, skip to its end
		while (y < rows.size()) {
			if (x < rows[y].size() && is_word_char(static_cast<unsigned char>(rows[y][x]))) {
				++x;
				continue;
			}
			if (x >= rows[y].size()) {
				if (y + 1 >= rows.size())
					break;
				++y;
				x = 0;
				continue;
			}
			break;
		}
		// Then, skip any non-word characters (including punctuation and whitespace)
		while (y < rows.size()) {
			if (x < rows[y].size()) {
				unsigned char c = static_cast<unsigned char>(rows[y][x]);
				if (is_word_char(c))
					break;
				++x;
				continue;
			}
			if (x >= rows[y].size()) {
				if (y + 1 >= rows.size())
					break;
				++y;
				x = 0;
				continue;
			}
		}
		// Now delete from (start_x, start_y) to (x, y)
		std::string deleted;
		if (start_y == y) {
			// same line
			if (start_x < x) {
				deleted = rows[y].substr(start_x, x - start_x);
				rows[y].erase(start_x, x - start_x);
				x = start_x;
			}
		} else {
			// spans multiple lines
			// First, collect text from start_x to end of line start_y
			deleted = rows[start_y].substr(start_x);
			rows[start_y].erase(start_x);
			// Then collect complete lines between start_y and y
			for (std::size_t ly = start_y + 1; ly < y; ++ly) {
				deleted += "\n";
				deleted += rows[ly];
			}
			// Finally, collect from beginning of y to x
			if (y < rows.size()) {
				deleted += "\n";
				deleted += rows[y].substr(0, x);
				rows[start_y] += rows[y].substr(x);
				// Remove lines from start_y+1 to y inclusive
				rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(start_y + 1),
				           rows.begin() + static_cast<std::ptrdiff_t>(y + 1));
			}
			y = start_y;
			x = start_x;
		}
		killed_total += deleted;
	}
	buf->SetCursor(x, y);
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
	if (!killed_total.empty()) {
		if (ctx.editor.KillChain())
			ctx.editor.KillRingAppend(killed_total);
		else
			ctx.editor.KillRingPush(killed_total);
		ctx.editor.SetKillChain(true);
	}
	return true;
}


static bool
cmd_indent_region(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (!buf->MarkSet()) {
		ctx.editor.SetStatus("No mark set");
		return false;
	}
	std::size_t sx, sy, ex, ey;
	if (!compute_mark_region(*buf, sx, sy, ex, ey)) {
		ctx.editor.SetStatus("No region to indent");
		return false;
	}
	auto &rows = buf->Rows();
	for (std::size_t y = sy; y <= ey && y < rows.size(); ++y) {
		rows[y].insert(0, "\t");
	}
	buf->SetDirty(true);
	buf->ClearMark();
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_unindent_region(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (!buf->MarkSet()) {
		ctx.editor.SetStatus("No mark set");
		return false;
	}
	std::size_t sx, sy, ex, ey;
	if (!compute_mark_region(*buf, sx, sy, ex, ey)) {
		ctx.editor.SetStatus("No region to unindent");
		return false;
	}
	auto &rows = buf->Rows();
	for (std::size_t y = sy; y <= ey && y < rows.size(); ++y) {
		auto &line = rows[y];
		if (!line.empty()) {
			if (line[0] == '\t') {
				line.erase(0, 1);
			} else if (line[0] == ' ') {
				std::size_t spaces = 0;
				while (spaces < line.size() && spaces < 8 && line[spaces] == ' ') {
					++spaces;
				}
				if (spaces > 0)
					line.erase(0, spaces);
			}
		}
	}
	buf->SetDirty(true);
	buf->ClearMark();
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_reflow_paragraph(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	ensure_at_least_one_line(*buf);
	auto &rows             = buf->Rows();
	std::size_t y          = buf->Cury();
	int width              = ctx.count > 0 ? ctx.count : 72;
	std::size_t para_start = y;
	while (para_start > 0 && !rows[para_start - 1].empty())
		--para_start;
	std::size_t para_end = y;
	while (para_end + 1 < rows.size() && !rows[para_end + 1].empty())
		++para_end;
	if (para_start > para_end)
		return false;
	std::string text;
	for (std::size_t i = para_start; i <= para_end; ++i) {
		if (i > para_start && !text.empty() && text.back() != ' ')
			text += ' ';
		const auto &line = rows[i];
		for (std::size_t j = 0; j < line.size(); ++j) {
			char c = line[j];
			if (c == '\t')
				text += ' ';
			else
				text += c;
		}
	}
	std::vector<std::string> new_lines;
	std::string line;
	std::size_t pos = 0;
	while (pos < text.size()) {
		while (pos < text.size() && text[pos] == ' ')
			++pos;
		if (pos >= text.size())
			break;
		std::size_t word_start = pos;
		while (pos < text.size() && text[pos] != ' ')
			++pos;
		std::string word = text.substr(word_start, pos - word_start);
		if (line.empty()) {
			line = word;
		} else if (static_cast<int>(line.size() + 1 + word.size()) <= width) {
			line += ' ';
			line += word;
		} else {
			new_lines.push_back(line);
			line = word;
		}
	}
	if (!line.empty())
		new_lines.push_back(line);
	if (new_lines.empty())
		new_lines.push_back("");
	rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(para_start),
	           rows.begin() + static_cast<std::ptrdiff_t>(para_end + 1));
	rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(para_start),
	            new_lines.begin(), new_lines.end());
	buf->SetCursor(0, para_start);
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_reload_buffer(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	const std::string &filename = buf->Filename();
	if (filename.empty()) {
		ctx.editor.SetStatus("Cannot reload unnamed buffer");
		return false;
	}
	std::string err;
	if (!buf->OpenFromFile(filename, err)) {
		ctx.editor.SetStatus(std::string("Reload failed: ") + err);
		return false;
	}
	ctx.editor.SetStatus(std::string("Reloaded ") + filename);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_mark_all_and_jump_end(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	ensure_at_least_one_line(*buf);
	buf->SetMark(0, 0);
	auto &rows         = buf->Rows();
	std::size_t last_y = rows.empty() ? 0 : rows.size() - 1;
	std::size_t last_x = last_y < rows.size() ? rows[last_y].size() : 0;
	buf->SetCursor(last_x, last_y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


std::vector<Command> &
CommandRegistry::storage_()
{
	static std::vector<Command> cmds;
	return cmds;
}


void
CommandRegistry::Register(const Command &cmd)
{
	auto &v = storage_();
	// Replace existing with same id or name
	auto it = std::find_if(v.begin(), v.end(), [&](const Command &c) {
		return c.id == cmd.id || c.name == cmd.name;
	});
	if (it != v.end()) {
		*it = cmd;
	} else {
		v.push_back(cmd);
	}
}


const Command *
CommandRegistry::FindById(CommandId id)
{
	auto &v = storage_();
	auto it = std::find_if(v.begin(), v.end(), [&](const Command &c) {
		return c.id == id;
	});
	return it == v.end() ? nullptr : &*it;
}


const Command *
CommandRegistry::FindByName(const std::string &name)
{
	auto &v = storage_();
	auto it = std::find_if(v.begin(), v.end(), [&](const Command &c) {
		return c.name == name;
	});
	return it == v.end() ? nullptr : &*it;
}


const std::vector<Command> &
CommandRegistry::All()
{
	return storage_();
}


void
InstallDefaultCommands()
{
	CommandRegistry::Register({CommandId::Save, "save", "Save current buffer", cmd_save});
	CommandRegistry::Register({CommandId::SaveAs, "save-as", "Save current buffer as...", cmd_save_as});
	CommandRegistry::Register({CommandId::Quit, "quit", "Quit editor (request)", cmd_quit});
	CommandRegistry::Register({CommandId::QuitNow, "quit-now", "Quit editor immediately", cmd_quit_now});
	CommandRegistry::Register({CommandId::SaveAndQuit, "save-quit", "Save and quit (request)", cmd_save_and_quit});
	CommandRegistry::Register({CommandId::Refresh, "refresh", "Force redraw", cmd_refresh});
	CommandRegistry::Register(
		{CommandId::KPrefix, "k-prefix", "Entering k-command prefix (show hint)", cmd_kprefix});
	CommandRegistry::Register({
		CommandId::UnknownKCommand, "unknown-k", "Unknown k-command (status)",
		cmd_unknown_kcommand
	});
	CommandRegistry::Register({CommandId::FindStart, "find-start", "Begin incremental search", cmd_find_start});
	CommandRegistry::Register({
		CommandId::OpenFileStart, "open-file-start", "Begin open-file prompt", cmd_open_file_start
	});
	// Buffers
	CommandRegistry::Register({
		CommandId::BufferSwitchStart, "buffer-switch-start", "Begin buffer switch prompt",
		cmd_buffer_switch_start
	});
	CommandRegistry::Register({CommandId::BufferNext, "buffer-next", "Switch to next buffer", cmd_buffer_next});
	CommandRegistry::Register({CommandId::BufferPrev, "buffer-prev", "Switch to previous buffer", cmd_buffer_prev});
	CommandRegistry::Register({CommandId::BufferClose, "buffer-close", "Close current buffer", cmd_buffer_close});
	// Editing
	CommandRegistry::Register({
		CommandId::InsertText, "insert", "Insert text at cursor (no newlines)", cmd_insert_text
	});
	CommandRegistry::Register({CommandId::Newline, "newline", "Insert newline at cursor", cmd_newline});
	CommandRegistry::Register({CommandId::Backspace, "backspace", "Delete char before cursor", cmd_backspace});
	CommandRegistry::Register({CommandId::DeleteChar, "delete-char", "Delete char at cursor", cmd_delete_char});
	CommandRegistry::Register({CommandId::KillToEOL, "kill-to-eol", "Delete to end of line", cmd_kill_to_eol});
	CommandRegistry::Register({CommandId::KillLine, "kill-line", "Delete entire line", cmd_kill_line});
	CommandRegistry::Register({CommandId::Yank, "yank", "Yank from kill ring", cmd_yank});
	// Marks/regions and file boundaries
	CommandRegistry::Register({
		CommandId::MoveFileStart, "file-start", "Move to beginning of file", cmd_move_file_start
	});
	CommandRegistry::Register({CommandId::MoveFileEnd, "file-end", "Move to end of file", cmd_move_file_end});
	CommandRegistry::Register({CommandId::ToggleMark, "toggle-mark", "Toggle mark at cursor", cmd_toggle_mark});
	CommandRegistry::Register({
		CommandId::JumpToMark, "jump-to-mark", "Jump to mark (swap mark)", cmd_jump_to_mark
	});
	CommandRegistry::Register({CommandId::KillRegion, "kill-region", "Kill region to kill ring", cmd_kill_region});
	CommandRegistry::Register({CommandId::CopyRegion, "copy-region", "Copy region to kill ring", cmd_copy_region});
	CommandRegistry::Register({
		CommandId::FlushKillRing, "flush-kill-ring", "Flush kill ring", cmd_flush_kill_ring
	});
	// Navigation
	CommandRegistry::Register({CommandId::MoveLeft, "left", "Move cursor left", cmd_move_left});
	CommandRegistry::Register({CommandId::MoveRight, "right", "Move cursor right", cmd_move_right});
	CommandRegistry::Register({CommandId::MoveUp, "up", "Move cursor up", cmd_move_up});
	CommandRegistry::Register({CommandId::MoveDown, "down", "Move cursor down", cmd_move_down});
	CommandRegistry::Register({CommandId::MoveHome, "home", "Move to beginning of line", cmd_move_home});
	CommandRegistry::Register({CommandId::MoveEnd, "end", "Move to end of line", cmd_move_end});
	CommandRegistry::Register({CommandId::PageUp, "page-up", "Page up", cmd_page_up});
	CommandRegistry::Register({CommandId::PageDown, "page-down", "Page down", cmd_page_down});
	CommandRegistry::Register({CommandId::WordPrev, "word-prev", "Move to previous word", cmd_word_prev});
	CommandRegistry::Register({CommandId::WordNext, "word-next", "Move to next word", cmd_word_next});
	CommandRegistry::Register({
		CommandId::DeleteWordPrev, "delete-word-prev", "Delete previous word", cmd_delete_word_prev
	});
	CommandRegistry::Register({
		CommandId::DeleteWordNext, "delete-word-next", "Delete next word", cmd_delete_word_next
	});
	CommandRegistry::Register({
		CommandId::MoveCursorTo, "move-cursor-to", "Move cursor to y:x", cmd_move_cursor_to
	});
	// Undo/Redo
	CommandRegistry::Register({CommandId::Undo, "undo", "Undo last edit", cmd_undo});
	CommandRegistry::Register({CommandId::Redo, "redo", "Redo edit", cmd_redo});
	// Region formatting
	CommandRegistry::Register({CommandId::IndentRegion, "indent-region", "Indent region", cmd_indent_region});
	CommandRegistry::Register(
		{CommandId::UnindentRegion, "unindent-region", "Unindent region", cmd_unindent_region});
	CommandRegistry::Register({
		CommandId::ReflowParagraph, "reflow-paragraph", "Reflow paragraph to column width", cmd_reflow_paragraph
	});
	// Buffer operations
	CommandRegistry::Register({
		CommandId::ReloadBuffer, "reload-buffer", "Reload buffer from disk", cmd_reload_buffer
	});
	CommandRegistry::Register({
		CommandId::MarkAllAndJumpEnd, "mark-all-jump-end", "Set mark at beginning and jump to end",
		cmd_mark_all_and_jump_end
	});
	// UI helpers
	CommandRegistry::Register(
		{CommandId::UArgStatus, "uarg-status", "Update universal-arg status", cmd_uarg_status});
}


bool
Execute(Editor &ed, CommandId id, const std::string &arg, int count)
{
	const Command *cmd = CommandRegistry::FindById(id);
	if (!cmd)
		return false;
	// If a quit confirmation was pending and the user invoked something other
	// than the soft quit again, cancel the pending confirmation.
	if (ed.QuitConfirmPending() && id != CommandId::Quit && id != CommandId::KPrefix) {
		ed.SetQuitConfirmPending(false);
	}
	// Reset kill chain unless this is a kill-like command (so consecutive kills append)
	if (id != CommandId::KillToEOL && id != CommandId::KillLine && id != CommandId::KillRegion && id !=
	    CommandId::CopyRegion && id != CommandId::DeleteWordPrev && id != CommandId::DeleteWordNext) {
		ed.SetKillChain(false);
	}
	CommandContext ctx{ed, arg, count};
	return cmd->handler ? cmd->handler(ctx) : false;
}


bool
Execute(Editor &ed, const std::string &name, const std::string &arg, int count)
{
	const Command *cmd = CommandRegistry::FindByName(name);
	if (!cmd)
		return false;
	CommandContext ctx{ed, arg, count};
	return cmd->handler ? cmd->handler(ctx) : false;
}
