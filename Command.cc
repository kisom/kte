#include <algorithm>
#include <map>
#include <cwchar>
#include <filesystem>
#include <cstdlib>
#include <regex>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cctype>
#include <string_view>

#include "Command.h"
#include "ErrorHandler.h"
#include "RegexGuard.h"
#include "RegexEngine.h"
#include "TermWidth.h"
#include "syntax/HighlighterRegistry.h"
#include "syntax/NullHighlighter.h"
#include "Editor.h"
#include "Buffer.h"
#include "UndoSystem.h"
#include "HelpText.h"
#include "syntax/LanguageHighlighter.h"
#include "syntax/HighlighterEngine.h"
#include "syntax/CppHighlighter.h"
#ifdef KTE_BUILD_GUI
#  include "GUITheme.h"
#  if !defined(KTE_USE_QT)
#    include "fonts/FontRegistry.h"
#    include "imgui.h"
#  endif
#  if defined(KTE_USE_QT)
#    include <QFontDatabase>
#    include <QStringList>
#  endif
#endif

// Define cross-frontend theme change flags declared in GUITheme.h
namespace kte {
bool gThemeChangePending = false;
std::string gThemeChangeRequest;
// Qt font change globals
bool gFontChangePending = false;
std::string gFontFamilyRequest;
float gFontSizeRequest = 0.0f;
std::string gCurrentFontFamily;
float gCurrentFontSize = 0.0f;
// Request Qt visual font dialog
bool gFontDialogRequested = false;
}


// UTF-8 aware stepping. Cursor columns are byte offsets; these keep motion
// and deletion on code point boundaries so a multibyte character is never
// split. Malformed bytes are treated as single-byte characters.
static inline bool
utf8_is_cont(const char c)
{
	return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}


// Byte length of the character ending at x (x > 0, x <= line.size()).
static std::size_t
utf8_prev_len(std::string_view line, std::size_t x)
{
	if (x == 0)
		return 0;
	if (x > line.size())
		return 1;
	std::size_t n = 1;
	while (n < 4 && n < x && utf8_is_cont(line[x - n]))
		++n;
	// Accept only if a lead byte starts the run and declares this length.
	const auto lead = static_cast<unsigned char>(line[x - n]);
	std::size_t want = 1;
	if (lead >= 0xF0)
		want = 4;
	else if (lead >= 0xE0)
		want = 3;
	else if (lead >= 0xC0)
		want = 2;
	return (n > 1 && want == n) ? n : 1;
}


// Byte length of the character starting at x (x < line.size()).
static std::size_t
utf8_next_len(std::string_view line, std::size_t x)
{
	if (x >= line.size())
		return 0;
	const auto lead = static_cast<unsigned char>(line[x]);
	std::size_t want = 1;
	if (lead >= 0xF0 && lead < 0xF8)
		want = 4;
	else if (lead >= 0xE0)
		want = 3;
	else if (lead >= 0xC0)
		want = 2;
	if (want == 1 || x + want > line.size())
		return 1;
	for (std::size_t i = 1; i < want; ++i) {
		if (!utf8_is_cont(line[x + i]))
			return 1;
	}
	return want;
}


// Visual-line mode applies an edit at "the cursor's column" on every
// selected line. Columns are byte offsets, so the same number on a line with
// different characters can fall inside a multibyte one: carry the column as
// a character count instead.
static std::size_t
utf8_char_count(std::string_view line, std::size_t x)
{
	x = std::min(x, line.size());
	std::size_t n = 0;
	for (std::size_t i = 0; i < x; i += utf8_next_len(line, i))
		++n;
	return n;
}


static std::size_t
utf8_byte_of_char(std::string_view line, std::size_t nchars)
{
	std::size_t i = 0;
	for (; nchars > 0 && i < line.size(); --nchars)
		i += utf8_next_len(line, i);
	return i;
}


static inline std::string_view
line_view(const std::string &l)
{
	return std::string_view(l);
}


// Keep buffer viewport offsets so that the cursor stays within the visible
// window based on the editor's current dimensions. The bottom row is reserved
// for the status line.
// Decode the character at line[i]: its byte length and display width in cells
// (tabs expand to the next multiple of tabw from column rx). Measures the way
// TerminalRenderer draws: invalid bytes are one cell each.
static void
measure_char(std::string_view line, std::size_t i, std::size_t rx, std::size_t tabw, std::size_t &len,
             std::size_t &width)
{
	std::mbstate_t state{};
	wchar_t wch   = 0;
	const auto rc = std::mbrtowc(&wch, line.data() + i, line.size() - i, &state);
	if (rc == static_cast<std::size_t>(-1) || rc == static_cast<std::size_t>(-2) || rc == 0) {
		// Invalid byte or NUL: the renderer draws the byte value itself.
		wch = static_cast<unsigned char>(line[i]);
		len = 1;
	} else {
		len = rc;
	}
	if (wch == L'\t')
		width = tabw - (rx % tabw);
	else
		width = static_cast<std::size_t>(kte::CellWidth(wch));
}


// Display column of byte offset curx, in terminal cells.
static std::size_t
compute_render_x(std::string_view line, const std::size_t curx, const std::size_t tabw)
{
	std::size_t rx = 0;
	std::size_t i  = 0;
	while (i < curx && i < line.size()) {
		std::size_t len = 1, width = 1;
		measure_char(line, i, rx, tabw, len, width);
		rx += width;
		i += len;
	}
	return rx;
}


// Clamp (x, y) to an existing row and to that row's length.
static void
clamp_position(const Buffer &buf, std::size_t &x, std::size_t &y)
{
	const std::size_t nrows = buf.Nrows();
	if (nrows == 0) {
		x = y = 0;
		return;
	}
	// A row past the end means a position past the end of the buffer: clamp
	// it to the end of the last line, not to its column on that line.
	const bool past_end = y >= nrows;
	if (past_end)
		y = nrows - 1;
	const auto [start, end] = buf.GetLineRange(y);
	std::size_t len         = end - start;
	// GetLineRange includes the trailing newline, if any.
	if (len > 0 && y + 1 < nrows)
		--len;
	x = past_end ? len : std::min(x, len);
}


// Clamp the cursor to an existing row and to that row's length. Commands that
// set the cursor from remembered or scrolled positions must call this: edits
// at a column past end-of-line are misapplied (and mis-recorded for undo).
static void
clamp_cursor_to_buffer(Buffer &buf)
{
	std::size_t x = buf.Curx();
	std::size_t y = buf.Cury();
	clamp_position(buf, x, y);
	buf.SetCursor(x, y);
}


static void
ensure_cursor_visible(const Editor &ed, Buffer &buf)
{
	const std::size_t cols = ed.Cols();
	if (cols == 0)
		return;

	const std::size_t content_rows = ed.ContentRows();
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

	// Clamp vertical offset to available content. Nrows() reads the line index
	// and equals Rows().size(); calling Rows() here would rebuild a string for
	// every line of the file after each edit.
	const auto total_rows = buf.Nrows();
	if (content_rows < total_rows) {
		std::size_t max_rowoffs = total_rows - content_rows;
		if (rowoffs > max_rowoffs)
			rowoffs = max_rowoffs;
	} else {
		rowoffs = 0;
	}

	// Horizontal scrolling (use rendered columns with tabs expanded)
	std::size_t rx = 0;
	if (cury < total_rows) {
		// GetLineView would materialize the whole buffer after an edit; copying
		// the one line is cheaper.
		rx = compute_render_x(buf.GetLineString(cury), curx, 8);
	}
	if (rx < coloffs) {
		coloffs = rx;
	} else if (rx >= coloffs + cols) {
		coloffs = rx - cols + 1;
	}

	buf.SetOffsets(rowoffs, coloffs);
	buf.SetRenderX(rx);
}


static bool
cmd_new_window(CommandContext &ctx)
{
	ctx.editor.SetNewWindowRequested(true);
	return true;
}


static bool
cmd_center_on_cursor(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	std::size_t total   = buf->Nrows();
	std::size_t content = ctx.editor.ContentRows();
	if (content == 0)
		content = 1;
	std::size_t cy          = buf->Cury();
	std::size_t half        = content / 2;
	std::size_t new_rowoffs = (cy > half) ? (cy - half) : 0;
	// Clamp to valid range
	if (total > content) {
		std::size_t max_rowoffs = total - content;
		if (new_rowoffs > max_rowoffs)
			new_rowoffs = max_rowoffs;
	} else {
		new_rowoffs = 0;
	}
	buf->SetOffsets(new_rowoffs, buf->Coloffs());
	return true;
}


static void
ensure_at_least_one_line(Buffer &buf)
{
	if (buf.Nrows() == 0) {
		buf.insert_row(0, "");
		buf.SetDirty(true);
	}
}


// RAII helper: brackets a multi-edit command so its individual undo nodes
// undo/redo as a single atomic step, per the project's undo-grouping convention.
struct UndoGroupGuard {
	UndoSystem *u;


	explicit UndoGroupGuard(UndoSystem *u_) : u(u_)
	{
		if (u)
			u->BeginGroup();
	}


	~UndoGroupGuard()
	{
		if (u)
			u->EndGroup();
	}
};


// Determine if a command mutates the buffer contents (text edits)
static bool
is_mutating_command(CommandId id)
{
	switch (id) {
	case CommandId::InsertText:
	case CommandId::Newline:
	case CommandId::Backspace:
	case CommandId::DeleteChar:
	case CommandId::KillToEOL:
	case CommandId::KillLine:
	case CommandId::Yank:
	case CommandId::DeleteWordPrev:
	case CommandId::DeleteWordNext:
	case CommandId::IndentRegion:
	case CommandId::UnindentRegion:
	case CommandId::ReflowParagraph:
	case CommandId::KillRegion:
	case CommandId::Undo:
	case CommandId::Redo:
		return true;
	default:
		return false;
	}
}


// Non-throwing filesystem queries for command code. The throwing overloads
// fail on ENAMETOOLONG, EACCES or a deleted working directory, which used
// to terminate the editor.
static bool
fs_exists(const std::string &path)
{
	std::error_code ec;
	return std::filesystem::exists(path, ec) && !ec;
}


static std::filesystem::path
safe_current_path()
{
	std::error_code ec;
	auto p = std::filesystem::current_path(ec);
	return ec ? std::filesystem::path(".") : p;
}


// True if `path` is open in a buffer other than the current one. Saving the
// current buffer there would leave two buffers for one file (sharing, and
// deleting each other's, swap journal).
static bool
open_in_other_buffer(const Editor &ed, const std::string &path)
{
	const std::size_t idx = ed.FindOpenBuffer(path);
	return idx != static_cast<std::size_t>(-1) && idx != ed.CurrentBufferIndex();
}


// True if `path` names the file `buf` is backed by (same inode).
static bool
is_buffers_own_file(const Buffer &buf, const std::string &path)
{
	if (!buf.IsFileBacked() || buf.Filename().empty())
		return false;
	try {
		return std::filesystem::equivalent(path, buf.Filename());
	} catch (...) {
		return false;
	}
}


// Commands that may run while a prompt is open: prompt editing and
// navigation (their handlers check PromptActive), cancel, and view-only
// commands that do not touch buffer contents or buffer selection.
static bool
allowed_during_prompt(CommandId id)
{
	switch (id) {
	case CommandId::Refresh:
	case CommandId::InsertText:
	case CommandId::Newline:
	case CommandId::SmartNewline:
	case CommandId::Backspace:
	case CommandId::MoveLeft:
	case CommandId::MoveRight:
	case CommandId::MoveUp:
	case CommandId::MoveDown:
	case CommandId::UArgStatus:
	case CommandId::KPrefix:
	case CommandId::UnknownKCommand:
	case CommandId::UnknownEscCommand:
	case CommandId::ScrollUp:
	case CommandId::ScrollDown:
	case CommandId::ThemeNext:
	case CommandId::ThemePrev:
	case CommandId::FontZoomIn:
	case CommandId::FontZoomOut:
	case CommandId::FontZoomReset:
	case CommandId::VisualFilePickerToggle:
	case CommandId::VisualFontPickerToggle:
		return true;
	default:
		return false;
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
	// Marks are not adjusted by edits, so either end may now lie past the end
	// of its line or of the buffer; clamp both before ordering them.
	clamp_position(buf, cx, cy);
	clamp_position(buf, mx, my);
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


// Row access for commands that touch a few rows. Buffer::Rows() rebuilds a
// string for every line of the file on the first call after any edit, which
// made every motion and delete O(file size). RowsView fetches rows on demand
// and caches them for the current buffer version. When the version changes
// the current cache becomes the previous one (the one before that is freed),
// so a reference obtained just before an edit stays valid (if stale) across
// that edit. Keeping every generation grew without bound in loops that edit
// on each pass (C-u 2000 M-d on a long line used ~2 GB).
class RowsView {
public:
	explicit RowsView(const Buffer &buf) : buf_(&buf) {}


	[[nodiscard]] std::size_t size() const
	{
		return buf_->Nrows();
	}


	[[nodiscard]] bool empty() const
	{
		return size() == 0;
	}


	const std::string &operator[](std::size_t row) const
	{
		if (buf_->Version() != version_) {
			previous_ = std::move(cache_);
			cache_.clear();
			version_ = buf_->Version();
		}
		auto it = cache_.find(row);
		if (it == cache_.end())
			it = cache_.emplace(row, buf_->GetLineString(row)).first;
		return it->second;
	}

private:
	const Buffer *buf_;
	mutable std::uint64_t version_ = ~std::uint64_t{0};
	mutable std::map<std::size_t, std::string> cache_;
	mutable std::map<std::size_t, std::string> previous_;
};


static RowsView
rows_of(const Buffer &buf)
{
	return RowsView(buf);
}


// Helper: extract text from [sx,sy) to [ex,ey) without modifying buffer. Newlines inserted between lines.
static std::string
extract_region_text(const Buffer &buf, std::size_t sx, std::size_t sy, std::size_t ex, std::size_t ey)
{
	const auto &rows = rows_of(buf);
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
		out              += line.substr(xs);
		out              += '\n';
	}
	// middle lines full
	for (std::size_t y = sy + 1; y < ey; ++y) {
		out += static_cast<std::string>(rows[y]);
		out += '\n';
	}
	// last line head
	{
		const auto &line = rows[ey];
		std::size_t xe   = std::min(ex, line.size());
		out              += line.substr(0, xe);
	}
	return out;
}


// Helper: delete region and leave cursor at start (sx,sy). Adjust lines appropriately.
// If `u` is non-null, each underlying mutation is recorded as its own undo node;
// callers that want the whole region-delete to undo/redo as one step should
// wrap the call in an UndoGroupGuard.
static void
delete_region(Buffer &buf, std::size_t sx, std::size_t sy, std::size_t ex, std::size_t ey,
              UndoSystem *u = nullptr)
{
	std::size_t nrows = buf.Nrows();
	if (nrows == 0)
		return;
	if (sy >= nrows)
		return;
	if (ey >= nrows)
		ey = nrows - 1;
	if (sy == ey) {
		// Single line: delete text from xs to xe
		const auto &rows = rows_of(buf);
		const auto &line = rows[sy];
		std::size_t xs   = std::min(sx, line.size());
		std::size_t xe   = std::min(ex, line.size());
		if (xe < xs)
			std::swap(xs, xe);
		std::string deleted = std::string(line.substr(xs, xe - xs));
		buf.delete_text(static_cast<int>(sy), static_cast<int>(xs), xe - xs);
		if (u && !deleted.empty()) {
			buf.SetCursor(xs, sy);
			u->Begin(UndoType::Delete);
			u->Append(std::string_view(deleted));
			u->commit();
		}
	} else {
		// Multi-line: delete from (sx,sy) to (ex,ey) as one contiguous range.
		// Deleting whole rows instead would leave a stray newline when the
		// region ends on a last line that has no trailing newline.
		std::string deleted;
		std::size_t xs = 0;
		for (std::size_t y = sy; y <= ey; ++y) {
			const std::string line = buf.GetLineString(y);
			if (y == sy) {
				xs = std::min(sx, line.size());
				deleted.append(line, xs, std::string::npos);
				deleted.push_back('\n');
			} else if (y < ey) {
				deleted.append(line);
				deleted.push_back('\n');
			} else {
				deleted.append(line, 0, std::min(ex, line.size()));
			}
		}
		buf.delete_text(static_cast<int>(sy), static_cast<int>(xs), deleted.size());
		if (u && !deleted.empty()) {
			buf.SetCursor(xs, sy);
			u->Begin(UndoType::Delete);
			u->Append(std::string_view(deleted));
			u->commit();
		}
	}
	buf.SetCursor(sx, sy);
	buf.SetDirty(true);
}


// The buffer's full text (rows joined by '\n', which reproduces the bytes).
static std::string
buffer_text(const Buffer &buf)
{
	std::string out;
	const std::size_t nrows = buf.Nrows();
	for (std::size_t y = 0; y < nrows; ++y) {
		if (y > 0)
			out.push_back('\n');
		out += buf.GetLineString(y);
	}
	return out;
}


// Replace the text of rows [y, y + old_text lines) in place with new_text,
// where old_text is those rows joined by '\n' (no trailing newline). Unlike
// delete_row/insert_row this never adds or removes the newline after the
// last replaced row, so a file without a trailing newline keeps that shape.
//
// Only the span between the texts' common prefix and suffix is deleted and
// inserted: recording the whole text kept two more copies of it in the undo
// tree (and the piece table and journal) for every replace, however small.
static void
replace_rows_text(Buffer &buf, std::size_t y, const std::string &old_text, const std::string &new_text,
                  UndoSystem *u)
{
	const std::size_t common = std::min(old_text.size(), new_text.size());
	std::size_t pre          = 0;
	while (pre < common && old_text[pre] == new_text[pre])
		++pre;
	std::size_t suf = 0;
	while (suf < common - pre && old_text[old_text.size() - 1 - suf] == new_text[new_text.size() - 1 - suf])
		++suf;
	// Keep UTF-8 sequences whole on both sides of the edit.
	auto is_cont = [](char c) {
		return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
	};
	while (pre > 0 && ((pre < old_text.size() && is_cont(old_text[pre])) ||
	                   (pre < new_text.size() && is_cont(new_text[pre]))))
		--pre;
	while (suf > 0 && (is_cont(old_text[old_text.size() - suf]) || is_cont(new_text[new_text.size() - suf])))
		--suf;
	if (pre + suf > old_text.size() || pre + suf > new_text.size())
		suf = std::min(old_text.size(), new_text.size()) - pre;
	const std::size_t del_len = old_text.size() - pre - suf;
	const std::size_t ins_len = new_text.size() - pre - suf;
	if (del_len == 0 && ins_len == 0)
		return;

	// Row and column of the first changed byte.
	std::size_t row = y, col = pre;
	const std::size_t last_nl = pre == 0 ? std::string::npos : old_text.rfind('\n', pre - 1);
	if (last_nl != std::string::npos) {
		row += static_cast<std::size_t>(std::count(old_text.begin(),
		                                           old_text.begin() + static_cast<std::ptrdiff_t>(pre), '\n'));
		col = pre - last_nl - 1;
	}

	if (del_len > 0) {
		buf.delete_text(static_cast<int>(row), static_cast<int>(col), del_len);
		if (u) {
			buf.SetCursor(col, row);
			u->Begin(UndoType::Delete);
			u->Append(std::string_view(old_text).substr(pre, del_len));
			u->commit();
		}
	}
	if (ins_len > 0) {
		const std::string_view ins = std::string_view(new_text).substr(pre, ins_len);
		buf.insert_text(static_cast<int>(row), static_cast<int>(col), ins);
		if (u) {
			buf.SetCursor(col, row);
			u->Begin(UndoType::Insert);
			u->Append(ins);
			u->commit();
		}
	}
}


static std::string_view line_text_view(const Buffer &buf, std::size_t y);


// Rewrite rows [sy, ey] with fn(line, y) as a single edit (see
// replace_rows_text). Commands that change every row of a range (indent,
// visual-line editing) made one piece-table edit per row, each costing time
// linear in the buffer: minutes for a region of a few hundred thousand rows.
template<typename Fn>
static void
transform_rows(Buffer &buf, const std::size_t sy, std::size_t ey, UndoSystem *u, Fn &&fn)
{
	const std::size_t nrows = buf.Nrows();
	if (nrows == 0 || sy >= nrows)
		return;
	ey = std::min(ey, nrows - 1);
	std::string old_text, new_text;
	for (std::size_t y = sy; y <= ey; ++y) {
		std::string line(line_text_view(buf, y));
		if (y > sy) {
			old_text.push_back('\n');
			new_text.push_back('\n');
		}
		old_text += line;
		fn(line, y);
		new_text += line;
	}
	if (old_text != new_text)
		replace_rows_text(buf, sy, old_text, new_text, u);
}


// Replace the whole buffer's text as one undo group.
static void
replace_buffer_text(Buffer &buf, const std::string &old_text, const std::string &new_text, UndoSystem *u)
{
	UndoGroupGuard group(u);
	replace_rows_text(buf, 0, old_text, new_text, u);
}


// Insert arbitrary text at cursor, supporting newlines. Updates cursor position.
static void
insert_text_at_cursor(Buffer &buf, const std::string &text)
{
	std::size_t nrows = buf.Nrows();
	std::size_t y     = buf.Cury();
	std::size_t x     = buf.Curx();
	if (y > nrows)
		y = nrows;
	if (nrows == 0) {
		buf.insert_row(0, "");
		nrows = 1;
	}
	if (y >= nrows) {
		buf.insert_row(static_cast<int>(nrows), "");
		nrows = buf.Nrows();
	}

	std::size_t cur_y = y;
	std::size_t cur_x = std::min(x, buf.GetLineString(cur_y).size());

	// One insert: the piece table takes newlines directly. Inserting and
	// splitting line by line (re-trimming the remaining text each time) was
	// quadratic in the number of lines yanked.
	buf.insert_text(static_cast<int>(cur_y), static_cast<int>(cur_x), text);
	const std::size_t last_nl = text.rfind('\n');
	if (last_nl == std::string::npos) {
		cur_x += text.size();
	} else {
		cur_y += static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
		cur_x = text.size() - last_nl - 1;
	}

	buf.SetCursor(cur_x, cur_y);
	buf.SetDirty(true);
}


// Byte offset (on a character boundary) whose display column is closest to
// rx_target; the inverse of compute_render_x.
static std::size_t
inverse_render_to_source_col(const std::string &line, std::size_t rx_target, std::size_t tabw)
{
	if (rx_target == 0)
		return 0;
	std::size_t rx        = 0;
	std::size_t i         = 0;
	std::size_t best_col  = 0;
	std::size_t best_dist = rx_target;
	while (true) {
		const std::size_t dist = (rx > rx_target) ? (rx - rx_target) : (rx_target - rx);
		if (dist <= best_dist) {
			best_dist = dist;
			best_col  = i;
		}
		if (i >= line.size() || rx >= rx_target)
			break;
		std::size_t len = 1, width = 1;
		measure_char(line, i, rx, tabw, len, width);
		rx += width;
		i += len;
	}
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
					ensure_at_least_one_line(*buf);
					const auto &lines2 = rows_of(*buf);
					if (by >= lines2.size())
						by = lines2.size() - 1;
					std::string line2     = static_cast<std::string>(lines2[by]);
					std::size_t rx_target = bco + vx;
					std::size_t sx        = inverse_render_to_source_col(line2, rx_target, 8);
					row                   = by;
					col                   = sx;
				} else {
					row = static_cast<std::size_t>(ay);
					col = static_cast<std::size_t>(ax);
				}
			} catch (...) {
				// ignore parse errors
			}
		}
	}
	ensure_at_least_one_line(*buf);
	const auto &lines = rows_of(*buf);
	if (row >= lines.size())
		row = lines.size() - 1;
	std::string line = static_cast<std::string>(lines[row]);
	if (col > line.size())
		col = line.size();
	buf->SetCursor(col, row);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


