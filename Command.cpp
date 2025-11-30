#include <algorithm>

#include "Command.h"
#include "Editor.h"
#include "Buffer.h"


// Keep buffer viewport offsets so that the cursor stays within the visible
// window based on the editor's current dimensions. The bottom row is reserved
// for the status line.
static void
ensure_cursor_visible(const Editor &ed, Buffer &buf)
{
	const std::size_t rows = ed.Rows();
	const std::size_t cols = ed.Cols();
	if (rows == 0 || cols == 0)
		return;

	std::size_t content_rows = rows > 0 ? rows - 1 : 0; // last row = status
	std::size_t cury         = buf.Cury();
	std::size_t curx         = buf.Curx();
	std::size_t rowoffs      = buf.Rowoffs();
	std::size_t coloffs      = buf.Coloffs();

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

	// Horizontal scrolling
	if (curx < coloffs) {
		coloffs = curx;
	} else if (curx >= coloffs + cols) {
		coloffs = curx - cols + 1;
	}

	buf.SetOffsets(rowoffs, coloffs);
}


static void
ensure_at_least_one_line(Buffer &buf)
{
	if (buf.Rows().empty()) {
		buf.Rows().push_back("");
		buf.SetDirty(true);
	}
}


// (helper removed)

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
    // Placeholder: renderer will handle this in Milestone 3
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
	// Placeholder for incremental search start
	ctx.editor.SetStatus("Find (incremental) start");
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
    CommandRegistry::Register({CommandId::KPrefix, "k-prefix", "Entering k-command prefix (show hint)", cmd_kprefix});
    CommandRegistry::Register({CommandId::FindStart, "find-start", "Begin incremental search", cmd_find_start});
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
