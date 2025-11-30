#include "TerminalRenderer.h"

#include <ncurses.h>
#include <cstdio>

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
			move(cur_y, cur_x);
		}
	} else {
		mvaddstr(0, 0, "[no buffer]");
	}

	// Status line (inverse)
	move(rows - 1, 0);
	attron(A_REVERSE);
	char status[1024];
	const char *fname = (buf && buf->IsFileBacked()) ? buf->Filename().c_str() : "(new)";
	int dirty         = (buf && buf->Dirty()) ? 1 : 0;
 // New compact status: "kte v<version> | <filename>* | row:col [mk row:col]"
 int row1 = 0, col1 = 0;
 int mrow1 = 0, mcol1 = 0;
 int have_mark = 0;
 if (buf) {
     row1 = static_cast<int>(buf->Cury()) + 1;
     col1 = static_cast<int>(buf->Curx()) + 1;
     if (buf->MarkSet()) {
         have_mark = 1;
         mrow1 = static_cast<int>(buf->MarkCury()) + 1;
         mcol1 = static_cast<int>(buf->MarkCurx()) + 1;
     }
 }
 if (have_mark) {
     snprintf(status, sizeof(status), " kte %s | %s%s | %d:%d | mk %d:%d ",
              KTE_VERSION_STR,
              fname,
              dirty ? "*" : "",
              row1, col1,
              mrow1, mcol1);
 } else {
     snprintf(status, sizeof(status), " kte %s | %s%s | %d:%d ",
              KTE_VERSION_STR,
              fname,
              dirty ? "*" : "",
              row1, col1);
 }
	addnstr(status, cols);
	clrtoeol();
	attroff(A_REVERSE);

	refresh();
}