// --- Search helpers (UI-agnostic) ---
// Row y's text without its newline, viewing the materialized buffer (valid
// until the next edit).
static std::string_view
line_text_view(const Buffer &buf, const std::size_t y)
{
	std::string_view v = buf.GetLineView(y);
	if (!v.empty() && v.back() == '\n')
		v.remove_suffix(1);
	return v;
}



// ===== Incremental search engine =====
// Finding a match stops at the first one after the current position (it
// used to collect every match in the buffer on every keystroke and every
// next/previous); the "i/N" count is capped (see search_count).

struct SearchHit {
	std::size_t y   = 0;
	std::size_t x   = 0;
	std::size_t len = 0;
};

// Count caps: stop at this many matches, and after scanning this many bytes
// (the regex budget is lower: std::regex is far slower than find).
static constexpr std::size_t kSearchCountCap        = 1000;
static constexpr std::size_t kSearchCountBytesPlain = std::size_t{64} << 20;
static constexpr std::size_t kSearchCountBytesRegex = std::size_t{4} << 20;


using RegexMatcher = kte::Regex; // see RegexEngine.h


// Run regex work: std::regex recurses per matched character, so it runs on
// a large stack (RegexGuard.h); PCRE2 does not recurse on the C stack.
template<typename Fn>
static void
run_regex_work(Fn &&fn)
{
	if (std::string_view(kte::Regex::EngineName()) == "PCRE2")
		fn();
	else
		kte::RunWithLargeStack(fn);
}


static std::string_view line_text_view(const Buffer &buf, std::size_t y);


// The whole text as one view into the materialized buffer.
static std::string_view
buffer_text_view(const Buffer &buf)
{
	const std::size_t nrows = buf.Nrows();
	if (nrows == 0)
		return {};
	const char *base = buf.GetLineView(0).data();
	const auto last  = buf.GetLineView(nrows - 1);
	return {base, static_cast<std::size_t>(last.data() + last.size() - base)};
}


// The last occurrence of q starting before `end`. string_view::rfind scans
// backwards a byte at a time (half a second over 100 MB); forward find is
// vectorized, so scan blocks forward, moving back one block at a time.
static std::size_t
rfind_before(std::string_view text, std::string_view q, std::size_t end)
{
	constexpr std::size_t kBlock = std::size_t{1} << 20;
	end                          = std::min(end, text.size());
	while (end > 0) {
		const std::size_t start = end > kBlock ? end - kBlock : 0;
		// Matches starting in [start, end); they may run past `end`.
		const std::string_view block = text.substr(start, std::min(text.size() - start, end - start + q.size() - 1));
		std::size_t last = std::string_view::npos;
		for (std::size_t p = block.find(q); p != std::string_view::npos && start + p < end; p = block.find(q, p + 1))
			last = p;
		if (last != std::string_view::npos)
			return start + last;
		end = start;
	}
	return std::string_view::npos;
}


// Plain text: the first match at or after (y, x) (forward), or the last one
// starting before it (backward), wrapping around the buffer.
static bool
plain_find(const Buffer &buf, const std::string &q, std::size_t y, std::size_t x, bool forward, SearchHit &hit)
{
	if (q.empty() || q.find('\n') != std::string::npos)
		return false; // matches never span lines
	const std::string_view text = buffer_text_view(buf);
	const std::size_t off       = std::min(buf.RowColToOffset(y, x), text.size());
	std::size_t p               = std::string_view::npos;
	if (forward) {
		p = text.find(q, off);
		if (p == std::string_view::npos) {
			p = text.find(q);
			if (p >= off)
				p = std::string_view::npos;
		}
	} else {
		p = rfind_before(text, q, off);
		if (p == std::string_view::npos) {
			p = rfind_before(text, q, text.size());
			if (p != std::string_view::npos && p < off)
				p = std::string_view::npos;
		}
	}
	if (p == std::string_view::npos)
		return false;
	const auto [ry, rx] = buf.OffsetToRowCol(p);
	hit                 = SearchHit{ry, rx, q.size()};
	return true;
}


// Walks the lines of the materialized text (a view per line; no per-row
// buffer calls) forward or backward from row y, wrapping.
class LineWalker {
public:
	LineWalker(const Buffer &buf, std::size_t y)
		: text_(buffer_text_view(buf)), nrows_(std::max<std::size_t>(buf.Nrows(), 1)),
		  y_(std::min(y, nrows_ - 1)), start_(std::min(buf.RowColToOffset(y_, 0), text_.size())) {}


	[[nodiscard]] std::size_t Row() const
	{
		return y_;
	}


	[[nodiscard]] std::string_view Line() const
	{
		const std::size_t e = text_.find('\n', start_);
		return text_.substr(start_, (e == std::string_view::npos ? text_.size() : e) - start_);
	}


	void Next()
	{
		const std::size_t e = text_.find('\n', start_);
		if (e == std::string_view::npos || y_ + 1 >= nrows_) {
			y_     = 0;
			start_ = 0;
		} else {
			++y_;
			start_ = e + 1;
		}
	}


	void Prev()
	{
		if (y_ == 0 || start_ == 0) {
			y_                  = nrows_ - 1;
			const std::size_t n = text_.rfind('\n');
			start_              = n == std::string_view::npos ? 0 : n + 1;
			return;
		}
		--y_;
		// start_ - 1 is the newline ending the previous row.
		const std::size_t n = start_ >= 2 ? text_.rfind('\n', start_ - 2) : std::string_view::npos;
		start_              = n == std::string_view::npos ? 0 : n + 1;
	}

private:
	std::string_view text_;
	std::size_t nrows_;
	std::size_t y_;
	std::size_t start_;
};


// Regex: like plain_find, line by line. Lines longer than line_limit are
// skipped (counted in *skipped); *limited counts lines where the engine hit
// its match limit (catastrophic backtracking) and gave up.
static bool
regex_find(const Buffer &buf, const RegexMatcher &m, std::size_t y0, std::size_t x0, bool forward,
           std::size_t line_limit, std::size_t *skipped, std::size_t *limited, SearchHit &hit)
{
	const std::size_t nrows = buf.Nrows();
	if (nrows == 0)
		return false;
	y0         = std::min(y0, nrows - 1);
	bool found = false;
	auto scan  = [&] {
		LineWalker w(buf, y0);
		// nrows + 1 visits: the start row is visited again after wrapping,
		// for the part on the other side of x0.
		for (std::size_t i = 0; i <= nrows && !found; ++i) {
			if (i > 0) {
				if (forward)
					w.Next();
				else
					w.Prev();
			}
			const std::size_t y         = w.Row();
			const std::string_view line = w.Line();
			if (line.size() > line_limit) {
				if (skipped && i < nrows)
					++*skipped;
				continue;
			}
			const bool first = i == 0, again = i == nrows;
			std::size_t pos = 0, len = 0;
			auto search = [&](std::size_t from) {
				const bool ok = m.Search(line, from, pos, len);
				if (!ok && limited && m.LimitHit())
					++*limited;
				return ok;
			};
			if (forward) {
				const std::size_t from = first ? x0 : 0;
				if (search(from) && (!again || pos < x0)) {
					hit   = SearchHit{y, pos, len};
					found = true;
				}
			} else {
				// The last match in the line before x0 (first visit), at or
				// after it (after wrapping), or anywhere (other rows).
				bool have        = false;
				SearchHit best{};
				std::size_t from = 0;
				while (from <= line.size() && search(from)) {
					if (first && pos >= x0)
						break;
					if (!again || pos >= x0) {
						best = SearchHit{y, pos, len};
						have = true;
					}
					from = pos + std::max<std::size_t>(len, 1);
				}
				if (have) {
					hit   = best;
					found = true;
				}
			}
		}
	};
	run_regex_work(scan);
	return found;
}


struct SearchCount {
	std::size_t before = 0; // matches before the current one
	std::size_t total  = 0;
	bool reached       = false; // the scan got as far as the current match
	bool complete      = false; // every match was counted
};


static SearchCount
search_count(const Buffer &buf, bool regex, const std::string &q, const RegexMatcher *m, const SearchHit &cur,
             std::size_t line_limit)
{
	SearchCount c;
	const std::size_t nrows = buf.Nrows();
	if (!regex) {
		const std::string_view all  = buffer_text_view(buf);
		const std::string_view text = all.substr(0, kSearchCountBytesPlain);
		const std::size_t cur_off   = buf.RowColToOffset(cur.y, cur.x);
		std::size_t p               = 0;
		while (c.total < kSearchCountCap && (p = text.find(q, p)) != std::string_view::npos) {
			if (p < cur_off)
				++c.before;
			else
				c.reached = true;
			++c.total;
			p += q.size();
		}
		if (p != std::string_view::npos && p >= cur_off)
			c.reached = true;
		c.complete = p == std::string_view::npos && text.size() == all.size();
		if (c.complete)
			c.reached = true;
		return c;
	}
	std::size_t scanned = 0;
	bool stopped        = false;
	run_regex_work([&] {
		LineWalker w(buf, 0);
		for (std::size_t y = 0; y < nrows && !stopped; ++y, w.Next()) {
			const std::string_view line = w.Line();
			if (line.size() > line_limit)
				continue;
			scanned += line.size() + 1;
			std::size_t from = 0, pos = 0, len = 0;
			while (from <= line.size() && m->Search(line, from, pos, len)) {
				if (y < cur.y || (y == cur.y && pos < cur.x))
					++c.before;
				else
					c.reached = true;
				if (++c.total >= kSearchCountCap) {
					stopped = true;
					break;
				}
				from = pos + std::max<std::size_t>(len, 1);
			}
			if (y >= cur.y)
				c.reached = true;
			if (scanned > kSearchCountBytesRegex && y + 1 < nrows) {
				stopped = true;
				break;
			}
		}
	});
	c.complete = !stopped;
	if (c.complete)
		c.reached = true;
	return c;
}


enum class SearchStep { Here, Next, Prev };


// Run the search for the editor's query and show the result: move the
// cursor to the match (or back to the origin if there is none) and set the
// status. Here: from the current match (so it stays while the extended query
// still matches there) or the search origin; Next/Prev: after/before the
// current match. incremental limits regex to lines up to
// Regex::IncrementalLineLimit() (search-as-you-type).
static void
run_search(Editor &ed, Buffer &buf, bool regex, SearchStep step, bool incremental)
{
	const std::string q      = ed.SearchQuery();
	const std::string prefix = regex ? "Regex: " : "Find: ";
	const bool have_cur      = ed.SearchIndex() >= 0;
	std::size_t y = buf.Cury(), x = buf.Curx();
	if (have_cur) {
		y = ed.SearchMatchY();
		x = ed.SearchMatchX();
	} else if (step == SearchStep::Here && ed.SearchOriginSet()) {
		y = ed.SearchOrigY();
		x = ed.SearchOrigX();
	}
	if (step == SearchStep::Next && have_cur) {
		const std::size_t len = std::max<std::size_t>(ed.SearchMatchLen(), 1);
		x += len;
		if (x > line_text_view(buf, y).size() && y + 1 < buf.Nrows()) {
			++y;
			x = 0;
		}
	}

	RegexMatcher m;
	std::string err;
	SearchHit hit;
	bool found          = false;
	std::size_t skipped = 0, limited = 0;
	const std::size_t line_limit = (regex && incremental) ? kte::Regex::IncrementalLineLimit()
	                                                      : static_cast<std::size_t>(-1);
	if (!q.empty()) {
		if (!regex)
			found = plain_find(buf, q, y, x, step != SearchStep::Prev, hit);
		else if (m.Compile(q, err))
			found = regex_find(buf, m, y, x, step != SearchStep::Prev, line_limit, &skipped, &limited, hit);
	}
	// Notes appended to the status.
	std::string notes;
	if (skipped > 0)
		notes += "  [" + std::to_string(skipped) + " long line(s) skipped; Left/Right search all]";
	if (limited > 0)
		notes += "  [pattern too complex on " + std::to_string(limited) + " line(s); stopped there]";

	if (!found) {
		ed.SetSearchMatch(0, 0, 0);
		if (ed.SearchOriginSet()) {
			buf.SetCursor(ed.SearchOrigX(), ed.SearchOrigY());
			buf.SetOffsets(ed.SearchOrigRowoffs(), ed.SearchOrigColoffs());
		}
		ed.SetSearchIndex(-1);
		std::string status = prefix + q;
		if (!err.empty())
			status += "  [error: " + err + "]";
		else
			status += notes;
		ed.SetStatus(status);
		return;
	}

	ed.SetSearchMatch(hit.y, hit.x, hit.len);
	buf.SetCursor(hit.x, hit.y);
	ensure_cursor_visible(ed, buf);
	const SearchCount c = search_count(buf, regex, q, regex ? &m : nullptr, hit, line_limit);
	ed.SetSearchIndex(static_cast<int>(c.reached ? c.before : 0));
	std::string where;
	if (c.complete)
		where = std::to_string(c.before + 1) + "/" + std::to_string(c.total);
	else if (c.reached)
		where = std::to_string(c.before + 1) + "/" + std::to_string(c.total) + "+";
	else
		where = std::to_string(c.total) + "+ matches";
	ed.SetStatus(prefix + q + "  " + where + notes);
}


static bool
regex_prompt(const Editor &ed)
{
	return ed.CurrentPromptKind() == Editor::PromptKind::RegexSearch ||
	       ed.CurrentPromptKind() == Editor::PromptKind::RegexReplaceFind;
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
		// A virtual buffer's name (e.g. +HELP+) is not a path: ask for one.
		if (!buf->Filename().empty() && !buf->IsVirtual()) {
			// If first-time save to an existing path, confirm overwrite
			if (fs_exists(buf->Filename())) {
				ctx.editor.StartPrompt(Editor::PromptKind::Confirm, "Overwrite", "");
				ctx.editor.SetPendingOverwritePath(buf->Filename());
				ctx.editor.SetStatus(
					std::string("Overwrite existing file '") + buf->Filename() + "'? (y/N)");
				return true;
			} else {
				if (!buf->SaveAs(buf->Filename(), err)) {
					ctx.editor.SetStatus(err);
					return false;
				}
				buf->SetDirty(false);
				if (auto *sm = ctx.editor.Swap())
					sm->ResetJournal(*buf);
				ctx.editor.SetStatus("Saved " + buf->Filename());
				if (auto *u = buf->Undo())
					u->mark_saved();
				return true;
			}
		}
		// If buffer has no name, prompt for a filename
		ctx.editor.StartPrompt(Editor::PromptKind::SaveAs, "Save as", "");
		ctx.editor.SetStatus("Save as: ");
		return true;
	}
	// External modification detection: if the on-disk file changed since we last observed it,
	// require confirmation before overwriting.
	if (buf->ExternallyModifiedOnDisk()) {
		ctx.editor.StartPrompt(Editor::PromptKind::Confirm, "Overwrite", "");
		ctx.editor.SetPendingOverwritePath(buf->Filename());
		ctx.editor.SetStatus(
			std::string("File changed on disk: overwrite '") + buf->Filename() + "'? (y/N)");
		return true;
	}
	if (!buf->Save(err)) {
		ctx.editor.SetStatus(err);
		return false;
	}
	buf->SetDirty(false);
	if (auto *sm = ctx.editor.Swap())
		sm->ResetJournal(*buf);
	ctx.editor.SetStatus("Saved " + buf->Filename());
	if (auto *u = buf->Undo())
		u->mark_saved();
	return true;
}


// --- Working directory commands ---
static bool
cmd_show_working_directory(CommandContext &ctx)
{
	try {
		std::filesystem::path cwd = std::filesystem::current_path();
		ctx.editor.SetStatus(std::string("cwd: ") + cwd.string());
		return true;
	} catch (const std::exception &e) {
		ctx.editor.SetStatus(std::string("cwd: <error> ") + e.what());
		return false;
	}
}


static bool
cmd_change_working_directory_start(CommandContext &ctx)
{
	std::string initial;
	try {
		initial = std::filesystem::current_path().string() + "/";
	} catch (...) {
		initial.clear();
	}
	ctx.editor.StartPrompt(Editor::PromptKind::Chdir, "chdir", initial);
	ctx.editor.SetStatus(std::string("chdir: ") + ctx.editor.PromptText());
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
	if (open_in_other_buffer(ctx.editor, ctx.arg)) {
		ctx.editor.SetStatus(ctx.arg + " is open in another buffer; save or close that buffer instead");
		return false;
	}
	// Ask before replacing an existing file other than the buffer's own.
	if (fs_exists(ctx.arg) && !is_buffers_own_file(*buf, ctx.arg)) {
		ctx.editor.StartPrompt(Editor::PromptKind::Confirm, "Overwrite", "");
		ctx.editor.SetPendingOverwritePath(ctx.arg);
		ctx.editor.SetStatus(std::string("Overwrite existing file '") + ctx.arg + "'? (y/N)");
		return true;
	}
	std::string err;
	if (!buf->SaveAs(ctx.arg, err)) {
		ctx.editor.SetStatus(err);
		return false;
	}
	if (auto *sm = ctx.editor.Swap()) {
		sm->NotifyFilenameChanged(*buf);
		sm->ResetJournal(*buf);
	}
	ctx.editor.SetStatus("Saved as " + ctx.arg);
	if (auto *u = buf->Undo())
		u->mark_saved();
	return true;
}


