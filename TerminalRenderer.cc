#include "TerminalRenderer.h"

#include <ncurses.h>
#include <cstdio>
#include <string>
#include <algorithm>
#include <filesystem>

#include "Editor.h"
#include "Buffer.h"

// Version string expected to be provided by build system as KTE_VERSION_STR
#ifndef KTE_VERSION_STR
#  define KTE_VERSION_STR "dev"
#endif

TerminalRenderer::TerminalRenderer() = default;

TerminalRenderer::~TerminalRenderer() = default;


void
TerminalRenderer::Draw(Editor &ed)
{
	// Determine terminal size and keep editor dimensions in sync
	int rows, cols;
	getmaxyx(stdscr, rows, cols);

	// Clear screen
	erase();

	const Buffer *buf = ed.CurrentBuffer();
	int content_rows  = rows - 1; // last line is status

	int saved_cur_y = -1, saved_cur_x = -1; // logical cursor position within content area
	if (buf) {
		const auto &lines   = buf->Rows();
		std::size_t rowoffs = buf->Rowoffs();
		std::size_t coloffs = buf->Coloffs();

		const int tabw = 8;
		for (int r = 0; r < content_rows; ++r) {
			move(r, 0);
			std::size_t li = rowoffs + static_cast<std::size_t>(r);
			std::size_t render_col = 0;
			std::size_t src_i = 0;
			bool do_hl = ed.SearchActive() && li == ed.SearchMatchY() && ed.SearchMatchLen() > 0;
			std::size_t mx = do_hl ? ed.SearchMatchX() : 0;
			std::size_t mlen = do_hl ? ed.SearchMatchLen() : 0;
			bool hl_on = false;
			int written = 0;
			if (li < lines.size()) {
				const std::string &line = lines[li];
				src_i                   = 0;
				render_col              = 0;
				while (written < cols) {
					char ch       = ' ';
					bool from_src = false;
					if (src_i < line.size()) {
						unsigned char c = static_cast<unsigned char>(line[src_i]);
						if (c == '\t') {
							std::size_t next_tab = tabw - (render_col % tabw);
							if (render_col + next_tab <= coloffs) {
								render_col += next_tab;
								++src_i;
								continue;
							}
							// Emit spaces for tab
							if (render_col < coloffs) {
								// skip to coloffs
								std::size_t to_skip = std::min<std::size_t>(
									next_tab, coloffs - render_col);
								render_col += to_skip;
								next_tab -= to_skip;
							}
							// Now render visible spaces
							while (next_tab > 0 && written < cols) {
								bool in_hl = do_hl && src_i >= mx && src_i < mx + mlen;
								// highlight by source index
								if (in_hl && !hl_on) {
									attron(A_STANDOUT);
									hl_on = true;
								}
								if (!in_hl && hl_on) {
									attroff(A_STANDOUT);
									hl_on = false;
								}
								addch(' ');
								++written;
								++render_col;
								--next_tab;
							}
							++src_i;
							continue;
						} else {
							// normal char
							if (render_col < coloffs) {
								++render_col;
								++src_i;
								continue;
							}
							ch       = static_cast<char>(c);
							from_src = true;
						}
					} else {
						// beyond EOL, fill spaces
						ch       = ' ';
						from_src = false;
					}
					if (do_hl) {
						bool in_hl = from_src && src_i >= mx && src_i < mx + mlen;
						if (in_hl && !hl_on) {
							attron(A_STANDOUT);
							hl_on = true;
						}
						if (!in_hl && hl_on) {
							attroff(A_STANDOUT);
							hl_on = false;
						}
					}
					addch(static_cast<unsigned char>(ch));
					++written;
					++render_col;
					if (from_src)
						++src_i;
					if (src_i >= line.size() && written >= cols)
						break;
				}
			}
			if (hl_on) {
				attroff(A_STANDOUT);
				hl_on = false;
			}
			clrtoeol();
		}

		// Place terminal cursor at logical position accounting for tabs and coloffs
		std::size_t cy = buf->Cury();
		std::size_t rx = buf->Rx(); // render x computed by command layer
		int cur_y      = static_cast<int>(cy) - static_cast<int>(buf->Rowoffs());
		int cur_x      = static_cast<int>(rx) - static_cast<int>(buf->Coloffs());
		if (cur_y >= 0 && cur_y < content_rows && cur_x >= 0 && cur_x < cols) {
			// remember where to leave the terminal cursor after status is drawn
			saved_cur_y = cur_y;
			saved_cur_x = cur_x;
			move(cur_y, cur_x);
		}
	} else {
		mvaddstr(0, 0, "[no buffer]");
	}

	// Status line (inverse) — left: app/version/buffer/dirty, middle: message, right: cursor/mark
	move(rows - 1, 0);
	attron(A_REVERSE);

	// Fill the status line with spaces first
	for (int i = 0; i < cols; ++i)
		addch(' ');

	// Build left segment
	std::string left;
	{
		const char *app = "kte";
		left.reserve(256);
		left += app;
		left += " ";
		left += KTE_VERSION_STR; // already includes leading 'v'
		const Buffer *b = buf;
		std::string fname;
		if (b) {
			fname = b->Filename();
		}
		if (!fname.empty()) {
			try {
				fname = std::filesystem::path(fname).filename().string();
			} catch (...) {
				// keep original on any error
			}
		} else {
			fname = "[no name]";
		}
		left += "  ";
		left += fname;
		if (b && b->Dirty())
			left += " *";
	}

	// Build right segment (cursor and mark)
	std::string right;
	{
		int row1       = 0, col1  = 0;
		int mrow1      = 0, mcol1 = 0;
		bool have_mark = false;
		if (buf) {
			row1 = static_cast<int>(buf->Cury()) + 1;
			col1 = static_cast<int>(buf->Curx()) + 1;
			if (buf->MarkSet()) {
				have_mark = true;
				mrow1     = static_cast<int>(buf->MarkCury()) + 1;
				mcol1     = static_cast<int>(buf->MarkCurx()) + 1;
			}
		}
		char rbuf[128];
		if (have_mark)
			std::snprintf(rbuf, sizeof(rbuf), "%d,%d | M: %d,%d", row1, col1, mrow1, mcol1);
		else
			std::snprintf(rbuf, sizeof(rbuf), "%d,%d | M: not set", row1, col1);
		right = rbuf;
	}

	// Compute placements with truncation rules: prioritize left and right; middle gets remaining
	int rlen = static_cast<int>(right.size());
	if (rlen > cols) {
		// Hard clip right if too long
		right = right.substr(static_cast<std::size_t>(rlen - cols), static_cast<std::size_t>(cols));
		rlen  = cols;
	}
	int left_max = std::max(0, cols - rlen - 1); // leave at least 1 space between left and right areas
	int llen     = static_cast<int>(left.size());
	if (llen > left_max)
		llen = left_max;

	// Draw left
	if (llen > 0)
		mvaddnstr(rows - 1, 0, left.c_str(), llen);

	// Draw right, flush to end
	int rstart = std::max(0, cols - rlen);
	if (rlen > 0)
		mvaddnstr(rows - 1, rstart, right.c_str(), rlen);

	// Middle message
	const std::string &msg = ed.Status();
	if (!msg.empty()) {
		int mid_start = llen + 1; // one space after left
		int mid_end   = rstart - 1; // one space before right
		if (mid_end >= mid_start) {
			int avail  = mid_end - mid_start + 1;
			int mlen   = static_cast<int>(msg.size());
			int mdraw  = std::min(avail, mlen);
			int mstart = mid_start + std::max(0, (avail - mdraw) / 2); // center within middle area
			mvaddnstr(rows - 1, mstart, msg.c_str(), mdraw);
		}
	}

	attroff(A_REVERSE);

	// Restore terminal cursor to the content position so a visible caret
	// remains in the editing area (not on the status line).
	if (saved_cur_y >= 0 && saved_cur_x >= 0) {
		move(saved_cur_y, saved_cur_x);
	}

	refresh();
}
