#include <algorithm>

#include "Command.h"
#include "Editor.h"
#include "Buffer.h"
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


static std::size_t
inverse_render_to_source_col(const std::string &line, std::size_t rx_target, std::size_t tabw)
{
    // Find source column that best matches given rendered x (with tabs expanded)
    std::size_t rx = 0;
    if (rx_target == 0) return 0;
    std::size_t best_col = 0;
    std::size_t best_dist = rx_target; // max
    for (std::size_t i = 0; i <= line.size(); ++i) {
        std::size_t dist = (rx > rx_target) ? (rx - rx_target) : (rx_target - rx);
        if (dist <= best_dist) {
            best_dist = dist;
            best_col = i;
        }
        if (i == line.size()) break;
        if (line[i] == '\t') {
            rx += tabw - (rx % tabw);
        } else {
            rx += 1;
        }
        if (rx >= rx_target && i + 1 <= line.size()) {
            // next iteration will evaluate i+1 with updated rx
        }
    }
    if (best_col > line.size()) best_col = line.size();
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
    std::size_t row = buf->Cury();
    std::size_t col = buf->Curx();
    const std::string &a = ctx.arg;
    if (!a.empty()) {
        bool screen = false;
        std::size_t start = 0;
        if (!a.empty() && a[0] == '@') { screen = true; start = 1; }
        std::size_t p = a.find(':', start);
        if (p != std::string::npos) {
            try {
                long ay = std::stol(a.substr(start, p - start));
                long ax = std::stol(a.substr(p + 1));
                if (ay < 0) ay = 0; if (ax < 0) ax = 0;
                if (screen) {
                    // Translate screen to buffer coordinates using offsets and tab expansion for x
                    std::size_t vy = static_cast<std::size_t>(ay);
                    std::size_t vx = static_cast<std::size_t>(ax);
                    std::size_t bro = buf->Rowoffs();
                    std::size_t bco = buf->Coloffs();
                    std::size_t by = bro + vy;
                    // Clamp by to existing lines later
                    auto &lines2 = buf->Rows();
                    if (lines2.empty()) { lines2.emplace_back(""); }
                    if (by >= lines2.size()) by = lines2.size() - 1;
                    const std::string &line2 = lines2[by];
                    std::size_t rx_target = bco + vx;
                    std::size_t sx = inverse_render_to_source_col(line2, rx_target, 8);
                    row = by;
                    col = sx;
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
    if (row >= lines.size()) row = lines.size() - 1;
    const std::string &line = lines[row];
    if (col > line.size()) col = line.size();
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
		ctx.editor.SetStatus("Buffer is not file-backed; use save-as");
		return false;
	}
	if (!buf->Save(err)) {
		ctx.editor.SetStatus(err);
		return false;
	}
	buf->SetDirty(false);
	ctx.editor.SetStatus("Saved " + buf->Filename());
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
	return true;
}


static bool
cmd_quit(CommandContext &ctx)
{
	// Placeholder: actual app loop should react to this status or a future flag
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
	ctx.editor.SetStatus("Refresh requested");
	return true;
}


static bool
cmd_kprefix(CommandContext &ctx)
{
	// Show k-command mode hint in status
	ctx.editor.SetStatus("C-k _");
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
	buf->SetCursor(x, y);
	buf->SetDirty(true);
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
		} else if (kind == Editor::PromptKind::SaveAs) {
			// Optional: not wired yet
			ctx.editor.SetStatus("Save As not implemented");
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
		std::string &line = rows[y];
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
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	for (int i = 0; i < repeat; ++i) {
		if (x > 0) {
			rows[y].erase(x - 1, 1);
			--x;
		} else if (y > 0) {
			// join with previous line
			std::size_t prev_len = rows[y - 1].size();
			rows[y - 1] += rows[y];
			rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(y));
			y = y - 1;
			x = prev_len;
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
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	for (int i = 0; i < repeat; ++i) {
		if (y >= rows.size())
			break;
		if (x < rows[y].size()) {
			rows[y].erase(x, 1);
		} else if (y + 1 < rows.size()) {
			// join next line
			rows[y] += rows[y + 1];
			rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(y + 1));
		} else {
			break;
		}
	}
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
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
	ensure_at_least_one_line(*buf);
	auto &rows               = buf->Rows();
	std::size_t y            = buf->Cury();
	std::size_t x            = buf->Curx();
	int repeat               = ctx.count > 0 ? ctx.count : 1;
	std::size_t content_rows = ctx.editor.Rows() > 0 ? ctx.editor.Rows() - 1 : 0;
	if (content_rows == 0)
		content_rows = 1;
	while (repeat-- > 0) {
		if (y > content_rows)
			y -= content_rows;
		else
			y = 0;
		if (x > rows[y].size())
			x = rows[y].size();
	}
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_page_down(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	ensure_at_least_one_line(*buf);
	auto &rows               = buf->Rows();
	std::size_t y            = buf->Cury();
	std::size_t x            = buf->Curx();
	int repeat               = ctx.count > 0 ? ctx.count : 1;
	std::size_t content_rows = ctx.editor.Rows() > 0 ? ctx.editor.Rows() - 1 : 0;
	if (content_rows == 0)
		content_rows = 1;
	while (repeat-- > 0) {
		std::size_t max_down = rows.empty() ? 0 : (rows.size() - 1 - y);
		if (content_rows < max_down)
			y += content_rows;
		else
			y += max_down;
		if (x > rows[y].size())
			x = rows[y].size();
	}
	buf->SetCursor(x, y);
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
	ensure_at_least_one_line(*buf);
	auto &rows    = buf->Rows();
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	while (repeat-- > 0) {
		if (y >= rows.size())
			break;
		// Skip whitespace to the right
		while (y < rows.size()) {
			if (y >= rows.size())
				break;
			if (x < rows[y].size() && std::isspace(static_cast<unsigned char>(rows[y][x]))) {
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
		// Skip word characters to the right
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
	}
	buf->SetCursor(x, y);
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
	CommandRegistry::Register({CommandId::SaveAndQuit, "save-quit", "Save and quit (request)", cmd_save_and_quit});
	CommandRegistry::Register({CommandId::Refresh, "refresh", "Force redraw", cmd_refresh});
	CommandRegistry::Register(
		{CommandId::KPrefix, "k-prefix", "Entering k-command prefix (show hint)", cmd_kprefix});
	CommandRegistry::Register({CommandId::FindStart, "find-start", "Begin incremental search", cmd_find_start});
	CommandRegistry::Register({
		CommandId::OpenFileStart, "open-file-start", "Begin open-file prompt", cmd_open_file_start
	});
	// Editing
	CommandRegistry::Register({
		CommandId::InsertText, "insert", "Insert text at cursor (no newlines)", cmd_insert_text
	});
	CommandRegistry::Register({CommandId::Newline, "newline", "Insert newline at cursor", cmd_newline});
	CommandRegistry::Register({CommandId::Backspace, "backspace", "Delete char before cursor", cmd_backspace});
	CommandRegistry::Register({CommandId::DeleteChar, "delete-char", "Delete char at cursor", cmd_delete_char});
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
    CommandRegistry::Register({CommandId::MoveCursorTo, "move-cursor-to", "Move cursor to y:x", cmd_move_cursor_to});
}


bool
Execute(Editor &ed, CommandId id, const std::string &arg, int count)
{
	const Command *cmd = CommandRegistry::FindById(id);
	if (!cmd)
		return false;
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