static std::string buffer_display_name(const Buffer &b);


// Display names of buffers with unsaved changes ("a.txt, b.txt and 2 more"),
// or empty if none.
static std::string
dirty_buffer_names(const Editor &ed)
{
	std::vector<std::string> names;
	for (const auto &b: ed.Buffers()) {
		if (b.Dirty())
			names.push_back(buffer_display_name(b));
	}
	std::string out;
	for (std::size_t i = 0; i < names.size() && i < 3; ++i) {
		if (i > 0)
			out += ", ";
		out += names[i];
	}
	if (names.size() > 3)
		out += " and " + std::to_string(names.size() - 3) + " more";
	return out;
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
	(void) buf;
	// Any dirty buffer (not only the current one) needs confirmation: quitting
	// used to drop other buffers' changes without a word.
	const std::string dirty = dirty_buffer_names(ctx.editor);
	if (!dirty.empty()) {
		ctx.editor.SetStatus("Unsaved changes in " + dirty + ". C-k q again to quit without saving");
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
		// Same guard as save: do not silently overwrite a file that changed
		// on disk since it was loaded.
		if (buf->IsFileBacked() && buf->ExternallyModifiedOnDisk()) {
			ctx.editor.SetStatus("File changed on disk; save with C-k s to confirm overwriting, then quit");
			return false;
		}
		if (buf->IsFileBacked()) {
			if (buf->Save(err)) {
				buf->SetDirty(false);
				if (auto *sm = ctx.editor.Swap())
					sm->ResetJournal(*buf);
				if (auto *u = buf->Undo())
					u->mark_saved();
			} else {
				ctx.editor.SetStatus(err);
				return false;
			}
		} else if (!buf->Filename().empty() && !buf->IsVirtual()) {
			if (buf->SaveAs(buf->Filename(), err)) {
				buf->SetDirty(false);
				if (auto *sm = ctx.editor.Swap())
					sm->ResetJournal(*buf);
				if (auto *u = buf->Undo())
					u->mark_saved();
			} else {
				ctx.editor.SetStatus(err);
				return false;
			}
		} else {
			ctx.editor.SetStatus("Buffer not file-backed; use save-as before quitting");
			return false;
		}
	}
	// Other buffers may still hold unsaved changes: do not drop them.
	const std::string dirty = dirty_buffer_names(ctx.editor);
	if (!dirty.empty()) {
		ctx.editor.SetStatus("Saved; unsaved changes remain in " + dirty + ". Not quitting");
		return true;
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
	// C-g is mapped to Refresh and acts as a general cancel key.
	// Cancel visual-line (multicursor) mode if active.
	if (Buffer *buf = ctx.editor.CurrentBuffer()) {
		if (buf->VisualLineActive()) {
			buf->VisualLineClear();
			ctx.editor.SetStatus("Visual line: OFF");
			return true;
		}
	}
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
		// Clear any pending close/overwrite state associated with prompts
		ctx.editor.SetCloseConfirmPending(false);
		ctx.editor.SetCloseAfterSave(false);
		ctx.editor.ClearPendingOverwritePath();
		ctx.editor.CancelRecoveryPrompt();
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
	// If nothing else to cancel, treat C-g/refresh as a mark clear (ke behavior).
	if (Buffer *buf = ctx.editor.CurrentBuffer()) {
		if (buf->MarkSet()) {
			buf->ClearMark();
			ctx.editor.SetStatus("Mark cleared");
			return true;
		}
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


// Start generic command prompt (": ")
static bool
cmd_command_prompt_start(const CommandContext &ctx)
{
	// Close any pending edit batch before entering prompt
	if (Buffer *b = ctx.editor.CurrentBuffer()) {
		if (auto *u = b->Undo())
			u->commit();
	}
	ctx.editor.StartPrompt(Editor::PromptKind::Command, "", "");
	ctx.editor.SetStatus(": ");
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
cmd_unknown_esc_command(CommandContext &ctx)
{
	(void) ctx;
	ctx.editor.SetStatus("invalid escape command");
	return true;
}


// --- Syntax highlighting commands ---
static void
apply_filetype(Buffer &buf, const std::string &ft)
{
	// Only reachable from explicit user commands (:syntax on, :set
	// filetype=...) - mark so frontends that re-apply config-driven syntax
	// defaults every frame (e.g. ImGuiFrontend) don't stomp this choice.
	buf.SetSyntaxUserOverride(true);
	buf.EnsureHighlighter();
	auto *eng = buf.Highlighter();
	if (!eng)
		return;
	std::string val = ft;
	// trim + lower
	auto trim = [](const std::string &s) {
		std::string r = s;
		auto notsp    = [](int ch) {
			return !std::isspace(ch);
		};
		r.erase(r.begin(), std::find_if(r.begin(), r.end(), notsp));
		r.erase(std::find_if(r.rbegin(), r.rend(), notsp).base(), r.end());
		return r;
	};
	val = trim(val);
	for (auto &ch: val)
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	if (val == "off") {
		eng->SetHighlighter(nullptr);
		buf.SetFiletype("");
		buf.SetSyntaxEnabled(false);
		return;
	}
	if (val.empty()) {
		// Empty means unknown/unspecified -> use NullHighlighter but keep syntax enabled
		buf.SetFiletype("");
		buf.SetSyntaxEnabled(true);
		eng->SetHighlighter(std::make_unique<kte::NullHighlighter>());
		eng->InvalidateFrom(0);
		return;
	}
	// Normalize and create via registry
	std::string norm = kte::HighlighterRegistry::Normalize(val);
	auto hl          = kte::HighlighterRegistry::CreateFor(norm);
	if (hl) {
		eng->SetHighlighter(std::move(hl));
		buf.SetFiletype(norm);
		buf.SetSyntaxEnabled(true);
		eng->InvalidateFrom(0);
	} else {
		// Unknown -> install NullHighlighter and keep syntax enabled
		eng->SetHighlighter(std::make_unique<kte::NullHighlighter>());
		buf.SetFiletype(val); // record what user asked even if unsupported
		buf.SetSyntaxEnabled(true);
		eng->InvalidateFrom(0);
	}
}


static bool
cmd_syntax(CommandContext &ctx)
{
	Buffer *b = ctx.editor.CurrentBuffer();
	if (!b) {
		ctx.editor.SetStatus("No buffer");
		return true;
	}
	std::string arg = ctx.arg;
	// trim
	auto trim = [](std::string &s) {
		auto notsp = [](int ch) {
			return !std::isspace(ch);
		};
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), notsp));
		s.erase(std::find_if(s.rbegin(), s.rend(), notsp).base(), s.end());
	};
	trim(arg);
	if (arg == "on") {
		b->SetSyntaxUserOverride(true);
		b->SetSyntaxEnabled(true);
		// If no highlighter but filetype is cpp by extension, set it
		if (!b->Highlighter() || !b->Highlighter()->HasHighlighter()) {
			apply_filetype(*b, b->Filetype().empty() ? std::string("cpp") : b->Filetype());
		}
		ctx.editor.SetStatus("syntax: on");
	} else if (arg == "off") {
		b->SetSyntaxUserOverride(true);
		b->SetSyntaxEnabled(false);
		ctx.editor.SetStatus("syntax: off");
	} else if (arg == "reload") {
		if (auto *eng = b->Highlighter())
			eng->InvalidateFrom(0);
		ctx.editor.SetStatus("syntax: reloaded");
	} else {
		ctx.editor.SetStatus("usage: :syntax on|off|reload");
	}
	return true;
}


static bool
cmd_set_option(CommandContext &ctx)
{
	Buffer *b = ctx.editor.CurrentBuffer();
	if (!b) {
		ctx.editor.SetStatus("No buffer");
		return true;
	}
	// Expect key=value
	auto eq = ctx.arg.find('=');
	if (eq == std::string::npos) {
		ctx.editor.SetStatus("usage: :set key=value");
		return true;
	}
	std::string key = ctx.arg.substr(0, eq);
	std::string val = ctx.arg.substr(eq + 1);
	// trim
	auto trim = [](std::string &s) {
		auto notsp = [](int ch) {
			return !std::isspace(ch);
		};
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), notsp));
		s.erase(std::find_if(s.rbegin(), s.rend(), notsp).base(), s.end());
	};
	trim(key);
	trim(val);
	// lower-case value for filetype
	for (auto &ch: val)
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	if (key == "filetype") {
		apply_filetype(*b, val);
		if (b->SyntaxEnabled())
			ctx.editor.SetStatus(
				std::string("filetype: ") + (b->Filetype().empty() ? "off" : b->Filetype()));
		else
			ctx.editor.SetStatus("filetype: off");
		return true;
	}
	ctx.editor.SetStatus("unknown option: " + key);
	return true;
}


// GUI theme cycling commands (available in GUI build; ImGui-only for now)
#if defined(KTE_BUILD_GUI) && !defined(KTE_USE_QT)
static bool
cmd_theme_next(CommandContext &ctx)
{
	auto id = kte::NextTheme();
	ctx.editor.SetStatus(std::string("Theme: ") + kte::ThemeName(id));
	return true;
}


static bool
cmd_theme_prev(CommandContext &ctx)
{
	auto id = kte::PrevTheme();
	ctx.editor.SetStatus(std::string("Theme: ") + kte::ThemeName(id));
	return true;
}
#else
static bool
cmd_theme_next(CommandContext &ctx)
{
	ctx.editor.SetStatus("Theme switching only available in GUI build");
	return true;
}


static bool
cmd_theme_prev(CommandContext &ctx)
{
	ctx.editor.SetStatus("Theme switching only available in GUI build");
	return true;
}
#endif


// Theme set by name command
#if defined(KTE_BUILD_GUI) && !defined(KTE_USE_QT)
static bool
cmd_theme_set_by_name(const CommandContext &ctx)
{
	std::string name = ctx.arg;
	// trim spaces
	auto ltrim = [](std::string &s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
			return !std::isspace(ch);
		}));
	};
	auto rtrim = [](std::string &s) {
		s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
			return !std::isspace(ch);
		}).base(), s.end());
	};
	ltrim(name);
	rtrim(name);
	if (name.empty()) {
		// Show current theme when no argument provided
		ctx.editor.SetStatus(
			std::string("Current theme: ") + kte::CurrentThemeName());
		return true;
	}
	if (kte::ApplyThemeByName(name)) {
		ctx.editor.SetStatus(
			std::string("Theme: ") + name + std::string(" (bg: ") + kte::BackgroundModeName() + ")");
	} else {
		// Build list of available themes
		const auto &reg = kte::ThemeRegistry();
		std::string avail;
		for (size_t i = 0; i < reg.size(); ++i) {
			if (i)
				avail += ", ";
			avail += reg[i]->Name();
		}
		ctx.editor.SetStatus(std::string("Unknown theme; available: ") + avail);
	}
	return true;
}
#else
static bool
cmd_theme_set_by_name(CommandContext &ctx)
{
#  if defined(KTE_BUILD_GUI) && defined(KTE_USE_QT)
	// Qt GUI build: schedule theme change for frontend
	std::string name = ctx.arg;
	// trim spaces
	auto ltrim = [](std::string &s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
			return !std::isspace(ch);
		}));
	};
	auto rtrim = [](std::string &s) {
		s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
			return !std::isspace(ch);
		}).base(), s.end());
	};
	ltrim(name);
	rtrim(name);
	if (name.empty()) {
		ctx.editor.SetStatus("theme: provide a name (e.g., nord, solarized-dark, gruvbox-light, eink)");
		return true;
	}
	kte::gThemeChangeRequest = name;
	kte::gThemeChangePending = true;
	ctx.editor.SetStatus(std::string("Theme requested: ") + name);
	return true;
#  else
	(void) ctx;
	// No-op in terminal build
	return true;
#  endif
}
#endif


// Font set by name (GUI)
#if defined(KTE_BUILD_GUI) && !defined(KTE_USE_QT)
static bool
cmd_font_set_by_name(const CommandContext &ctx)
{
	using namespace kte::Fonts;
	std::string name = ctx.arg;
	// trim
	auto ltrim = [](std::string &s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
			return !std::isspace(ch);
		}));
	};
	auto rtrim = [](std::string &s) {
		s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
			return !std::isspace(ch);
		}).base(), s.end());
	};
	ltrim(name);
	rtrim(name);
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
		return (char) std::tolower(c);
	});
	if (name.empty()) {
		// Show current font when no argument provided
		auto &reg                = FontRegistry::Instance();
		std::string current_font = reg.CurrentFontName();
		if (current_font.empty())
			current_font = "default";
		ctx.editor.SetStatus(std::string("Current font: ") + current_font);
		return true;
	}

	auto &reg = FontRegistry::Instance();
	if (!reg.HasFont(name)) {
		ctx.editor.SetStatus("font: unknown name");
		return true;
	}

	float size = reg.CurrentFontSize();
	if (size <= 0.0f) {
		// Fallback to current ImGui font size if available
		size = ImGui::GetFontSize();
		if (size <= 0.0f)
			size = 16.0f;
	}
	reg.RequestLoadFont(name, size);
	ctx.editor.SetStatus(std::string("Font: ") + name + " (" + std::to_string((int) std::round(size)) + ")");
	return true;
}
#else
static bool
cmd_font_set_by_name(CommandContext &ctx)
{
	// Qt build: queue font family change
	std::string name = ctx.arg;
	// trim
	auto ltrim = [](std::string &s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
			return !std::isspace(ch);
		}));
	};
	auto rtrim = [](std::string &s) {
		s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
			return !std::isspace(ch);
		}).base(), s.end());
	};
	ltrim(name);
	rtrim(name);
	if (name.empty()) {
		// Show current font when no argument provided
		std::string cur = kte::gCurrentFontFamily.empty() ? std::string("default") : kte::gCurrentFontFamily;
		ctx.editor.SetStatus(std::string("Current font: ") + cur);
		return true;
	}
	kte::gFontFamilyRequest = name;
	// Keep size if not specified by user; signal change
	kte::gFontChangePending = true;
	ctx.editor.SetStatus(std::string("Font requested: ") + name);
	return true;
}
#endif


// Font size set (GUI, ImGui-only for now)
#if defined(KTE_BUILD_GUI) && !defined(KTE_USE_QT)
static bool
cmd_font_set_size(const CommandContext &ctx)
{
	using namespace kte::Fonts;
	std::string a = ctx.arg;
	auto ltrim    = [](std::string &s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
			return !std::isspace(ch);
		}));
	};
	auto rtrim = [](std::string &s) {
		s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
			return !std::isspace(ch);
		}).base(), s.end());
	};
	ltrim(a);
	rtrim(a);
	if (a.empty()) {
		// Show current font size when no argument provided
		auto &reg          = FontRegistry::Instance();
		float current_size = reg.CurrentFontSize();
		if (current_size <= 0.0f) {
			// Fallback to current ImGui font size if available
			current_size = ImGui::GetFontSize();
			if (current_size <= 0.0f)
				current_size = 16.0f;
		}
		ctx.editor.SetStatus(
			std::string("Current font size: ") + std::to_string((int) std::round(current_size)));
		return true;
	}
	char *endp = nullptr;
	float size = strtof(a.c_str(), &endp);
	if (endp == a.c_str() || !std::isfinite(size)) {
		ctx.editor.SetStatus("font-size: expected number");
		return true;
	}
	// Clamp to a reasonable range
	if (size < 6.0f)
		size = 6.0f;
	if (size > 96.0f)
		size = 96.0f;

	auto &reg        = FontRegistry::Instance();
	std::string name = reg.CurrentFontName();
	if (name.empty())
		name = "default";
	if (!reg.HasFont(name))
		name = "default";

	reg.RequestLoadFont(name, size);
	ctx.editor.SetStatus(std::string("Font size: ") + std::to_string((int) std::round(size)));
	return true;
}
#else
static bool
cmd_font_set_size(CommandContext &ctx)
{
	// Qt build: parse size and queue change
	std::string a = ctx.arg;
	auto ltrim    = [](std::string &s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
			return !std::isspace(ch);
		}));
	};
	auto rtrim = [](std::string &s) {
		s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
			return !std::isspace(ch);
		}).base(), s.end());
	};
	ltrim(a);
	rtrim(a);
	if (a.empty()) {
		float cur = (kte::gCurrentFontSize > 0.0f) ? kte::gCurrentFontSize : 18.0f;
		ctx.editor.SetStatus(std::string("Current font size: ") + std::to_string((int) std::round(cur)));
		return true;
	}
	char *endp = nullptr;
	float size = strtof(a.c_str(), &endp);
	if (endp == a.c_str() || !std::isfinite(size)) {
		ctx.editor.SetStatus("font-size: expected number");
		return true;
	}
	if (size < 6.0f)
		size = 6.0f;
	if (size > 96.0f)
		size = 96.0f;
	kte::gFontSizeRequest   = size;
	kte::gFontChangePending = true;
	ctx.editor.SetStatus(std::string("Font size requested: ") + std::to_string((int) std::round(size)));
	return true;
}
#endif


// Toggle edit mode (code/writing) for current buffer
static bool
cmd_toggle_edit_mode(const CommandContext &ctx)
{
	Buffer *b = ctx.editor.CurrentBuffer();
	if (!b)
		return false;

	std::string arg = ctx.arg;
	std::transform(arg.begin(), arg.end(), arg.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	// Trim whitespace
	auto start = arg.find_first_not_of(" \t");
	if (start != std::string::npos)
		arg = arg.substr(start);
	auto end = arg.find_last_not_of(" \t");
	if (end != std::string::npos)
		arg = arg.substr(0, end + 1);

	if (arg == "code") {
		b->SetEditMode(EditMode::Code);
	} else if (arg == "writing") {
		b->SetEditMode(EditMode::Writing);
	} else {
		b->ToggleEditMode();
	}

	// Writing mode disables syntax highlighting; code mode re-enables it.
	b->SetSyntaxEnabled(b->GetEditMode() == EditMode::Code);

	const char *mode_str = (b->GetEditMode() == EditMode::Writing) ? "writing" : "code";
	ctx.editor.SetStatus(std::string("Mode: ") + mode_str);
	return true;
}


// Background set command (GUI, ImGui-only for now)
#if defined(KTE_BUILD_GUI) && !defined(KTE_USE_QT)
static bool
cmd_background_set(const CommandContext &ctx)
{
	std::string mode = ctx.arg;
	// trim
	auto ltrim = [](std::string &s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
			return !std::isspace(ch);
		}));
	};
	auto rtrim = [](std::string &s) {
		s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
			return !std::isspace(ch);
		}).base(), s.end());
	};
	ltrim(mode);
	rtrim(mode);
	std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) {
		return (char) std::tolower(c);
	});
	if (mode.empty()) {
		ctx.editor.SetStatus(std::string("Background: ") + kte::BackgroundModeName());
		return true;
	}
	if (mode != "light" && mode != "dark") {
		ctx.editor.SetStatus("background: expected 'light' or 'dark'");
		return true;
	}
	kte::SetBackgroundMode(mode == "light" ? kte::BackgroundMode::Light : kte::BackgroundMode::Dark);
	// Re-apply current theme to reflect background change
	kte::ApplyThemeByName(kte::CurrentThemeName());
	ctx.editor.SetStatus(std::string("Background: ") + mode + std::string("; Theme: ") + kte::CurrentThemeName());
	return true;
}
#else
static bool
cmd_background_set(CommandContext &ctx)
{
	(void) ctx;
	return true;
}
#endif


static bool
cmd_find_start(const CommandContext &ctx)
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
cmd_regex_find_start(const CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to search");
		return false;
	}

	// Save origin for cancel
	ctx.editor.SetSearchOrigin(buf->Curx(), buf->Cury(), buf->Rowoffs(), buf->Coloffs());

	// Enter regex search mode using the generic prompt system
	ctx.editor.SetSearchActive(true);
	ctx.editor.SetSearchQuery("");
	ctx.editor.SetSearchMatch(0, 0, 0);
	ctx.editor.SetSearchIndex(-1);
	ctx.editor.StartPrompt(Editor::PromptKind::RegexSearch, "Regex", "");
	ctx.editor.SetStatus("Regex: ");
	return true;
}


static bool
cmd_search_replace_start(const CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to search");
		return false;
	}
	// Save original cursor/viewport to restore on cancel
	ctx.editor.SetSearchOrigin(buf->Curx(), buf->Cury(), buf->Rowoffs(), buf->Coloffs());
	// Enter search-highlighting mode for the find step
	ctx.editor.SetSearchActive(true);
	ctx.editor.SetSearchQuery("");
	ctx.editor.SetSearchMatch(0, 0, 0);
	ctx.editor.SetSearchIndex(-1);
	// Two-step prompt: first collect find string, then replacement
	ctx.editor.SetReplaceFindTmp("");
	ctx.editor.SetReplaceWithTmp("");
	ctx.editor.StartPrompt(Editor::PromptKind::ReplaceFind, "Replace: find", "");
	ctx.editor.SetStatus("Replace: find: ");
	return true;
}


static bool
cmd_regex_replace_start(const CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to search");
		return false;
	}
	// Save original cursor/viewport to restore on cancel
	ctx.editor.SetSearchOrigin(buf->Curx(), buf->Cury(), buf->Rowoffs(), buf->Coloffs());
	// Enter search-highlighting mode for the find step (regex)
	ctx.editor.SetSearchActive(true);
	ctx.editor.SetSearchQuery("");
	ctx.editor.SetSearchMatch(0, 0, 0);
	ctx.editor.SetSearchIndex(-1);
	// Two-step prompt: first collect regex find pattern, then replacement
	ctx.editor.SetReplaceFindTmp("");
	ctx.editor.SetReplaceWithTmp("");
	ctx.editor.StartPrompt(Editor::PromptKind::RegexReplaceFind, "Regex replace: find", "");
	ctx.editor.SetStatus("Regex replace: find: ");
	return true;
}


static bool
cmd_open_file_start(const CommandContext &ctx)
{
	// Start a generic prompt to read a path
	ctx.editor.StartPrompt(Editor::PromptKind::OpenFile, "Open", "");
	ctx.editor.SetStatus("Open: ");
	return true;
}


// GUI: toggle visual file picker (no-op in terminal; renderer will consume flag)
static bool
cmd_visual_file_picker_toggle(const CommandContext &ctx)
{
	// Toggle visibility
	bool show = !ctx.editor.FilePickerVisible();
	ctx.editor.SetFilePickerVisible(show);
	if (show) {
		// Initialize directory to current working directory if empty
		if (ctx.editor.FilePickerDir().empty()) {
			try {
				ctx.editor.SetFilePickerDir(safe_current_path().string());
			} catch (...) {
				ctx.editor.SetFilePickerDir(".");
			}
		}
		ctx.editor.SetStatus("Open File (visual)");
	} else {
		ctx.editor.SetStatus("Closed file picker");
	}
	return true;
}


// GUI: request visual font picker (Qt frontend will consume flag)
static bool
cmd_visual_font_picker_toggle(const CommandContext &ctx)
{
#ifdef KTE_BUILD_GUI
	kte::gFontDialogRequested = true;
	ctx.editor.SetStatus("Font chooser");
#else
	ctx.editor.SetStatus("Font chooser not available in terminal");
#endif
	return true;
}


static bool
cmd_jump_to_line_start(const CommandContext &ctx)
{
	// Start a prompt to read a 1-based line number and jump there (clamped)
	ctx.editor.StartPrompt(Editor::PromptKind::GotoLine, "Goto", "");
	ctx.editor.SetStatus("Goto line: ");
	return true;
}


// --- Buffers: switch/next/prev/close ---
static bool
cmd_buffer_switch_start(const CommandContext &ctx)
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
	return {"<untitled>"};
}


static std::string
buffer_basename(const Buffer &b)
{
	const std::string &p = b.Filename();
	if (p.empty())
		return {"<untitled>"};
	auto pos = p.find_last_of("/\\");
	if (pos == std::string::npos)
		return p;
	return p.substr(pos + 1);
}


static bool
cmd_buffer_next(const CommandContext &ctx)
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
cmd_buffer_prev(const CommandContext &ctx)
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
cmd_buffer_close(const CommandContext &ctx)
{
	if (ctx.editor.BufferCount() == 0)
		return true;
	std::size_t idx  = ctx.editor.CurrentBufferIndex();
	Buffer *b        = ctx.editor.CurrentBuffer();
	std::string name = b ? buffer_display_name(*b) : std::string("");
	// If buffer is dirty, prompt to save first (for both named and unnamed buffers)
	if (b && b->Dirty()) {
		ctx.editor.StartPrompt(Editor::PromptKind::Confirm, "Save", "");
		ctx.editor.SetCloseConfirmPending(true);
		ctx.editor.SetStatus(std::string("Save changes to ") + name + "? (y/n, C-g cancel)");
		return true;
	}
	// Otherwise close immediately
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


// Create a new empty, unnamed buffer and switch to it
static bool
cmd_buffer_new(const CommandContext &ctx)
{
	// Create an empty buffer and add it to the editor
	Buffer empty;
	std::size_t idx = ctx.editor.AddBuffer(std::move(empty));
	ctx.editor.SwitchTo(idx);
	ctx.editor.SetStatus("New buffer");
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
		// Special-case: Tab-completion for prompts
		if (ctx.arg == "\t") {
			auto kind = ctx.editor.CurrentPromptKind();
			// Buffer switch prompt supports Tab-completion on buffer names
			if (kind == Editor::PromptKind::BufferSwitch) {
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

			// File path completion for OpenFile/SaveAs/Chdir
			if (kind == Editor::PromptKind::OpenFile || kind == Editor::PromptKind::SaveAs
			    || kind == Editor::PromptKind::Chdir) {
				auto expand_user_path = [](const std::string &in) -> std::string {
					if (!in.empty() && in[0] == '~') {
						const char *home = std::getenv("HOME");
						if (home && in.size() == 1)
							return std::string(home);
						if (home && (in.size() > 1) && (in[1] == '/' || in[1] == '\\')) {
							std::string rest = in.substr(1); // keep leading slash
							return std::string(home) + rest;
						}
					}
					return in;
				};

				std::string text = ctx.editor.PromptText();
				// Build a path and split dir + base prefix
				std::string expanded = expand_user_path(text);
				std::filesystem::path p(expanded);
				std::filesystem::path dir;
				std::string base;
				std::error_code dir_ec;
				if (expanded.empty()) {
					dir = safe_current_path();
					base.clear();
				} else if (std::filesystem::is_directory(p, dir_ec)) {
					dir = p;
					base.clear();
				} else {
					dir  = p.parent_path();
					base = p.filename().string();
					if (dir.empty())
						dir = safe_current_path();
				}

				std::error_code ec;
				std::vector<std::filesystem::directory_entry> entries;
				std::filesystem::directory_iterator it(dir, ec), end;
				for (; !ec && it != end; it.increment(ec)) {
					entries.push_back(*it);
				}
				// Filter by base prefix
				std::vector<std::string> cands;
				for (const auto &de: entries) {
					std::string name = de.path().filename().string();
					if (base.empty() || name.rfind(base, 0) == 0) {
						std::string candidate = (dir / name).string();
						// For dirs, add trailing slash hint
						if (de.is_directory(ec))
							candidate += "/";
						cands.push_back(candidate);
					}
				}
				// If no candidates, keep as-is
				if (cands.empty()) {
					// no-op
				} else if (cands.size() == 1) {
					ctx.editor.SetPromptText(cands[0]);
				} else {
					// Longest common prefix of display strings
					auto lcp = cands[0];
					for (size_t i = 1; i < cands.size(); ++i) {
						const auto &s = cands[i];
						size_t j      = 0;
						while (j < lcp.size() && j < s.size() && lcp[j] == s[j])
							++j;
						lcp.resize(j);
						if (lcp.empty())
							break;
					}
					if (!lcp.empty() && lcp != ctx.editor.PromptText()) {
						ctx.editor.SetPromptText(lcp);
					} else {
						// Show some choices in status (trim to avoid spam)
						std::string msg = ctx.editor.PromptLabel() + ": ";
						size_t shown    = 0;
						for (const auto &s: cands) {
							if (shown >= 10) {
								msg += " …";
								break;
							}
							if (shown > 0)
								msg += ' ';
							msg += std::filesystem::path(s).filename().string();
							++shown;
						}
						ctx.editor.SetStatus(msg);
						return true;
					}
				}
				ctx.editor.SetStatus(ctx.editor.PromptLabel() + ": " + ctx.editor.PromptText());
				return true;
			}

			// Generic command prompt completion
			if (kind == Editor::PromptKind::Command) {
				std::string text = ctx.editor.PromptText();
				// Split into command and arg prefix
				auto sp = text.find(' ');
				if (sp == std::string::npos) {
					// complete command name from public commands
					std::string prefix = text;
					std::vector<std::string> names;
					for (const auto &c: CommandRegistry::All()) {
						if (c.isPublic) {
							if (prefix.empty() || c.name.rfind(prefix, 0) == 0)
								names.push_back(c.name);
						}
					}
					if (names.empty()) {
						// no change
					} else if (names.size() == 1) {
						ctx.editor.SetPromptText(names[0]);
					} else {
						// compute LCP
						std::string lcp = names[0];
						for (size_t i = 1; i < names.size(); ++i) {
							const std::string &s = names[i];
							size_t j             = 0;
							while (j < lcp.size() && j < s.size() && lcp[j] == s[j])
								++j;
							lcp.resize(j);
							if (lcp.empty())
								break;
						}
						if (!lcp.empty() && lcp != text)
							ctx.editor.SetPromptText(lcp);
					}
					ctx.editor.SetStatus(std::string(": ") + ctx.editor.PromptText());
					return true;
				} else {
					std::string cmd       = text.substr(0, sp);
					std::string argprefix = text.substr(sp + 1);
					// Only special-case argument completion for certain commands
					if (cmd == "theme") {
#if defined(KTE_BUILD_GUI)
#  if !defined(KTE_USE_QT)
						std::vector<std::string> cands;
						const auto &reg = kte::ThemeRegistry();
						for (const auto &t: reg) {
							std::string n = t->Name();
							if (argprefix.empty() || n.rfind(argprefix, 0) == 0)
								cands.push_back(n);
						}
#  else
						// Qt: offer known theme names handled by ApplyQtThemeByName
						static const char *qt_themes[] = {
							"nord",
							"solarized-dark",
							"solarized-light",
							"gruvbox-dark",
							"gruvbox-light",
							"eink"
						};
						std::vector<std::string> cands;
						for (const char *t: qt_themes) {
							std::string n(t);
							if (argprefix.empty() || n.rfind(argprefix, 0) == 0)
								cands.push_back(n);
						}
#  endif
						if (cands.empty()) {
							// no change
						} else if (cands.size() == 1) {
							ctx.editor.SetPromptText(cmd + std::string(" ") + cands[0]);
						} else {
							std::string lcp = cands[0];
							for (size_t i = 1; i < cands.size(); ++i) {
								const std::string &s = cands[i];
								size_t j             = 0;
								while (j < lcp.size() && j < s.size() && lcp[j] == s[j])
									++j;
								lcp.resize(j);
								if (lcp.empty())
									break;
							}
							if (!lcp.empty() && lcp != argprefix)
								ctx.editor.SetPromptText(cmd + std::string(" ") + lcp);
						}
						ctx.editor.SetStatus(std::string(": ") + ctx.editor.PromptText());
						return true;
#else
						(void) argprefix; // no completion in non-GUI build
#endif
					}
					if (cmd == "font") {
						std::vector<std::string> cands;
						std::string apfx_lower = argprefix;
						std::transform(apfx_lower.begin(), apfx_lower.end(), apfx_lower.begin(),
						               [](unsigned char c) {
							               return (char) std::tolower(c);
						               });
#if defined(KTE_BUILD_GUI) && defined(KTE_USE_QT)
						// Qt: complete against system font families
						QStringList fams = QFontDatabase::families();
						for (const auto &fam: fams) {
							std::string n      = fam.toStdString();
							std::string nlower = n;
							std::transform(nlower.begin(), nlower.end(), nlower.begin(),
							               [](unsigned char c) {
								               return (char) std::tolower(c);
							               });
							if (apfx_lower.empty() || nlower.rfind(apfx_lower, 0) == 0)
								cands.push_back(n);
						}
#elif defined(KTE_BUILD_GUI)
						// ImGui: complete against embedded font registry
						for (const auto &n : kte::Fonts::FontRegistry::Instance().FontNames()) {
							if (apfx_lower.empty() || n.rfind(apfx_lower, 0) == 0)
								cands.push_back(n);
						}
#endif
						if (cands.empty()) {
							// no change
						} else if (cands.size() == 1) {
							ctx.editor.SetPromptText(cmd + std::string(" ") + cands[0]);
						} else {
							std::string lcp = cands[0];
							for (size_t i = 1; i < cands.size(); ++i) {
								const std::string &s = cands[i];
								size_t j             = 0;
								while (j < lcp.size() && j < s.size() && lcp[j] == s[j])
									++j;
								lcp.resize(j);
								if (lcp.empty())
									break;
							}
							if (!lcp.empty() && lcp != argprefix)
								ctx.editor.SetPromptText(cmd + std::string(" ") + lcp);
						}
						ctx.editor.SetStatus(std::string(": ") + ctx.editor.PromptText());
						return true;
					}
					if (cmd == "mode") {
						std::vector<std::string> modes = {"code", "writing"};
						std::vector<std::string> cands;
						for (const auto &m : modes) {
							if (argprefix.empty() || m.rfind(argprefix, 0) == 0)
								cands.push_back(m);
						}
						if (cands.size() == 1) {
							ctx.editor.SetPromptText(cmd + std::string(" ") + cands[0]);
						}
						ctx.editor.SetStatus(std::string(": ") + ctx.editor.PromptText());
						return true;
					}
					// default: no special arg completion
					ctx.editor.SetStatus(std::string(": ") + ctx.editor.PromptText());
					return true;
				}
			}
		}

		ctx.editor.AppendPromptText(ctx.arg);
		// If it's a search prompt, mirror text to search state
		if (ctx.editor.CurrentPromptKind() == Editor::PromptKind::Search ||
		    ctx.editor.CurrentPromptKind() == Editor::PromptKind::RegexSearch ||
		    ctx.editor.CurrentPromptKind() == Editor::PromptKind::ReplaceFind ||
		    ctx.editor.CurrentPromptKind() == Editor::PromptKind::RegexReplaceFind) {
			ctx.editor.SetSearchQuery(ctx.editor.PromptText());
			run_search(ctx.editor, *buf, regex_prompt(ctx.editor), SearchStep::Here, true);
		} else {
			// For other prompts, just echo label:text in status
			ctx.editor.SetStatus(ctx.editor.PromptLabel() + ": " + ctx.editor.PromptText());
		}
		return true;
	}
	// If in search mode, treat printable input as query update
	if (ctx.editor.SearchActive()) {
		std::string q = ctx.editor.SearchQuery();
		q             += ctx.arg; // arg already printable text
		ctx.editor.SetSearchQuery(q);

		run_search(ctx.editor, *buf, false, SearchStep::Here, true);
		return true;
	}
	// Disallow newlines in InsertText; they should come via Newline
	if (ctx.arg.find('\n') != std::string::npos || ctx.arg.find('\r') != std::string::npos) {
		ctx.editor.SetStatus("InsertText arg must not contain newlines");
		return false;
	}
	ensure_at_least_one_line(*buf);
	std::size_t y  = buf->Cury();
	std::size_t x  = buf->Curx();
	int repeat     = ctx.count > 0 ? ctx.count : 1;
	std::size_t cx = x;
	std::size_t cy = y;
	{
		constexpr std::size_t kMaxInsertBytes = std::size_t{256} << 20;
		const std::size_t lines = buf->VisualLineActive() ? (buf->VisualLineEndY() - buf->VisualLineStartY() + 1) : 1;
		if (ctx.arg.size() * static_cast<std::size_t>(repeat) * lines > kMaxInsertBytes) {
			ctx.editor.SetStatus("Insert too large");
			return false;
		}
	}

	// Visual-line mode: broadcast inserts to each selected line at the same column.
	if (buf->VisualLineActive()) {
		const std::size_t sy = buf->VisualLineStartY();
		const std::size_t ey = buf->VisualLineEndY();
		const auto &rows     = rows_of(*buf);
		UndoSystem *u        = buf->Undo();
		std::uint64_t gid    = 0;
		if (u)
			gid = u->BeginGroup();
		(void) gid;

		std::string ins;
		if (repeat == 1) {
			ins = ctx.arg;
		} else {
			ins.reserve(ctx.arg.size() * static_cast<std::size_t>(repeat));
			for (int i = 0; i < repeat; ++i)
				ins += ctx.arg;
		}
		const std::size_t xchars = y < rows.size() ? utf8_char_count(line_view(rows[y]), x) : x;
		transform_rows(*buf, sy, ey, u, [&](std::string &line, const std::size_t yy) {
			const std::size_t xx = utf8_byte_of_char(line, xchars);
			line.insert(xx, ins);
			if (yy == y) {
				cx = xx + ins.size();
				cy = yy;
			}
		});
		if (u)
			u->EndGroup();
		buf->SetDirty(true);
		buf->SetCursor(cx, cy);
		ensure_cursor_visible(ctx.editor, *buf);
		return true;
	}

	UndoSystem *u = buf->Undo();
	if (u) {
		// Start/extend a typed-run batch. Do NOT commit here; commit happens on boundaries
		// (cursor movement, prompts, undo/redo, etc.) so consecutive InsertText commands coalesce.
		buf->SetCursor(x, y);
		u->Begin(UndoType::Insert);
	}
	// Apply edits to the underlying PieceTable through Buffer::insert_text,
	// not directly to the legacy rows_ cache. This ensures Save() persists text.
	// A repeat count inserts the repeated text in one edit (one edit per
	// repetition made C-u 1000000 x take minutes on a large file).
	std::string ins;
	if (repeat == 1) {
		ins = ctx.arg;
	} else {
		ins.reserve(ctx.arg.size() * static_cast<std::size_t>(repeat));
		for (int i = 0; i < repeat; ++i)
			ins += ctx.arg;
	}
	buf->insert_text(static_cast<int>(y), static_cast<int>(x), std::string_view(ins));
	if (u)
		u->Append(std::string_view(ins));
	x += ins.size();
	buf->SetDirty(true);
	buf->SetCursor(x, y);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


// Toggle read-only state of the current buffer
static bool
cmd_toggle_read_only(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer");
		return false;
	}
	buf->ToggleReadOnly();
	ctx.editor.SetStatus(std::string("Read-only: ") + (buf->IsReadOnly() ? "ON" : "OFF"));
	return true;
}


// Open or refresh the +HELP+ buffer with content from docs/kte.1
static bool
cmd_show_help(CommandContext &ctx)
{
	const std::string help_name = "+HELP+";
	// Try to locate existing +HELP+ buffer
	std::vector<Buffer> &bufs = ctx.editor.Buffers();
	std::size_t help_index    = static_cast<std::size_t>(-1);
	for (std::size_t i = 0; i < bufs.size(); ++i) {
		if (bufs[i].Filename() == help_name && !bufs[i].IsFileBacked()) {
			help_index = i;
			break;
		}
	}

	auto roff_to_text = [](const std::string &in) -> std::string {
		std::istringstream iss(in);
		std::ostringstream out;
		std::string line;
		auto unquote = [](std::string s) {
			if (!s.empty() && (s.front() == '"' || s.front() == '\''))
				s.erase(s.begin());
			if (!s.empty() && (s.back() == '"' || s.back() == '\''))
				s.pop_back();
			return s;
		};
		while (std::getline(iss, line)) {
			if (line.rfind("'", 0) == 0) {
				continue; // comment line
			}
			if (line.rfind(".", 0) == 0) {
				// Macro line
				std::istringstream ls(line);
				std::string dot, macro;
				ls >> dot >> macro;
				if (macro == "TH" || macro == "SH") {
					std::string title;
					std::getline(ls, title);
					// trim leading spaces
					while (!title.empty() && (title.front() == ' ' || title.front() == '\t'))
						title.erase(title.begin());
					title = unquote(title);
					out << "\n\n";
					for (auto &c: title)
						c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
					out << title << "\n";
				} else if (macro == "PP" || macro == "P" || macro == "TP") {
					out << "\n";
				} else if (macro == "B" || macro == "I" || macro == "BR" || macro == "IR") {
					std::string rest;
					std::getline(ls, rest);
					while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
						rest.erase(rest.begin());
					out << unquote(rest) << "\n";
				} else if (macro == "nf" || macro == "fi") {
					// ignore fill mode toggles for now
				} else {
					// Unhandled macro: ignore
				}
				continue;
			}
			// Regular text; apply minimal escape replacements
			for (std::size_t i = 0; i < line.size(); ++i) {
				if (line[i] == '\\') {
					if (i + 1 < line.size() && line[i + 1] == '-') {
						out << '-';
						++i;
						continue;
					}
					if (i + 3 < line.size() && line[i + 1] == '(') {
						std::string esc = line.substr(i + 2, 2);
						if (esc == "em") {
							out << "—";
							i += 3;
							continue;
						}
						if (esc == "en") {
							out << "-";
							i += 3;
							continue;
						}
					}
				}
				out << line[i];
			}
			out << "\n";
		}
		return out.str();
	};

	auto load_help_text = [&](bool &used_man) -> std::string {
		// 1) Prefer embedded/customizable help content
		{
			std::string embedded = HelpText::Text();
			if (!embedded.empty()) {
				used_man = false;
				return embedded;
			}
		}

		// 2) Fall back to the manpage and convert roff to plain text
		const char *man_candidates[] = {
			"docs/kte.1",
			"./docs/kte.1",
			"/usr/local/share/man/man1/kte.1",
			"/usr/share/man/man1/kte.1"
		};
		for (const char *p: man_candidates) {
			std::ifstream in(p);
			if (in.good()) {
				std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				if (!s.empty()) {
					used_man = true;
					return roff_to_text(s);
				}
			}
		}
		// Fallback minimal help text
		used_man = false;
		return std::string(
			"KTE - Kyle's Text Editor\n\n"
			"About:\n"
			"  kte is Kyle's Text Editor and is probably ill-suited to everyone else. It was\n"
			"  inspired by Antirez' kilo text editor by way of someone's writeup of the\n"
			"  process of writing a text editor from scratch. It has keybindings inspired by\n"
			"  VDE (and the Wordstar family) and emacs; its spiritual parent is mg(1).\n\n"
			"Core keybindings:\n"
			"  C-k h        Show this help\n"
			"  C-k s        Save buffer\n"
			"  C-k x        Save and quit\n"
			"  C-k q        Quit (confirm if dirty)\n"
			"  C-k C-q      Quit now (no confirm)\n"
			"  C-k c        Close current buffer\n"
			"  C-k b        Switch buffer\n"
			"  C-k p        Next buffer\n"
			"  C-k n        Previous buffer\n"
			"  C-k e        Open file (prompt)\n"
			"  C-k g        Jump to line\n"
			"  C-k u        Undo\n"
			"  C-k r        Redo\n"
			"  C-k d        Kill to end of line\n"
			"  C-k C-d      Kill entire line\n"
			"  C-k =        Indent region\n"
			"  C-k -        Unindent region\n"
			"  C-k '        Toggle read-only\n"
			"  C-k l        Reload buffer from disk\n"
			"  C-k a        Mark all and jump to end\n"
			"  C-k v        Toggle visual file picker (GUI)\n"
			"  C-k w        Show working directory\n"
			"  C-k o        Change working directory (prompt)\n\n"
			"ESC/Alt commands:\n"
			"  ESC q        Reflow paragraph\n"
			"  ESC BACKSPACE Delete previous word\n"
			"  ESC d        Delete next word\n"
			"  Alt-w        Copy region to kill ring\n\n"
			"Buffers:\n  +HELP+ is read-only. Press C-k ' to toggle if you need to edit; C-k h restores it.\n");
	};

	auto populate_from_text = [](Buffer &b, const std::string &text) {
		// Clear existing content
		b.replace_all_bytes("");
		// Parse text and insert rows
		std::string line;
		line.reserve(128);
		int row_idx = 0;
		for (char ch: text) {
			if (ch == '\n') {
				b.insert_row(row_idx++, line);
				line.clear();
			} else if (ch != '\r') {
				line.push_back(ch);
			}
		}
		// Add last line (even if empty)
		b.insert_row(row_idx, line);
		b.SetDirty(false);
		b.SetCursor(0, 0);
		b.SetOffsets(0, 0);
		b.SetRenderX(0);
	};

	if (help_index != static_cast<std::size_t>(-1)) {
		Buffer &hb = bufs[help_index];
		// If dirty, overwrite with original contents
		if (hb.Dirty()) {
			bool used_man    = false;
			std::string text = load_help_text(used_man);
			populate_from_text(hb, text);
		}
		hb.SetReadOnly(true);
		ctx.editor.SwitchTo(help_index);
		ctx.editor.SetStatus("Help opened");
		return true;
	}

	// Create a new help buffer
	Buffer help;
	help.SetVirtualName(help_name);
	bool used_man    = false;
	std::string text = load_help_text(used_man);
	populate_from_text(help, text);
	help.SetReadOnly(true);
	std::size_t idx = ctx.editor.AddBuffer(std::move(help));
	ctx.editor.SwitchTo(idx);
	ctx.editor.SetStatus("Help opened");
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
		if (kind == Editor::PromptKind::Command) {
			// Parse COMMAND ARG and dispatch only public commands
			// Trim leading/trailing spaces
			auto ltrim = [](std::string &s) {
				s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
					return !std::isspace(ch);
				}));
			};
			auto rtrim = [](std::string &s) {
				s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
					return !std::isspace(ch);
				}).base(), s.end());
			};
			ltrim(value);
			rtrim(value);
			if (value.empty()) {
				ctx.editor.SetStatus("Canceled");
				return true;
			}
			// Split first token
			std::string cmdname;
			std::string arg;
			auto sp = value.find(' ');
			if (sp == std::string::npos) {
				cmdname = value;
			} else {
				cmdname = value.substr(0, sp);
				arg     = value.substr(sp + 1);
			}
			const Command *cmd = CommandRegistry::FindByName(cmdname);
			if (!cmd || !cmd->isPublic) {
				ctx.editor.SetStatus(std::string("Unknown command: ") + cmdname);
				return true;
			}
			bool ok = Execute(ctx.editor, cmdname, arg);
			if (!ok) {
				ctx.editor.SetStatus(std::string("Command failed: ") + cmdname);
			}
			return true;
		}
		if (kind == Editor::PromptKind::Search || kind == Editor::PromptKind::RegexSearch) {
			// Finish search: keep cursor where it is, clear search UI prompt
			ctx.editor.SetSearchActive(false);
			ctx.editor.SetSearchMatch(0, 0, 0);
			ctx.editor.ClearSearchOrigin();
			ctx.editor.SetStatus(kind == Editor::PromptKind::RegexSearch ? "Regex find done" : "Find done");
			Buffer *b = ctx.editor.CurrentBuffer();
			if (b)
				ensure_cursor_visible(ctx.editor, *b);
		} else if (kind == Editor::PromptKind::ReplaceFind) {
			// Proceed to replacement text prompt
			ctx.editor.SetReplaceFindTmp(value);
			// Keep search highlights active using the collected find string
			ctx.editor.SetSearchActive(true);
			ctx.editor.SetSearchQuery(value);
			if (Buffer *b = ctx.editor.CurrentBuffer())
				run_search(ctx.editor, *b, false, SearchStep::Here, false);
			ctx.editor.StartPrompt(Editor::PromptKind::ReplaceWith, "Replace: with", "");
			ctx.editor.SetStatus("Replace: with: ");
			return true;
		} else if (kind == Editor::PromptKind::ReplaceWith) {
			// Execute replace-all
			Buffer *buf = ctx.editor.CurrentBuffer();
			if (!buf)
				return false;
			if (buf->IsReadOnly()) {
				ctx.editor.SetStatus("Read-only buffer");
				// Clear search UI state
				ctx.editor.SetSearchActive(false);
				ctx.editor.SetSearchQuery("");
				ctx.editor.SetSearchMatch(0, 0, 0);
				ctx.editor.ClearSearchOrigin();
				ctx.editor.SetSearchIndex(-1);
				return true;
			}
			const std::string find = ctx.editor.ReplaceFindTmp();
			const std::string with = value;
			ctx.editor.SetReplaceWithTmp(with);
			if (find.empty()) {
				ctx.editor.SetStatus("Replace canceled (empty find)");
				// Clear search UI state
				ctx.editor.SetSearchActive(false);
				ctx.editor.SetSearchQuery("");
				ctx.editor.SetSearchMatch(0, 0, 0);
				ctx.editor.ClearSearchOrigin();
				ctx.editor.SetSearchIndex(-1);
				return true;
			}
			// Save original cursor to restore after operations
			std::size_t orig_x = buf->Curx();
			std::size_t orig_y = buf->Cury();
			std::size_t total  = 0;
			UndoSystem *u      = buf->Undo();
			if (u)
				u->commit(); // end any pending batch
			if (!find.empty()) {
				// Build the replaced text in one pass and apply it as a single
				// edit: one piece-table edit per match cost O(file) each, so
				// replace-all was quadratic (298 s for 249k matches in 12 MB).
				const std::string before = buffer_text(*buf);
				std::string after;
				after.reserve(before.size());
				std::size_t pos = 0;
				while (true) {
					// Matches never span lines: the prompt text has no newline.
					const std::size_t p = before.find(find, pos);
					if (p == std::string::npos)
						break;
					after.append(before, pos, p - pos);
					after += with;
					pos = p + find.size();
					++total;
				}
				after.append(before, pos, std::string::npos);
				if (total > 0)
					replace_buffer_text(*buf, before, after, u);
			}
			if (total > 0)
				buf->SetDirty(true);
			// Restore original cursor, clamped: replacements may have shortened the line.
			if (orig_y < buf->Nrows())
				buf->SetCursor(orig_x, orig_y);
			clamp_cursor_to_buffer(*buf);
			ensure_cursor_visible(ctx.editor, *buf);
			char msg[128];
			std::snprintf(msg, sizeof(msg), "Replaced %zu occurrence%s", total, (total == 1 ? "" : "s"));
			ctx.editor.SetStatus(msg);
			// Clear search-highlighting state after replace completes
			ctx.editor.SetSearchActive(false);
			ctx.editor.SetSearchQuery("");
			ctx.editor.SetSearchMatch(0, 0, 0);
			ctx.editor.ClearSearchOrigin();
			ctx.editor.SetSearchIndex(-1);
			return true;
		} else if (kind == Editor::PromptKind::OpenFile) {
			// Expand "~" to the user's home directory
			auto expand_user_path = [](const std::string &in) -> std::string {
				if (!in.empty() && in[0] == '~') {
					const char *home = std::getenv("HOME");
					if (home && in.size() == 1)
						return std::string(home);
					if (home && (in.size() > 1) && (in[1] == '/' || in[1] == '\\')) {
						std::string rest = in.substr(1);
						return std::string(home) + rest;
					}
				}
				return in;
			};
			value = expand_user_path(value);
			if (value.empty()) {
				ctx.editor.SetStatus("Open canceled (empty)");
			} else {
				ctx.editor.RequestOpenFile(value);
				const bool opened = ctx.editor.ProcessPendingOpens();
				if (ctx.editor.PromptActive()) {
					// A recovery confirmation prompt was started.
					return true;
				}
				if (opened) {
					// Center the view on the cursor (e.g. if the buffer restored a cursor position)
					cmd_center_on_cursor(ctx);
					// Close the prompt so subsequent typing edits the buffer, not the prompt
					ctx.editor.CancelPrompt();
				}
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
					// Expand "~" for save path
					auto expand_user_path = [](const std::string &in) -> std::string {
						if (!in.empty() && in[0] == '~') {
							const char *home = std::getenv("HOME");
							if (home && in.size() == 1)
								return std::string(home);
							if (home && (in.size() > 1) && (
								    in[1] == '/' || in[1] == '\\')) {
								std::string rest = in.substr(1);
								return std::string(home) + rest;
							}
						}
						return in;
					};
					value = expand_user_path(value);
					// Ask before overwriting any existing file other than the
					// buffer's own (it used to ask only for unnamed buffers).
					if (open_in_other_buffer(ctx.editor, value)) {
						ctx.editor.SetStatus(value + " is open in another buffer; save or close that buffer instead");
					} else if (fs_exists(value) && !is_buffers_own_file(*buf, value)) {
						ctx.editor.StartPrompt(Editor::PromptKind::Confirm, "Overwrite", "");
						ctx.editor.SetPendingOverwritePath(value);
						ctx.editor.SetStatus(
							std::string("Overwrite existing file '") + value + "'? (y/N)");
					} else {
						std::string err;
						if (!buf->SaveAs(value, err)) {
							ctx.editor.SetStatus(err);
						} else {
							buf->SetDirty(false);
							if (auto *sm = ctx.editor.Swap()) {
								sm->NotifyFilenameChanged(*buf);
								sm->ResetJournal(*buf);
							}
							ctx.editor.SetStatus("Saved as " + value);
							if (auto *u = buf->Undo())
								u->mark_saved();
							// Close the prompt on successful save-as
							ctx.editor.CancelPrompt();
							// If a close-after-save was requested (from closing a dirty, unnamed buffer),
							// close the buffer now.
							if (ctx.editor.CloseAfterSave()) {
								ctx.editor.SetCloseAfterSave(false);
								std::size_t idx_close = ctx.editor.CurrentBufferIndex();
								std::string name_close = buffer_display_name(*buf);
								if (buf->Undo())
									buf->Undo()->discard_pending();
								ctx.editor.CloseBuffer(idx_close);
								if (ctx.editor.BufferCount() == 0) {
									Buffer empty;
									ctx.editor.AddBuffer(std::move(empty));
									ctx.editor.SwitchTo(0);
								}
								const Buffer *cur = ctx.editor.CurrentBuffer();
								ctx.editor.SetStatus(
									std::string("Closed: ") + name_close +
									std::string("  Now: ")
									+ (cur
										   ? buffer_display_name(*cur)
										   : std::string("")));
							}
						}
					}
				}
			}
		} else if (kind == Editor::PromptKind::Confirm) {
			// Confirmation for potentially destructive operations (e.g., overwrite on save-as)
			Buffer *buf              = ctx.editor.CurrentBuffer();
			const std::string target = ctx.editor.PendingOverwritePath();
			if (!target.empty() && buf) {
				bool yes = false;
				if (!value.empty()) {
					char c = value[0];
					yes    = (c == 'y' || c == 'Y');
				}
				if (yes) {
					std::string err;
					const bool is_same_target = (buf->Filename() == target) && buf->IsFileBacked();
					const bool ok = is_same_target ? buf->Save(err) : buf->SaveAs(target, err);
					if (!ok) {
						ctx.editor.SetStatus(err);
					} else {
						buf->SetDirty(false);
						if (auto *sm = ctx.editor.Swap()) {
							if (!is_same_target)
								sm->NotifyFilenameChanged(*buf);
							sm->ResetJournal(*buf);
						}
						ctx.editor.SetStatus(
							is_same_target ? ("Saved " + target) : ("Saved as " + target));
						if (auto *u = buf->Undo())
							u->mark_saved();
						// If this overwrite confirm was part of a close-after-save flow, close now.
						if (ctx.editor.CloseAfterSave()) {
							ctx.editor.SetCloseAfterSave(false);
							std::size_t idx_close  = ctx.editor.CurrentBufferIndex();
							std::string name_close = buffer_display_name(*buf);
							if (buf->Undo())
								buf->Undo()->discard_pending();
							ctx.editor.CloseBuffer(idx_close);
							if (ctx.editor.BufferCount() == 0) {
								Buffer empty;
								ctx.editor.AddBuffer(std::move(empty));
								ctx.editor.SwitchTo(0);
							}
							const Buffer *cur = ctx.editor.CurrentBuffer();
							ctx.editor.SetStatus(
								std::string("Closed: ") + name_close + std::string(
									"  Now: ")
								+ (cur ? buffer_display_name(*cur) : std::string("")));
						}
						// Close the prompt after successful confirmation
						ctx.editor.CancelPrompt();
					}
				} else {
					ctx.editor.SetStatus("Save canceled");
					// Close the prompt after negative confirmation
					ctx.editor.CancelPrompt();
				}
				ctx.editor.ClearPendingOverwritePath();
				// Regardless of answer, end any close-after-save pending state for safety.
				ctx.editor.SetCloseAfterSave(false);
			} else if (ctx.editor.PendingRecoveryPrompt() != Editor::RecoveryPromptKind::None) {
				const char c = value.empty() ? '\0' : value[0];
				const bool yes = (c == 'y' || c == 'Y');
				const bool no  = (c == 'n' || c == 'N');
				// "Discard" deletes the crash journal, so it takes an explicit
				// 'n'. Enter or any other key (e.g. typing that was meant for the
				// buffer when the prompt appeared) cancels, keeping the journal.
				if (!yes && !no &&
				    ctx.editor.PendingRecoveryPrompt() == Editor::RecoveryPromptKind::RecoverOrDiscard) {
					ctx.editor.CancelRecoveryPrompt();
					ctx.editor.CancelPrompt();
					ctx.editor.SetStatus("Recovery canceled; swap file kept (answer y or n)");
					return true;
				}
				(void) ctx.editor.ResolveRecoveryPrompt(yes);
				ctx.editor.CancelPrompt();
				// Continue any queued opens (e.g., startup argv files).
				ctx.editor.ProcessPendingOpens();
			} else if (ctx.editor.CloseConfirmPending() && buf) {
				const char c = value.empty() ? '\0' : value[0];
				const bool yes = (c == 'y' || c == 'Y');
				// Closing without saving discards the edits (and their journal),
				// so it takes an explicit 'n'; anything else cancels the close.
				if (!yes && c != 'n' && c != 'N') {
					ctx.editor.SetCloseConfirmPending(false);
					ctx.editor.SetStatus("Close canceled (answer y or n)");
					return true;
				}
				// Saving here must not silently overwrite changes made on disk.
				if (yes && buf->IsFileBacked() && buf->ExternallyModifiedOnDisk()) {
					ctx.editor.SetCloseConfirmPending(false);
					ctx.editor.SetStatus("File changed on disk; not saved or closed. Save with C-k s to confirm overwriting");
					return true;
				}
				// Prepare close details
				std::size_t idx_close  = ctx.editor.CurrentBufferIndex();
				std::string name_close = buffer_display_name(*buf);
				bool proceed_to_close  = true;
				if (yes) {
					std::string err;
					if (buf->IsFileBacked()) {
						if (!buf->Save(err)) {
							ctx.editor.SetStatus(err);
							proceed_to_close = false;
						} else {
							buf->SetDirty(false);
							if (auto *sm = ctx.editor.Swap())
								sm->ResetJournal(*buf);
							if (auto *u = buf->Undo())
								u->mark_saved();
						}
					} else if (!buf->Filename().empty() && !buf->IsVirtual()) {
						if (!buf->SaveAs(buf->Filename(), err)) {
							ctx.editor.SetStatus(err);
							proceed_to_close = false;
						} else {
							buf->SetDirty(false);
							if (auto *sm = ctx.editor.Swap()) {
								sm->NotifyFilenameChanged(*buf);
								sm->ResetJournal(*buf);
							}
							if (auto *u = buf->Undo())
								u->mark_saved();
						}
					} else {
						// No filename; fall back to Save As flow and set close-after-save
						ctx.editor.StartPrompt(Editor::PromptKind::SaveAs, "Save as", "");
						ctx.editor.SetCloseAfterSave(true);
						ctx.editor.SetStatus("Save as: ");
						ctx.editor.SetCloseConfirmPending(false);
						return true;
					}
				}
				if (proceed_to_close) {
					if (buf->Undo())
						buf->Undo()->discard_pending();
					ctx.editor.CloseBuffer(idx_close);
					if (ctx.editor.BufferCount() == 0) {
						Buffer empty;
						ctx.editor.AddBuffer(std::move(empty));
						ctx.editor.SwitchTo(0);
					}
					const Buffer *cur = ctx.editor.CurrentBuffer();
					ctx.editor.SetStatus(
						std::string("Closed: ") + name_close + std::string("  Now: ")
						+ (cur ? buffer_display_name(*cur) : std::string("")));
				}
				ctx.editor.SetCloseConfirmPending(false);
			} else {
				ctx.editor.SetStatus("Nothing to confirm");
			}
		} else if (kind == Editor::PromptKind::GotoLine) {
			Buffer *buf = ctx.editor.CurrentBuffer();
			if (!buf) {
				ctx.editor.SetStatus("No buffer");
				return true;
			}
			if (auto *u = buf->Undo())
				u->commit();
			std::size_t nrows = buf->Nrows();
			if (nrows == 0) {
				buf->SetCursor(0, 0);
				ensure_cursor_visible(ctx.editor, *buf);
				ctx.editor.SetStatus("Empty buffer");
				return true;
			}
			// Parse 1-based line number; on failure, keep cursor and show status
			std::size_t line1 = 0;
			try {
				if (!value.empty())
					line1 = static_cast<std::size_t>(std::stoull(value));
			} catch (...) {
				line1 = 0;
			}
			if (line1 == 0) {
				ctx.editor.SetStatus("Goto canceled (invalid line)");
				return true;
			}
			std::size_t y = line1 - 1; // convert to 0-based
			if (y >= nrows)
				y = nrows - 1; // clamp to last line
			buf->SetCursor(0, y);
			ensure_cursor_visible(ctx.editor, *buf);
			ctx.editor.SetStatus("Goto line " + std::to_string(line1));
		} else if (kind == Editor::PromptKind::Chdir) {
			// Attempt to change the current working directory
			if (value.empty()) {
				ctx.editor.SetStatus("chdir canceled (empty)");
				return true;
			}
			try {
				// Expand "~" for chdir
				auto expand_user_path = [](const std::string &in) -> std::string {
					if (!in.empty() && in[0] == '~') {
						const char *home = std::getenv("HOME");
						if (home && in.size() == 1)
							return std::string(home);
						if (home && (in.size() > 1) && (in[1] == '/' || in[1] == '\\')) {
							std::string rest = in.substr(1);
							return std::string(home) + rest;
						}
					}
					return in;
				};
				value = expand_user_path(value);
				std::filesystem::path p(value);
				std::error_code ec;
				// Expand if value is relative: resolve against current_path implicitly
				if (!std::filesystem::exists(p, ec)) {
					ctx.editor.SetStatus(std::string("chdir: no such path: ") + value);
					return true;
				}
				if (!std::filesystem::is_directory(p, ec)) {
					ctx.editor.SetStatus(std::string("chdir: not a directory: ") + value);
					return true;
				}
				std::filesystem::current_path(p);
				ctx.editor.SetStatus(std::string("cwd: ") + std::filesystem::current_path().string());
			} catch (const std::exception &e) {
				ctx.editor.SetStatus(std::string("chdir failed: ") + e.what());
			}
		} else if (kind == Editor::PromptKind::RegexReplaceFind) {
			// Proceed to regex replacement text prompt
			ctx.editor.SetReplaceFindTmp(value);
			// Keep search highlights active using the collected regex pattern
			ctx.editor.SetSearchActive(true);
			ctx.editor.SetSearchQuery(value);
			if (Buffer *b = ctx.editor.CurrentBuffer())
				run_search(ctx.editor, *b, true, SearchStep::Here, false);
			ctx.editor.StartPrompt(Editor::PromptKind::RegexReplaceWith, "Regex replace: with", "");
			ctx.editor.SetStatus("Regex replace: with: ");
			return true;
		} else if (kind == Editor::PromptKind::RegexReplaceWith) {
			// Execute regex replace-all
			Buffer *buf = ctx.editor.CurrentBuffer();
			if (!buf)
				return false;
			if (buf->IsReadOnly()) {
				ctx.editor.SetStatus("Read-only buffer");
				// Clear search UI state
				ctx.editor.SetSearchActive(false);
				ctx.editor.SetSearchQuery("");
				ctx.editor.SetSearchMatch(0, 0, 0);
				ctx.editor.ClearSearchOrigin();
				ctx.editor.SetSearchIndex(-1);
				return true;
			}
			const std::string patt = ctx.editor.ReplaceFindTmp();
			const std::string repl = value;
			ctx.editor.SetReplaceWithTmp(repl);
			if (patt.empty()) {
				ctx.editor.SetStatus("Regex replace canceled (empty pattern)");
				ctx.editor.SetSearchActive(false);
				ctx.editor.SetSearchQuery("");
				ctx.editor.SetSearchMatch(0, 0, 0);
				ctx.editor.ClearSearchOrigin();
				ctx.editor.SetSearchIndex(-1);
				return true;
			}
			kte::Regex rx;
			std::string rx_err;
			if (!rx.Compile(patt, rx_err)) {
				ctx.editor.SetStatus("Regex error: " + rx_err);
				// Clear search UI state
				ctx.editor.SetSearchActive(false);
				ctx.editor.SetSearchQuery("");
				ctx.editor.SetSearchMatch(0, 0, 0);
				ctx.editor.ClearSearchOrigin();
				ctx.editor.SetSearchIndex(-1);
				return true;
			}
			std::size_t changed = 0;
			UndoSystem *ru      = buf->Undo();
			UndoGroupGuard rguard(ru);
			// When the buffer ends with '\n', the last row is the empty position
			// after it, not a line; a zero-width pattern like ^ must not match it.
			std::size_t nrows = buf->Nrows();
			if (nrows > 1 && buf->GetLineString(nrows - 1).empty())
				--nrows;
			// Build the new text line by line and apply it as one edit (one
			// edit per changed line was quadratic on big files).
			const std::size_t all_rows = buf->Nrows();
			std::string after;
			bool limited = false;
			run_regex_work([&] {
				for (std::size_t y = 0; y < all_rows; ++y) {
					if (y > 0)
						after.push_back('\n');
					const std::string before_line = buf->GetLineString(y);
					if (y >= nrows) {
						after += before_line; // the empty row after a final '\n'
						continue;
					}
					const std::string replaced = rx.ReplaceAll(before_line, repl);
					limited                    = limited || rx.LimitHit();
					if (replaced != before_line)
						++changed;
					after += replaced;
				}
			});
			if (changed > 0)
				replace_buffer_text(*buf, buffer_text(*buf), after, ru);
			clamp_cursor_to_buffer(*buf);
			buf->SetDirty(true);
			ctx.editor.SetStatus("Regex replaced in " + std::to_string(changed) + " line(s)" +
			                     (limited ? " (pattern too complex on some lines; those left as they were)" : ""));
			// Clear search UI state
			ctx.editor.SetSearchActive(false);
			ctx.editor.SetSearchQuery("");
			ctx.editor.SetSearchMatch(0, 0, 0);
			ctx.editor.ClearSearchOrigin();
			ctx.editor.SetSearchIndex(-1);
			if (auto *b = ctx.editor.CurrentBuffer())
				ensure_cursor_visible(ctx.editor, *b);
			return true;
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
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;

	// Visual-line mode: broadcast newline splits across selected lines.
	if (buf->VisualLineActive()) {
		const std::size_t sy = buf->VisualLineStartY();
		const std::size_t ey = buf->VisualLineEndY();
		if (buf->Nrows() == 0)
			return true;
		std::size_t splits_above = 0;
		if (sy < y)
			splits_above = std::min(ey, y - 1) - sy + 1;

		// All splits as one edit (one undo step): repeat newlines at the
		// cursor's column on every selected line.
		UndoSystem *vu = buf->Undo();
		UndoGroupGuard vguard(vu);
		const std::size_t nrows  = buf->Nrows();
		const std::size_t xchars = y < nrows ? utf8_char_count(line_text_view(*buf, y), x) : x;
		const std::string nls(static_cast<std::size_t>(repeat), '\n');
		transform_rows(*buf, sy, ey, vu, [&](std::string &line, std::size_t) {
			line.insert(utf8_byte_of_char(line, xchars), nls);
		});

		buf->SetDirty(true);
		// Cursor: end up on the final inserted line for the original cursor line.
		// Each selected line above the cursor gained `repeat` lines, and the
		// cursor's own line only if it was selected.
		const bool own_split = (y >= sy && y <= ey);
		std::size_t new_y    = y + (own_split ? static_cast<std::size_t>(repeat) : 0);
		new_y                += splits_above * static_cast<std::size_t>(repeat);
		buf->SetCursor(0, new_y);
		clamp_cursor_to_buffer(*buf);
		ensure_cursor_visible(ctx.editor, *buf);
		return true;
	}
	UndoSystem *u = buf->Undo();
	if (u && repeat > 1)
		u->BeginGroup();
	for (int i = 0; i < repeat; ++i) {
		// Sync the buffer's cursor to the split point before Begin(), which
		// records its node's row/col from the buffer's *current* cursor. Undo
		// must reverse this exact split, not wherever the cursor ends up after
		// the whole loop finishes.
		buf->SetCursor(x, y);
		if (u) {
			u->Begin(UndoType::Newline);
			u->commit();
		}
		buf->split_line(static_cast<int>(y), static_cast<int>(x));
		// Move to start of next line
		y += 1;
		x = 0;
	}
	if (u && repeat > 1)
		u->EndGroup();
	buf->SetCursor(x, y);
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_smart_newline(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to edit");
		return false;
	}

	// With a prompt open, Enter accepts the prompt; there is no line to indent,
	// and accepting may switch or reallocate buffers, invalidating `buf`.
	if (ctx.editor.PromptActive())
		return cmd_newline(ctx);

	if (buf->IsReadOnly()) {
		ctx.editor.SetStatus("Read-only buffer");
		return true;
	}

	// Smart newline behavior: add a newline with the same indentation as the current line.
	// Find indentation of current line
	std::size_t y    = buf->Cury();
	std::string line = buf->GetLineString(y);
	std::string indent;
	for (char c: line) {
		if (c == ' ' || c == '\t') {
			indent += c;
		} else {
			break;
		}
	}

	// The newline and its indent form one undo step.
	UndoGroupGuard group(buf->Undo());

	// Perform standard newline first
	if (!cmd_newline(ctx)) {
		return false;
	}

	// Now insert the indentation at the new cursor position
	if (!indent.empty()) {
		std::size_t new_y = buf->Cury();
		std::size_t new_x = buf->Curx();
		// Begin() records the cursor position, so it must run before the
		// cursor moves past the inserted indent.
		UndoSystem *u = buf->Undo();
		if (u) {
			u->Begin(UndoType::Insert);
			u->Append(indent);
		}
		buf->insert_text(static_cast<int>(new_y), static_cast<int>(new_x), indent);
		buf->SetCursor(new_x + indent.size(), new_y);
		buf->SetDirty(true);
		if (u)
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
		if (ctx.editor.CurrentPromptKind() == Editor::PromptKind::Search ||
		    ctx.editor.CurrentPromptKind() == Editor::PromptKind::ReplaceFind ||
		    ctx.editor.CurrentPromptKind() == Editor::PromptKind::RegexSearch ||
		    ctx.editor.CurrentPromptKind() == Editor::PromptKind::RegexReplaceFind) {
			Buffer *buf2 = ctx.editor.CurrentBuffer();
			if (buf2) {
				ctx.editor.SetSearchQuery(ctx.editor.PromptText());
				run_search(ctx.editor, *buf2, regex_prompt(ctx.editor), SearchStep::Here, true);
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
		if (Buffer *buf2 = ctx.editor.CurrentBuffer())
			run_search(ctx.editor, *buf2, false, SearchStep::Here, true);
		return true;
	}
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf) {
		ctx.editor.SetStatus("No buffer to edit");
		return false;
	}
	ensure_at_least_one_line(*buf);
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	UndoSystem *u = buf->Undo();
	int repeat    = ctx.count > 0 ? ctx.count : 1;

	// Visual-line mode: broadcast backspace deletes within each selected line.
	// For now, we do NOT join lines when at column 0 (too ambiguous across multiple lines).
	if (buf->VisualLineActive()) {
		const std::size_t sy = buf->VisualLineStartY();
		const std::size_t ey = buf->VisualLineEndY();
		const auto &rows     = rows_of(*buf);
		std::uint64_t gid    = 0;
		if (u)
			gid = u->BeginGroup();
		(void) gid;
		std::size_t cx = x;
		const std::size_t xchars = y < rows.size() ? utf8_char_count(line_view(rows[y]), x) : x;
		transform_rows(*buf, sy, ey, u, [&](std::string &line, const std::size_t yy) {
			std::size_t xx = utf8_byte_of_char(line, xchars);
			for (int i = 0; i < repeat && xx > 0; ++i) {
				const std::size_t n = utf8_prev_len(line, xx);
				line.erase(xx - n, n);
				xx -= n;
			}
			if (yy == y)
				cx = xx;
		});
		if (u)
			u->EndGroup();
		buf->SetDirty(true);
		buf->SetCursor(cx, y);
		ensure_cursor_visible(ctx.editor, *buf);
		return true;
	}
	// A count is one undo step, including any line joins among its deletions.
	UndoGroupGuard count_group(repeat > 1 ? u : nullptr);
	for (int i = 0; i < repeat; ++i) {
		// Refresh a read-only view of lines for char capture/lengths
		const auto &rows_view = rows_of(*buf);
		if (x > 0) {
			std::string deleted;
			std::size_t n = 1;
			if (y < rows_view.size()) {
				const std::string_view lv = line_view(rows_view[y]);
				if (x > lv.size())
					x = lv.size(); // never delete past end-of-line
				n = utf8_prev_len(lv, x);
				if (x > 0)
					deleted.assign(lv.substr(x - n, n));
			}
			if (deleted.empty()) {
				buf->SetCursor(x, y);
				continue;
			}
			buf->delete_text(static_cast<int>(y), static_cast<int>(x - n), n);
			x -= n;
			buf->SetCursor(x, y);
			if (u) {
				u->Begin(UndoType::Delete);
				u->Append(std::string_view(deleted));
			}
		} else if (y > 0) {
			// Compute previous line length before join
			std::size_t prev_len = 0;
			if (y - 1 < rows_view.size())
				prev_len = rows_view[y - 1].size();
			buf->join_lines(static_cast<int>(y - 1));
			y = y - 1;
			x = prev_len;
			buf->SetCursor(x, y);
			if (u) {
				// Forward action here is a join, not a split: JoinLines has the
				// correct (inverted) apply() semantics, unlike Newline.
				u->Begin(UndoType::JoinLines);
				u->commit();
			}
		} else {
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
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	UndoSystem *u = buf->Undo();
	int repeat    = ctx.count > 0 ? ctx.count : 1;

	// Visual-line mode: broadcast delete-char within each selected line.
	// For now, we do NOT join lines when at end-of-line.
	if (buf->VisualLineActive()) {
		const std::size_t sy = buf->VisualLineStartY();
		const std::size_t ey = buf->VisualLineEndY();
		const auto &rows     = rows_of(*buf);
		std::uint64_t gid    = 0;
		if (u)
			gid = u->BeginGroup();
		(void) gid;
		const std::size_t xchars = y < rows.size() ? utf8_char_count(line_view(rows[y]), x) : x;
		transform_rows(*buf, sy, ey, u, [&](std::string &line, std::size_t) {
			const std::size_t xx = utf8_byte_of_char(line, xchars);
			for (int i = 0; i < repeat && xx < line.size(); ++i)
				line.erase(xx, utf8_next_len(line, xx));
		});
		if (u)
			u->EndGroup();
		// The edit moved the cursor; deleting forward leaves it where it was.
		buf->SetCursor(x, y);
		buf->SetDirty(true);
		ensure_cursor_visible(ctx.editor, *buf);
		return true;
	}
	// A count is one undo step, including any line joins among its deletions.
	UndoGroupGuard count_group(repeat > 1 ? u : nullptr);
	for (int i = 0; i < repeat; ++i) {
		const auto &rows_view = rows_of(*buf);
		if (y >= rows_view.size())
			break;
		if (x < rows_view[y].size()) {
			const std::string_view lv = line_view(rows_view[y]);
			const std::size_t n       = utf8_next_len(lv, x);
			const std::string deleted(lv.substr(x, n));
			buf->delete_text(static_cast<int>(y), static_cast<int>(x), n);
			if (u) {
				u->Begin(UndoType::Delete);
				u->Append(std::string_view(deleted));
			}
		} else if (y + 1 < rows_view.size()) {
			buf->join_lines(static_cast<int>(y));
			if (u) {
				// Forward action here is a join, not a split: JoinLines has the
				// correct (inverted) apply() semantics, unlike Newline.
				u->Begin(UndoType::JoinLines);
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
		int repeat = ctx.count > 0 ? ctx.count : 1;
		for (int i = 0; i < repeat; ++i)
			u->undo();
		// Keep cursor within buffer bounds
		clamp_cursor_to_buffer(*buf); // never leave the cursor past a line or the buffer
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
		// With branching undo, a universal-argument count selects an alternate redo branch:
		//  - no count (or 1): redo the active branch
		//  - n>1: redo the (n-1)th sibling branch from this point and make it active
		if (ctx.count > 1) {
			u->redo(ctx.count - 1);
		} else {
			u->redo();
		}
		clamp_cursor_to_buffer(*buf); // never leave the cursor past a line or the buffer
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
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	std::string killed_total;
	UndoSystem *u = buf->Undo();
	UndoGroupGuard guard(u);
	for (int i = 0; i < repeat; ++i) {
		const auto &rows_view = rows_of(*buf);
		if (y >= rows_view.size())
			break;
		if (x < rows_view[y].size()) {
			// delete from cursor to end of line
			std::string seg = static_cast<std::string>(rows_view[y].substr(x));
			killed_total    += seg;
			std::size_t len = rows_view[y].size() - x;
			buf->delete_text(static_cast<int>(y), static_cast<int>(x), len);
			if (u) {
				buf->SetCursor(x, y);
				u->Begin(UndoType::Delete);
				u->Append(std::string_view(seg));
				u->commit();
			}
		} else if (y + 1 < rows_view.size()) {
			// at EOL: delete the newline (join with next line)
			killed_total += "\n";
			if (u) {
				buf->SetCursor(x, y);
				u->Begin(UndoType::JoinLines);
				u->commit();
			}
			buf->join_lines(static_cast<int>(y));
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
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	(void) x; // cursor x will be reset to 0
	int repeat = ctx.count > 0 ? ctx.count : 1;
	std::string killed_total;
	UndoSystem *u = buf->Undo();
	UndoGroupGuard guard(u);
	for (int i = 0; i < repeat; ++i) {
		const auto &rows_view = rows_of(*buf);
		if (rows_view.empty())
			break;
		if (rows_view.size() == 1) {
			// last remaining line: clear its contents
			std::string content = static_cast<std::string>(rows_view[0]);
			killed_total += content;
			if (!content.empty()) {
				buf->delete_text(0, 0, content.size());
				if (u) {
					buf->SetCursor(0, 0);
					u->Begin(UndoType::Delete);
					u->Append(std::string_view(content));
					u->commit();
				}
			}
			y = 0;
		} else if (y + 1 == rows_view.size()) {
			// Last row. If it is empty, the buffer ends with '\n' and this row is
			// just the position after it: there is no line to kill. Otherwise
			// the line has no trailing newline, so kill it together with the
			// newline before it (delete_row would leave that newline behind).
			std::string content = static_cast<std::string>(rows_view[y]);
			if (content.empty())
				break;
			const std::size_t prev_len = rows_view[y - 1].size();
			killed_total += content;
			const std::string deleted = "\n" + content;
			buf->delete_text(static_cast<int>(y - 1), static_cast<int>(prev_len), deleted.size());
			if (u) {
				buf->SetCursor(prev_len, y - 1);
				u->Begin(UndoType::Delete);
				u->Append(std::string_view(deleted));
				u->commit();
			}
			// End of buffer reached: a repeat count must not continue upward.
			y -= 1;
			break;
		} else if (y < rows_view.size()) {
			// erase current line; keep y pointing at the next line
			std::string content = static_cast<std::string>(rows_view[y]);
			killed_total += content;
			killed_total += "\n";
			if (u) {
				buf->SetCursor(0, y);
				u->Begin(UndoType::DeleteRow);
				u->Append(std::string_view(content));
				u->commit();
			}
			buf->delete_row(static_cast<int>(y));
			const auto &rows_after = rows_of(*buf);
			if (y >= rows_after.size()) {
				// deleted last line; move to previous
				y = rows_after.empty() ? 0 : rows_after.size() - 1;
			}
		} else {
			// out of range
			const auto &rows2 = rows_of(*buf);
			y                 = rows2.empty() ? 0 : rows2.size() - 1;
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
	// Bound the total: C-u 1000000 C-y of a large kill would allocate
	// gigabytes (and bad_alloc or the OOM killer end the session).
	constexpr std::size_t kMaxYankBytes = std::size_t{256} << 20;
	// Visual-line yank inserts the text once per selected line.
	const std::size_t yank_lines =
		buf->VisualLineActive() ? (buf->VisualLineEndY() - buf->VisualLineStartY() + 1) : 1;
	if (text.size() * static_cast<std::size_t>(repeat) * yank_lines > kMaxYankBytes) {
		ctx.editor.SetStatus("Yank too large (" + std::to_string(repeat) + " x " + std::to_string(text.size()) +
		                     " bytes)");
		return false;
	}
	std::string ins;
	if (repeat == 1) {
		ins = text;
	} else {
		ins.reserve(text.size() * static_cast<std::size_t>(repeat));
		for (int i = 0; i < repeat; ++i)
			ins += text;
	}

	UndoSystem *u = buf->Undo();
	// Visual-line mode: broadcast yank to beginning-of-line on every affected line.
	if (buf->VisualLineActive()) {
		const std::size_t sy = buf->VisualLineStartY();
		const std::size_t ey = buf->VisualLineEndY();
		const std::size_t y0 = buf->Cury();

		{
			// One edit for all lines (see transform_rows).
			UndoGroupGuard yguard(u);
			transform_rows(*buf, sy, ey, u, [&](std::string &line, std::size_t) {
				line.insert(0, ins);
			});
		}

		// Keep the point on the primary cursor line (as it was before yank), at the end of the
		// inserted text for that line.
		std::size_t nl_count = 0;
		std::size_t last_nl  = std::string::npos;
		for (std::size_t i = 0; i < ins.size(); ++i) {
			if (ins[i] == '\n') {
				++nl_count;
				last_nl = i;
			}
		}
		const std::size_t delta_y = nl_count;
		const std::size_t delta_x = (last_nl == std::string::npos) ? ins.size() : (ins.size() - last_nl - 1);
		// The cursor may have left the selection (C-k a, Enter, ...) without
		// updating it; place it relative to the nearest selected line.
		const std::size_t yc    = std::clamp(y0, sy, ey);
		const std::size_t above = yc - sy;
		buf->SetCursor(delta_x, yc + delta_y + above * nl_count);
		clamp_cursor_to_buffer(*buf);
	} else {
		if (u)
			u->Begin(UndoType::Paste);
		insert_text_at_cursor(*buf, ins);
		if (u) {
			u->Append(std::string_view(ins));
			u->commit();
		}
	}
	// Yank is a paste operation; it should clear the mark/region and any selection highlighting.
	buf->ClearMark();
	if (buf->VisualLineActive())
		buf->VisualLineClear();
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
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	buf->SetCursor(0, 0);
	if (buf->VisualLineActive())
		buf->VisualLineSetActiveY(buf->Cury());
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


static bool
cmd_move_file_end(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	const auto &rows = rows_of(*buf);
	std::size_t y    = rows.empty() ? 0 : rows.size() - 1;
	std::size_t x    = rows.empty() ? 0 : rows[y].size();
	buf->SetCursor(x, y);
	if (buf->VisualLineActive())
		buf->VisualLineSetActiveY(buf->Cury());
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
cmd_visual_line_mode_toggle(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	buf->VisualLineToggle();
	ctx.editor.SetStatus(std::string("Visual line: ") + (buf->VisualLineActive() ? "ON" : "OFF"));
	return true;
}


static bool
cmd_jump_to_mark(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	if (!buf->MarkSet()) {
		ctx.editor.SetStatus("Mark not set");
		return false;
	}
	std::size_t cx = buf->Curx();
	std::size_t cy = buf->Cury();
	std::size_t mx = buf->MarkCurx();
	std::size_t my = buf->MarkCury();
	buf->SetCursor(mx, my);
	// Marks are not adjusted by edits, so the mark may now be past the end.
	clamp_cursor_to_buffer(*buf);
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
	{
		UndoGroupGuard guard(buf->Undo());
		delete_region(*buf, sx, sy, ex, ey, buf->Undo());
	}
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
	// In a search prompt (or search mode), Left/Right step to the previous/
	// next match.
	if (ctx.editor.PromptActive() &&
	    (ctx.editor.CurrentPromptKind() == Editor::PromptKind::Search ||
	     ctx.editor.CurrentPromptKind() == Editor::PromptKind::RegexSearch ||
	     ctx.editor.CurrentPromptKind() == Editor::PromptKind::ReplaceFind ||
	     ctx.editor.CurrentPromptKind() == Editor::PromptKind::RegexReplaceFind)) {
		run_search(ctx.editor, *buf, regex_prompt(ctx.editor), SearchStep::Prev, false);
		return true;
	}
	if (ctx.editor.SearchActive()) {
		run_search(ctx.editor, *buf, false, SearchStep::Prev, false);
		return true;
	}
	ensure_at_least_one_line(*buf);
	auto rows    = rows_of(*buf);
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	if (rows.empty())
		return true;
	if (y >= rows.size())
		y = rows.size() - 1;
	if (x > rows[y].size())
		x = rows[y].size();
	while (repeat-- > 0) {
		if (x > 0) {
			x -= utf8_prev_len(line_view(rows[y]), x);
		} else if (y > 0) {
			--y;
			x = rows[y].size();
		}
	}
	buf->SetCursor(x, y);
	if (buf->VisualLineActive())
		buf->VisualLineSetActiveY(buf->Cury());
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
	// In a search prompt (or search mode), Left/Right step to the previous/
	// next match.
	if (ctx.editor.PromptActive() &&
	    (ctx.editor.CurrentPromptKind() == Editor::PromptKind::Search ||
	     ctx.editor.CurrentPromptKind() == Editor::PromptKind::RegexSearch ||
	     ctx.editor.CurrentPromptKind() == Editor::PromptKind::ReplaceFind ||
	     ctx.editor.CurrentPromptKind() == Editor::PromptKind::RegexReplaceFind)) {
		run_search(ctx.editor, *buf, regex_prompt(ctx.editor), SearchStep::Next, false);
		return true;
	}
	if (ctx.editor.SearchActive()) {
		run_search(ctx.editor, *buf, false, SearchStep::Next, false);
		return true;
	}
	ensure_at_least_one_line(*buf);
	auto rows    = rows_of(*buf);
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	while (repeat-- > 0) {
		if (y < rows.size() && x < rows[y].size()) {
			x += utf8_next_len(line_view(rows[y]), x);
		} else if (y + 1 < rows.size()) {
			++y;
			x = 0;
		}
	}
	buf->SetCursor(x, y);
	if (buf->VisualLineActive())
		buf->VisualLineSetActiveY(buf->Cury());
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
		// Up == previous match (a regex query in a regex prompt)
		run_search(ctx.editor, *buf, ctx.editor.PromptActive() && regex_prompt(ctx.editor), SearchStep::Prev,
		           false);
		return true;
	}
	ensure_at_least_one_line(*buf);
	auto rows    = rows_of(*buf);
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	if (rows.empty())
		return true;
	if (y >= rows.size())
		y = rows.size() - 1;
	if (repeat > static_cast<int>(y))
		repeat = static_cast<int>(y);
	y -= static_cast<std::size_t>(repeat);
	if (x > rows[y].size())
		x = rows[y].size();
	// Keep the column on a character boundary of the new line.
	while (x > 0 && x < rows[y].size() && utf8_is_cont(rows[y][x]))
		--x;
	buf->SetCursor(x, y);
	if (buf->VisualLineActive())
		buf->VisualLineSetActiveY(buf->Cury());
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
		// Down == next match (a regex query in a regex prompt)
		run_search(ctx.editor, *buf, ctx.editor.PromptActive() && regex_prompt(ctx.editor), SearchStep::Next,
		           false);
		return true;
	}
	ensure_at_least_one_line(*buf);
	auto rows           = rows_of(*buf);
	std::size_t y        = buf->Cury();
	std::size_t x        = buf->Curx();
	int repeat           = ctx.count > 0 ? ctx.count : 1;
	if (rows.empty())
		return true;
	if (y >= rows.size())
		y = rows.size() - 1;
	std::size_t max_down = rows.size() - 1 - y;
	if (repeat > static_cast<int>(max_down))
		repeat = static_cast<int>(max_down);
	y += static_cast<std::size_t>(repeat);
	if (x > rows[y].size())
		x = rows[y].size();
	// Keep the column on a character boundary of the new line.
	while (x > 0 && x < rows[y].size() && utf8_is_cont(rows[y][x]))
		--x;
	buf->SetCursor(x, y);
	if (buf->VisualLineActive())
		buf->VisualLineSetActiveY(buf->Cury());
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
	auto rows    = rows_of(*buf);
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
	auto rows               = rows_of(*buf);
	int repeat               = ctx.count > 0 ? ctx.count : 1;
	std::size_t content_rows = std::max<std::size_t>(1, ctx.editor.ContentRows());

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
	auto rows               = rows_of(*buf);
	int repeat               = ctx.count > 0 ? ctx.count : 1;
	std::size_t content_rows = std::max<std::size_t>(1, ctx.editor.ContentRows());

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
	return true;
}


static bool
cmd_scroll_up(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	const auto &rows         = rows_of(*buf);
	std::size_t content_rows = std::max<std::size_t>(1, ctx.editor.ContentRows());
	std::size_t rowoffs      = buf->Rowoffs();

	// Scroll up by 3 lines (or count if specified), without moving cursor
	int scroll_amount = ctx.count > 0 ? ctx.count : 3;
	if (rowoffs >= static_cast<std::size_t>(scroll_amount))
		rowoffs -= static_cast<std::size_t>(scroll_amount);
	else
		rowoffs = 0;

	buf->SetOffsets(rowoffs, buf->Coloffs());

	// If cursor is now below the visible area, move it to the last visible line
	std::size_t cury = buf->Cury();
	if (cury >= rowoffs + content_rows) {
		std::size_t new_y = rowoffs + content_rows - 1;
		if (new_y >= rows.size() && !rows.empty())
			new_y = rows.size() - 1;
		buf->SetCursor(buf->Curx(), new_y);
		clamp_cursor_to_buffer(*buf);
	}

	return true;
}


static bool
cmd_scroll_down(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	const auto &rows         = rows_of(*buf);
	std::size_t content_rows = std::max<std::size_t>(1, ctx.editor.ContentRows());
	std::size_t rowoffs      = buf->Rowoffs();

	// Scroll down by 3 lines (or count if specified), without moving cursor
	int scroll_amount = ctx.count > 0 ? ctx.count : 3;

	// Compute maximum top offset
	std::size_t max_top = 0;
	if (!rows.empty() && rows.size() > content_rows)
		max_top = rows.size() - content_rows;

	rowoffs += static_cast<std::size_t>(scroll_amount);
	if (rowoffs > max_top)
		rowoffs = max_top;

	buf->SetOffsets(rowoffs, buf->Coloffs());

	// If cursor is now above the visible area, move it to the first visible line
	std::size_t cury = buf->Cury();
	if (cury < rowoffs) {
		buf->SetCursor(buf->Curx(), rowoffs);
		clamp_cursor_to_buffer(*buf);
	}

	return true;
}


static inline bool
is_word_char(unsigned char c)
{
	// Bytes >= 0x80 belong to multibyte UTF-8 characters: count them as word
	// characters, so word motion and deletion never stop inside a character
	// (and letters outside ASCII are part of words).
	return std::isalnum(c) || c == '_' || c >= 0x80;
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
	auto rows    = rows_of(*buf);
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
	auto rows    = rows_of(*buf);
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
	auto rows    = rows_of(*buf);
	std::size_t y = buf->Cury();
	std::size_t x = buf->Curx();
	int repeat    = ctx.count > 0 ? ctx.count : 1;
	std::string killed_total;
	UndoGroupGuard guard(buf->Undo());
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
		// Now delete from (x, y) to (start_x, start_y) using PieceTable
		std::string deleted = extract_region_text(*buf, x, y, start_x, start_y);
		delete_region(*buf, x, y, start_x, start_y, buf->Undo());
		// Prepend to killed_total (since we're deleting backwards)
		killed_total = deleted + killed_total;
	}
	buf->SetCursor(x, y);
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
	if (!killed_total.empty()) {
		if (ctx.editor.KillChain())
			ctx.editor.KillRingPrepend(killed_total);
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
	const auto &rows = rows_of(*buf);
	std::size_t y    = buf->Cury();
	std::size_t x    = buf->Curx();
	int repeat       = ctx.count > 0 ? ctx.count : 1;
	std::string killed_total;
	UndoGroupGuard guard(buf->Undo());
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
		// Now delete from (start_x, start_y) to (x, y) using PieceTable
		std::string deleted = extract_region_text(*buf, start_x, start_y, x, y);
		delete_region(*buf, start_x, start_y, x, y, buf->Undo());
		y            = start_y;
		x            = start_x;
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
	UndoSystem *u = buf->Undo();
	UndoGroupGuard guard(u);
	// Empty lines are left alone: indenting them only added trailing
	// whitespace, and the empty row after a final newline became a tab-only
	// last line.
	transform_rows(*buf, sy, ey, u, [](std::string &line, std::size_t) {
		if (!line.empty())
			line.insert(0, 1, '\t');
	});
	buf->SetCursor(0, std::min(ey, buf->Nrows() ? buf->Nrows() - 1 : 0));
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
	UndoSystem *u = buf->Undo();
	UndoGroupGuard guard(u);
	transform_rows(*buf, sy, ey, u, [](std::string &line, std::size_t) {
		if (line.empty())
			return;
		if (line[0] == '\t') {
			line.erase(0, 1);
		} else if (line[0] == ' ') {
			std::size_t spaces = 0;
			while (spaces < line.size() && spaces < 8 && line[spaces] == ' ')
				++spaces;
			line.erase(0, spaces);
		}
	});
	buf->SetCursor(0, std::min(ey, buf->Nrows() ? buf->Nrows() - 1 : 0));
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
	// Reflow performs a multi-edit transformation; make it a single standalone undo/redo step.
	UndoGroupGuard guard(buf->Undo());
	if (auto *u = buf->Undo())
		u->commit();
	ensure_at_least_one_line(*buf);
	auto rows    = rows_of(*buf);
	std::size_t y = buf->Cury();
	// Treat a universal-argument count of 1 as "no width specified".
	// Editor::UArgGet() returns 1 when no explicit count was provided.
	int width              = ctx.count > 1 ? ctx.count : 72;
	// A blank line (empty, or only spaces, tabs or a CR) separates
	// paragraphs; expanding from it would merge the paragraphs above and
	// below.
	auto is_blank_row = [&](std::size_t i) {
		for (const char c: line_view(rows[i]))
			if (c != ' ' && c != '\t' && c != '\r')
				return false;
		return true;
	};
	if (y >= rows.size() || is_blank_row(y))
		return true;
	std::size_t para_start = y;
	while (para_start > 0 && !is_blank_row(para_start - 1))
		--para_start;
	std::size_t para_end = y;
	while (para_end + 1 < rows.size() && !is_blank_row(para_end + 1))
		++para_end;
	if (para_start > para_end)
		return false;
	// CRLF text: reflow the lines without their CR (it would otherwise be
	// joined into the middle of lines). If any line had one, every new line
	// but the last gets one; the last keeps whatever the paragraph's last
	// line had (a CRLF file often lacks the final line ending).
	bool crlf = false;
	for (std::size_t i = para_start; i <= para_end && !crlf; ++i) {
		const std::string_view lv = line_view(rows[i]);
		crlf = !lv.empty() && lv.back() == '\r';
	}
	const std::string_view last_lv = line_view(rows[para_end]);
	const bool last_cr             = !last_lv.empty() && last_lv.back() == '\r';
	auto row_text = [&](std::size_t i) {
		std::string t = static_cast<std::string>(rows[i]);
		if (!t.empty() && t.back() == '\r')
			t.pop_back();
		return t;
	};

	auto is_space = [](char c) {
		return c == ' ' || c == '\t';
	};

	auto leading_ws = [&](const std::string &s) {
		std::size_t i = 0;
		while (i < s.size() && is_space(s[i]))
			++i;
		return s.substr(0, i);
	};

	auto starts_with = [](const std::string &s, const std::string &pfx) {
		return s.size() >= pfx.size() && std::equal(pfx.begin(), pfx.end(), s.begin());
	};

	auto is_bullet_line = [&](const std::string &s, std::string &indent_out, char &marker_out,
	                          std::size_t &after_prefix_idx) -> bool {
		indent_out    = leading_ws(s);
		std::size_t i = indent_out.size();
		if (i + 1 < s.size()) {
			char m = s[i];
			if ((m == '-' || m == '+' || m == '*') && s[i + 1] == ' ') {
				marker_out       = m;
				after_prefix_idx = i + 2; // after marker + space
				return true;
			}
		}
		return false;
	};

	auto is_numbered_line = [&](const std::string &s,
	                            std::string &indent_out,
	                            std::string &marker_out,
	                            std::size_t &after_prefix_idx) -> bool {
		indent_out    = leading_ws(s);
		std::size_t i = indent_out.size();
		if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i])))
			return false;
		std::size_t j = i;
		while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
			++j;
		if (j >= s.size())
			return false;
		char delim = s[j];
		if (!(delim == '.' || delim == ')'))
			return false;
		if (j + 1 >= s.size() || s[j + 1] != ' ')
			return false;
		marker_out       = s.substr(i, (j - i) + 1); // e.g. "1." or "10)"
		after_prefix_idx = j + 2; // after delimiter + space
		return true;
	};

	auto normalize_spaces = [](const std::string &in) {
		std::string out;
		out.reserve(in.size());
		bool in_space = false;
		for (char c: in) {
			char cc = (c == '\t') ? ' ' : c;
			if (cc == ' ') {
				if (!in_space) {
					out.push_back(' ');
					in_space = true;
				}
			} else {
				out.push_back(cc);
				in_space = false;
			}
		}
		// trim leading/trailing spaces
		// leading
		std::size_t start = 0;
		while (start < out.size() && out[start] == ' ')
			++start;
		// trailing
		std::size_t end = out.size();
		while (end > start && out[end - 1] == ' ')
			--end;
		return out.substr(start, end - start);
	};

	auto looks_like_marker = [](const std::string &wrd) {
		if (wrd == "-" || wrd == "+" || wrd == "*")
			return true;
		std::size_t i = 0;
		while (i < wrd.size() && std::isdigit(static_cast<unsigned char>(wrd[i])))
			++i;
		return i > 0 && i + 1 == wrd.size() && (wrd[i] == '.' || wrd[i] == ')');
	};

	auto wrap_with_prefixes = [&](const std::string &content,
	                              const std::string &first_prefix,
	                              const std::string &cont_prefix,
	                              int w,
	                              std::vector<std::string> &dst) {
		// Tokenize by spaces
		std::vector<std::string> words;
		std::size_t pos = 0;
		while (pos < content.size()) {
			while (pos < content.size() && content[pos] == ' ')
				++pos;
			if (pos >= content.size())
				break;
			std::size_t ws = pos;
			while (pos < content.size() && content[pos] != ' ')
				++pos;
			words.emplace_back(content.substr(ws, pos - ws));
		}
		std::string line        = first_prefix;
		std::size_t cur_len     = line.size();
		bool first_word_on_line = true;
		auto flush_line         = [&]() {
			dst.emplace_back(line);
			line               = cont_prefix;
			cur_len            = line.size();
			first_word_on_line = true;
		};
		if (words.empty()) {
			// Still emit a line with just the prefix (e.g., empty bullet)
			dst.emplace_back(line);
			return;
		}
		for (std::size_t i = 0; i < words.size(); ++i) {
			const std::string &wrd = words[i];
			std::size_t needed     = wrd.size() + (first_word_on_line ? 0 : 1);
			// Wrap before a word that does not fit, but never before the
			// first word on a line: that emitted a line holding only the
			// prefix (a blank line, or a bullet marker split from its text)
			// ahead of a word longer than the width, again on every reflow.
			// Nor before a word that would read as a list marker at the
			// start of a line ("-", "*", "1."): the next reflow would turn
			// the paragraph into a list. Such a word stays on this line.
			if (!first_word_on_line && static_cast<int>(cur_len + needed) > w &&
			    !looks_like_marker(wrd)) {
				flush_line();
			}
			if (!first_word_on_line) {
				line.push_back(' ');
				++cur_len;
			}
			line               += wrd;
			cur_len            += wrd.size();
			first_word_on_line = false;
		}
		if (!line.empty())
			dst.emplace_back(line);
	};

	std::vector<std::string> new_lines;

	// Determine if this region looks like a list: any line starting with bullet or number
	bool region_has_list = false;
	for (std::size_t i = para_start; i <= para_end; ++i) {
		std::string s = row_text(i);
		std::string indent;
		char marker;
		std::size_t idx;
		if (is_bullet_line(s, indent, marker, idx)) {
			region_has_list = true;
			break;
		}
		std::string nmarker;
		if (is_numbered_line(s, indent, nmarker, idx)) {
			region_has_list = true;
			break;
		}
	}

	if (region_has_list) {
		// Parse as list items; support hanging indent continuations
		for (std::size_t i = para_start; i <= para_end; ++i) {
			std::string s = row_text(i);
			std::string indent;
			char marker           = 0;
			std::size_t after_idx = 0;
			if (is_bullet_line(s, indent, marker, after_idx)) {
				std::string first_prefix = indent + std::string(1, marker) + " ";
				std::string cont_prefix  = indent + "  ";
				std::string content      = s.substr(after_idx);
				// consume continuation lines that are part of this bullet item
				std::size_t j = i + 1;
				while (j <= para_end) {
					std::string ns = row_text(j);
					// stop if next bullet at same indentation or different structure
					std::string nindent;
					char nmarker;
					std::size_t nidx;
					if (is_bullet_line(ns, nindent, nmarker, nidx)) {
						break; // next item
					}
					std::string nnmarker;
					if (is_numbered_line(ns, nindent, nnmarker, nidx)) {
						break; // next item
					}
					// Now check if it's a continuation line
					if (starts_with(ns, indent + "  ")) {
						content += ' ';
						content += ns.substr(indent.size() + 2);
						++j;
						continue;
					}
					// Not a continuation and not a bullet: stop (treat as separate paragraph chunk)
					break;
				}
				content = normalize_spaces(content);
				wrap_with_prefixes(content, first_prefix, cont_prefix, width, new_lines);
				i = j - 1; // advance
			} else {
				std::string nmarker;
				if (is_numbered_line(s, indent, nmarker, after_idx)) {
					std::string first_prefix = indent + nmarker + " ";
					std::string cont_prefix  = indent + std::string(nmarker.size() + 1, ' ');
					std::string content      = s.substr(after_idx);
					// consume continuation lines that are part of this numbered item
					std::size_t j = i + 1;
					while (j <= para_end) {
						std::string ns = row_text(j);
						if (starts_with(ns, cont_prefix)) {
							content += ' ';
							content += ns.substr(cont_prefix.size());
							++j;
							continue;
						}
						// stop if next item
						std::string nindent2;
						char bmarker;
						std::size_t nidx;
						if (is_bullet_line(ns, nindent2, bmarker, nidx))
							break;
						std::string nnmarker;
						if (is_numbered_line(ns, nindent2, nnmarker, nidx))
							break;
						break;
					}
					content = normalize_spaces(content);
					wrap_with_prefixes(content, first_prefix, cont_prefix, width, new_lines);
					i = j - 1;
				} else {
					// A non-bullet line within a list region; treat as its own wrapped paragraph preserving its indent
					std::string base_indent = leading_ws(s);
					std::string content     = s.substr(base_indent.size());
					std::size_t j           = i + 1;
					while (j <= para_end) {
						std::string ns      = row_text(j);
						std::string nindent = leading_ws(ns);
						std::string tmp_indent;
						char tmp_marker;
						std::size_t tmp_idx;
						if (is_bullet_line(ns, tmp_indent, tmp_marker, tmp_idx)) {
							break; // next bullet starts
						}
						std::string tmp_nmarker;
						if (is_numbered_line(ns, tmp_indent, tmp_nmarker, tmp_idx)) {
							break; // next numbered starts
						}
						if (nindent.size() >= base_indent.size()) {
							content += ' ';
							content += ns.substr(base_indent.size());
							++j;
						} else {
							break;
						}
					}
					content = normalize_spaces(content);
					wrap_with_prefixes(content, base_indent, base_indent, width, new_lines);
					i = j - 1;
				}
			}
		}
	} else {
		// Normal paragraph: preserve indentation of first line
		std::string s0  = row_text(para_start);
		std::string pfx = leading_ws(s0);
		std::string content;
		for (std::size_t i = para_start; i <= para_end; ++i) {
			std::string si = row_text(i);
			// strip the same prefix length if present
			if (si.size() >= pfx.size() && starts_with(si, pfx))
				si.erase(0, pfx.size());
			if (!content.empty())
				content.push_back(' ');
			content += si;
		}
		content = normalize_spaces(content);
		wrap_with_prefixes(content, pfx, pfx, width, new_lines);
	}

	if (new_lines.empty())
		new_lines.push_back("");

	// Replace the paragraph's text in place (see replace_rows_text).
	std::string old_text;
	for (std::size_t i = para_start; i <= para_end; ++i) {
		if (i > para_start)
			old_text.push_back('\n');
		old_text += static_cast<std::string>(rows[i]);
	}
	std::string new_text;
	for (std::size_t i = 0; i < new_lines.size(); ++i) {
		if (i > 0)
			new_text.push_back('\n');
		new_text += new_lines[i];
		if (crlf && (i + 1 < new_lines.size() || last_cr))
			new_text.push_back('\r');
	}
	if (new_text != old_text)
		replace_rows_text(*buf, para_start, old_text, new_text, buf->Undo());

	// Place cursor at the end of the paragraph
	std::size_t new_last_y = para_start + (new_lines.empty() ? 0 : new_lines.size() - 1);
	std::size_t new_last_x = new_lines.empty() ? 0 : new_lines.back().size();
	buf->SetCursor(new_last_x, new_last_y);
	buf->SetDirty(true);
	ensure_cursor_visible(ctx.editor, *buf);
	return true;
}


// Pending reload confirmation: the buffer (by Id(), since addresses are
// reused once a buffer is closed) and its version when it was armed. Any
// other command cancels it (see Execute).
static std::uint64_t reload_confirm_id      = 0;
static std::uint64_t reload_confirm_version = 0;


static bool
cmd_reload_buffer(CommandContext &ctx)
{
	Buffer *buf = ctx.editor.CurrentBuffer();
	if (!buf)
		return false;
	// Remember the current cursor position so we can attempt to restore it
	const std::size_t old_x     = buf->Curx();
	const std::size_t old_y     = buf->Cury();
	const std::string &filename = buf->Filename();
	if (filename.empty()) {
		ctx.editor.SetStatus("Cannot reload unnamed buffer");
		return false;
	}
	// Reloading a file that is gone would replace the buffer with nothing.
	if (!fs_exists(filename)) {
		ctx.editor.SetStatus("File no longer exists on disk; not reloading " + filename);
		return false;
	}
	// Reload discards unsaved edits (and their undo history and journal), so
	// a dirty buffer needs a second C-k l with no edit in between.
	if (buf->Dirty() && !(reload_confirm_id == buf->Id() && reload_confirm_version == buf->Version())) {
		reload_confirm_id      = buf->Id();
		reload_confirm_version = buf->Version();
		ctx.editor.SetStatus("Unsaved changes will be lost. C-k l again to reload anyway");
		return true;
	}
	reload_confirm_id = 0;
	std::string err;
	if (!buf->OpenFromFile(filename, err)) {
		ctx.editor.SetStatus(std::string("Reload failed: ") + err);
		return false;
	}
	// The journal recorded edits against the discarded content; restart it
	// from the reloaded file, or crash recovery would replay those edits.
	if (auto *sm = ctx.editor.Swap())
		sm->ResetJournal(*buf);
	// Try to restore the cursor to its previous position if still valid; otherwise clamp
	{
		auto rows              = rows_of(*buf);
		const std::size_t nrows = rows.size();
		if (nrows == 0) {
			buf->SetCursor(0, 0);
		} else {
			const std::size_t new_y    = old_y < nrows ? old_y : (nrows - 1);
			const std::size_t line_len = rows[new_y].size();
			const std::size_t new_x    = old_x < line_len ? old_x : line_len;
			buf->SetCursor(new_x, new_y);
		}
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
	auto rows         = rows_of(*buf);
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
		{CommandId::KPrefix, "k-prefix", "Entering k-command prefix (show hint)", cmd_kprefix, false, false});
	CommandRegistry::Register({
		CommandId::UnknownKCommand, "unknown-k", "Unknown k-command (status)",
		cmd_unknown_kcommand, false, false
	});
	CommandRegistry::Register({
		CommandId::UnknownEscCommand, "unknown-esc", "Unknown ESC command (status)",
		cmd_unknown_esc_command, false, false
	});
	CommandRegistry::Register({
		CommandId::FindStart, "find-start", "Begin incremental search", cmd_find_start, false, false
	});
	CommandRegistry::Register({
		CommandId::RegexFindStart, "regex-find-start", "Begin regex search", cmd_regex_find_start, false, false
	});
	CommandRegistry::Register({
		CommandId::RegexpReplace, "regex-replace", "Begin regex search & replace", cmd_regex_replace_start,
		false, false
	});
	CommandRegistry::Register({
		CommandId::SearchReplace, "search-replace", "Begin search & replace", cmd_search_replace_start, false,
		false
	});
	CommandRegistry::Register({
		CommandId::OpenFileStart, "open-file-start", "Begin open-file prompt", cmd_open_file_start, false, false
	});
	// Buffers
	CommandRegistry::Register({
		CommandId::BufferSwitchStart, "buffer-switch-start", "Begin buffer switch prompt",
		cmd_buffer_switch_start, false, false
	});
	CommandRegistry::Register({CommandId::BufferNew, "buffer-new", "Open new empty buffer", cmd_buffer_new});
	CommandRegistry::Register({CommandId::BufferNext, "buffer-next", "Switch to next buffer", cmd_buffer_next});
	CommandRegistry::Register({CommandId::BufferPrev, "buffer-prev", "Switch to previous buffer", cmd_buffer_prev});
	CommandRegistry::Register({
		CommandId::BufferClose, "buffer-close", "Close current buffer", cmd_buffer_close, false, false
	});
	// Editing
	CommandRegistry::Register({
		CommandId::InsertText, "insert", "Insert text at cursor (no newlines)", cmd_insert_text, false, true
	});
	CommandRegistry::Register({CommandId::Newline, "newline", "Insert newline at cursor", cmd_newline});
	CommandRegistry::Register({
		CommandId::SmartNewline, "smart-newline", "Insert newline with auto-indent", cmd_smart_newline
	});
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
		CommandId::VisualLineModeToggle, "visual-line-toggle", "Toggle visual-line (multicursor) mode",
		cmd_visual_line_mode_toggle, false, false
	});
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
	CommandRegistry::Register({CommandId::ScrollUp, "scroll-up", "Scroll viewport up", cmd_scroll_up});
	CommandRegistry::Register({CommandId::ScrollDown, "scroll-down", "Scroll viewport down", cmd_scroll_down});
	CommandRegistry::Register({CommandId::WordPrev, "word-prev", "Move to previous word", cmd_word_prev});
	CommandRegistry::Register({CommandId::WordNext, "word-next", "Move to next word", cmd_word_next});
	CommandRegistry::Register({
		CommandId::DeleteWordPrev, "delete-word-prev", "Delete previous word", cmd_delete_word_prev
	});
	CommandRegistry::Register({
		CommandId::DeleteWordNext, "delete-word-next", "Delete next word", cmd_delete_word_next
	});
	CommandRegistry::Register({
		CommandId::MoveCursorTo, "move-cursor-to", "Move cursor to y:x", cmd_move_cursor_to, false, false
	});
	// Direct navigation by line number
	CommandRegistry::Register({
		CommandId::JumpToLine, "goto-line", "Prompt for line and jump", cmd_jump_to_line_start, false, false
	});
	// Undo/Redo
	CommandRegistry::Register({CommandId::Undo, "undo", "Undo last edit", cmd_undo, false, true});
	CommandRegistry::Register({CommandId::Redo, "redo", "Redo edit", cmd_redo, false, true});
	// Region formatting
	CommandRegistry::Register({CommandId::IndentRegion, "indent-region", "Indent region", cmd_indent_region});
	CommandRegistry::Register(
		{CommandId::UnindentRegion, "unindent-region", "Unindent region", cmd_unindent_region});
	CommandRegistry::Register({
		CommandId::ReflowParagraph, "reflow-paragraph",
		"Reflow paragraph to column width", cmd_reflow_paragraph
	});
	// Read-only
	CommandRegistry::Register({
		CommandId::ToggleReadOnly, "toggle-read-only", "Toggle buffer read-only", cmd_toggle_read_only
	});
	// GUI Themes
	CommandRegistry::Register({CommandId::ThemeNext, "theme-next", "Cycle to next GUI theme", cmd_theme_next});
	CommandRegistry::Register({CommandId::ThemePrev, "theme-prev", "Cycle to previous GUI theme", cmd_theme_prev});
	// Theme by name (public in command prompt)
	CommandRegistry::Register({
		CommandId::ThemeSetByName, "theme", "Set GUI theme by name", cmd_theme_set_by_name, true, false
	});
	// Font by name (public)
	CommandRegistry::Register({
		CommandId::FontSetByName, "font", "Set GUI font by name", cmd_font_set_by_name, true, false
	});
	// Font size (public)
	CommandRegistry::Register({
		CommandId::FontSetSize, "font-size", "Set GUI font size (pixels)", cmd_font_set_size, true, false
	});
	// Background light/dark (public)
	CommandRegistry::Register({
		CommandId::BackgroundSet, "background", "Set GUI background light|dark", cmd_background_set, true, false
	});
	// Generic command prompt (C-k ;)
	CommandRegistry::Register({
		CommandId::CommandPromptStart, "command-prompt-start", "Start generic command prompt",
		cmd_command_prompt_start, false, false
	});
	// Buffer operations
	CommandRegistry::Register({
		CommandId::ReloadBuffer, "reload-buffer", "Reload buffer from disk", cmd_reload_buffer, false, false
	});
	// Help
	CommandRegistry::Register({
		CommandId::ShowHelp, "help", "+HELP+ buffer with manual text", cmd_show_help, false, false
	});
	CommandRegistry::Register({
		CommandId::MarkAllAndJumpEnd, "mark-all-jump-end", "Set mark at beginning and jump to end",
		cmd_mark_all_and_jump_end
	});
	// GUI
	CommandRegistry::Register({
		CommandId::VisualFilePickerToggle, "file-picker-toggle", "Toggle visual file picker",
		cmd_visual_file_picker_toggle, false, false
	});
	CommandRegistry::Register({
		CommandId::VisualFontPickerToggle, "font-picker-toggle", "Show visual font picker",
		cmd_visual_font_picker_toggle, false, false
	});
	// Working directory
	CommandRegistry::Register({
		CommandId::ShowWorkingDirectory, "show-working-directory", "Show current working directory",
		cmd_show_working_directory, false, false
	});
	CommandRegistry::Register({
		CommandId::ChangeWorkingDirectory, "change-working-directory", "Change current working directory",
		cmd_change_working_directory_start, false, false
	});
	// UI helpers
	CommandRegistry::Register(
		{CommandId::UArgStatus, "uarg-status", "Update universal-arg status", cmd_uarg_status, false, false});
	// Syntax highlighting (public commands)
	CommandRegistry::Register({CommandId::Syntax, "syntax", "Syntax: on|off|reload", cmd_syntax, true});
	CommandRegistry::Register({CommandId::SetOption, "set", "Set option: key=value", cmd_set_option, true});
	// Viewport control
	CommandRegistry::Register({
		CommandId::CenterOnCursor, "center-on-cursor", "Center viewport on current line", cmd_center_on_cursor,
		false, false
	});
	// GUI: new window
	CommandRegistry::Register({
		CommandId::NewWindow, "new-window", "Open a new editor window (GUI only)", cmd_new_window,
		false, false
	});
	// Edit mode toggle (public)
	CommandRegistry::Register({
		CommandId::ToggleEditMode, "mode", "Toggle or set edit mode: code|writing",
		cmd_toggle_edit_mode, true, false
	});
}


// Run a command handler. An exception escaping a command (a filesystem
// error, bad_alloc on a huge operation, ...) used to reach main() and exit
// the editor, losing every unsaved buffer; report it and keep running.
static bool
run_handler(Editor &ed, const Command &cmd, CommandContext &ctx)
{
	if (!cmd.handler)
		return false;
	// An exception can leave undo groups opened by hand (BeginGroup without
	// its EndGroup); close them, or every later edit would join that group.
	auto close_undo_groups = [&ed] {
		if (Buffer *b = ed.CurrentBuffer())
			if (UndoSystem *u = b->Undo())
				u->AbortGroups();
	};
	// Whatever a command did to the cursor (remembered positions, visual-line
	// arithmetic, row counts from before an edit), never leave it past a line
	// or the buffer: the next edit would be applied at the clamped position
	// but recorded for undo at the stale one, and could not be undone.
	struct ClampOnExit {
		Editor &ed;


		~ClampOnExit()
		{
			if (Buffer *b = ed.CurrentBuffer())
				clamp_cursor_to_buffer(*b);
		}
	} clamp_on_exit{ed};
	if (Buffer *b = ed.CurrentBuffer())
		clamp_cursor_to_buffer(*b);
	try {
		return cmd.handler(ctx);
	} catch (const std::exception &e) {
		close_undo_groups();
		kte::ErrorHandler::Instance().Error("Command", std::string(cmd.name) + ": " + e.what(), "");
		ed.SetStatus(std::string("Error in ") + cmd.name + ": " + e.what());
	} catch (...) {
		close_undo_groups();
		kte::ErrorHandler::Instance().Error("Command", std::string(cmd.name) + ": unknown exception", "");
		ed.SetStatus(std::string("Error in ") + cmd.name);
	}
	return false;
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
	// Likewise a pending reload confirmation.
	if (id != CommandId::ReloadBuffer && id != CommandId::KPrefix)
		reload_confirm_id = 0;
	// Reset kill chain unless this is a kill-like command (so consecutive kills append)
	if (id != CommandId::KillToEOL && id != CommandId::KillLine && id != CommandId::KillRegion && id !=
	    CommandId::CopyRegion && id != CommandId::DeleteWordPrev && id != CommandId::DeleteWordNext) {
		ed.SetKillChain(false);
	}
	// While a prompt is open, keys edit the prompt; only commands that know
	// about prompts (or only change the view) may run. Anything else would
	// edit the buffer behind the prompt (skipping the read-only check below)
	// or switch/close buffers under a pending confirmation, which then acts
	// on the wrong buffer.
	if (ed.PromptActive() && !allowed_during_prompt(id)) {
		ed.SetStatus("Finish or cancel the prompt first (C-g)");
		return true;
	}
	// If buffer is read-only, block mutating commands outside of prompts
	if (!ed.PromptActive()) {
		Buffer *b = ed.CurrentBuffer();
		if (b && b->IsReadOnly() && is_mutating_command(id)) {
			ed.SetStatus("Read-only buffer");
			return true; // treated as handled, but no change
		}
	}

	// Source repeat count from editor-level universal argument per new design.
	int final_count = 0;
	if (cmd->repeatable) {
		final_count = ed.UArgGet(); // returns 1 if no active uarg
	} else {
		// Special-case non-repeatables that should NOT consume/clear uarg:
		// - KPrefix: keeps uarg for the following k-suffix command.
		// - UnknownKCommand / UnknownEscCommand: user mistyped; keep uarg for next try.
		if (id != CommandId::KPrefix && id != CommandId::UnknownKCommand && id !=
		    CommandId::UnknownEscCommand) {
			ed.UArgClear();
		}
		final_count = 0;
	}

	CommandContext ctx{ed, arg, final_count};
	return run_handler(ed, *cmd, ctx);
}


bool
Execute(Editor &ed, const std::string &name, const std::string &arg, int count)
{
	const Command *cmd = CommandRegistry::FindByName(name);
	if (!cmd)
		return false;
	if (!ed.PromptActive()) {
		Buffer *b = ed.CurrentBuffer();
		if (b && b->IsReadOnly() && is_mutating_command(cmd->id)) {
			ed.SetStatus("Read-only buffer");
			return true;
		}
	}
	CommandContext ctx{ed, arg, count};
	return run_handler(ed, *cmd, ctx);
}