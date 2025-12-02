#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <ncurses.h>
#include <regex>
#include <string>

#include "TerminalRenderer.h"
#include "Buffer.h"
#include "Editor.h"
#include "Highlight.h"

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
	if (content_rows < 1)
		content_rows = 1;

	int saved_cur_y = -1, saved_cur_x = -1; // logical cursor position within content area
	if (buf) {
		const auto &lines   = buf->Rows();
		std::size_t rowoffs = buf->Rowoffs();
		std::size_t coloffs = buf->Coloffs();

		const int tabw = 8;
		// Phase 3: prefetch visible viewport highlights (current terminal area)
		if (buf->SyntaxEnabled() && buf->Highlighter() && buf->Highlighter()->HasHighlighter()) {
			int fr = static_cast<int>(rowoffs);
			int rc = std::max(0, content_rows);
			buf->Highlighter()->PrefetchViewport(*buf, fr, rc, buf->Version());
		}

		for (int r = 0; r < content_rows; ++r) {
			move(r, 0);
			std::size_t li         = rowoffs + static_cast<std::size_t>(r);
			std::size_t render_col = 0;
			std::size_t src_i      = 0;
			// Compute matches for this line if search highlighting is active
			bool search_mode = ed.SearchActive() && !ed.SearchQuery().empty();
			std::vector<std::pair<std::size_t, std::size_t> > ranges; // [start, end)
			if (search_mode && li < lines.size()) {
				std::string sline = static_cast<std::string>(lines[li]);
				// If regex search prompt is active (RegexSearch or RegexReplaceFind), use regex to compute highlight ranges
				if (ed.PromptActive() && (
					    ed.CurrentPromptKind() == Editor::PromptKind::RegexSearch || ed.
					    CurrentPromptKind() == Editor::PromptKind::RegexReplaceFind)) {
					try {
						std::regex rx(ed.SearchQuery());
						for (auto it = std::sregex_iterator(sline.begin(), sline.end(), rx);
						     it != std::sregex_iterator(); ++it) {
							const auto &m  = *it;
							std::size_t sx = static_cast<std::size_t>(m.position());
							std::size_t ex = sx + static_cast<std::size_t>(m.length());
							ranges.emplace_back(sx, ex);
						}
					} catch (const std::regex_error &) {
						// ignore invalid patterns here; status shows error
					}
				} else {
					const std::string &q = ed.SearchQuery();
					std::size_t pos      = 0;
					while (!q.empty() && (pos = sline.find(q, pos)) != std::string::npos) {
						ranges.emplace_back(pos, pos + q.size());
						pos += q.size();
					}
				}
			}
			auto is_src_in_hl = [&](std::size_t si) -> bool {
				if (ranges.empty())
					return false;
				// ranges are non-overlapping and ordered by construction
				// linear scan is fine for now
				for (const auto &rg: ranges) {
					if (si < rg.first)
						break;
					if (si >= rg.first && si < rg.second)
						return true;
				}
				return false;
			};
			// Track current-match to optionally emphasize
			const bool has_current     = ed.SearchActive() && ed.SearchMatchLen() > 0;
			const std::size_t cur_mx   = has_current ? ed.SearchMatchX() : 0;
			const std::size_t cur_my   = has_current ? ed.SearchMatchY() : 0;
			const std::size_t cur_mend = has_current ? (ed.SearchMatchX() + ed.SearchMatchLen()) : 0;
			bool hl_on                 = false;
			bool cur_on                = false;
			int written                = 0;
			if (li < lines.size()) {
				std::string line = static_cast<std::string>(lines[li]);
				src_i            = 0;
				render_col       = 0;
				// Syntax highlighting: fetch per-line spans
				const kte::LineHighlight *lh_ptr = nullptr;
				if (buf->SyntaxEnabled() && buf->Highlighter() && buf->Highlighter()->
				    HasHighlighter()) {
					lh_ptr = &buf->Highlighter()->GetLine(
						*buf, static_cast<int>(li), buf->Version());
				}
				auto token_at = [&](std::size_t src_index) -> kte::TokenKind {
					if (!lh_ptr)
						return kte::TokenKind::Default;
					for (const auto &sp: lh_ptr->spans) {
						if (static_cast<int>(src_index) >= sp.col_start && static_cast<int>(
							    src_index) < sp.col_end)
							return sp.kind;
					}
					return kte::TokenKind::Default;
				};
				auto apply_token_attr = [&](kte::TokenKind k) {
					// Map to simple attributes; search highlight uses A_STANDOUT which takes precedence below
					attrset(A_NORMAL);
					switch (k) {
					case kte::TokenKind::Keyword:
					case kte::TokenKind::Type:
					case kte::TokenKind::Constant:
					case kte::TokenKind::Function:
						attron(A_BOLD);
						break;
					case kte::TokenKind::Comment:
						attron(A_DIM);
						break;
					case kte::TokenKind::String:
					case kte::TokenKind::Char:
					case kte::TokenKind::Number:
						// standout a bit using A_UNDERLINE if available
						attron(A_UNDERLINE);
						break;
					default:
						break;
					}
				};
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
								bool in_hl  = search_mode && is_src_in_hl(src_i);
								bool in_cur =
									has_current && li == cur_my && src_i >= cur_mx
									&& src_i < cur_mend;
								// Toggle highlight attributes
								int attr = 0;
								if (in_hl)
									attr |= A_STANDOUT;
								if (in_cur)
									attr |= A_BOLD;
								if ((attr & A_STANDOUT) && !hl_on) {
									attron(A_STANDOUT);
									hl_on = true;
								}
								if (!(attr & A_STANDOUT) && hl_on) {
									attroff(A_STANDOUT);
									hl_on = false;
								}
								if ((attr & A_BOLD) && !cur_on) {
									attron(A_BOLD);
									cur_on = true;
								}
								if (!(attr & A_BOLD) && cur_on) {
									attroff(A_BOLD);
									cur_on = false;
								}
								// Apply syntax attribute only if not in search highlight
								if (!in_hl) {
									apply_token_attr(token_at(src_i));
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
					bool in_hl  = search_mode && from_src && is_src_in_hl(src_i);
					bool in_cur =
						has_current && li == cur_my && from_src && src_i >= cur_mx && src_i <
						cur_mend;
					if (in_hl && !hl_on) {
						attron(A_STANDOUT);
						hl_on = true;
					}
					if (!in_hl && hl_on) {
						attroff(A_STANDOUT);
						hl_on = false;
					}
					if (in_cur && !cur_on) {
						attron(A_BOLD);
						cur_on = true;
					}
					if (!in_cur && cur_on) {
						attroff(A_BOLD);
						cur_on = false;
					}
					if (!in_hl && from_src) {
						apply_token_attr(token_at(src_i));
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
			if (cur_on) {
				attroff(A_BOLD);
				cur_on = false;
			}
			attrset(A_NORMAL);
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

	// Status line (inverse)
	move(rows - 1, 0);
	attron(A_REVERSE);

	// Fill the status line with spaces first
	for (int i = 0; i < cols; ++i)
		addch(' ');

	// If a prompt is active, replace the status bar with the full prompt text
	if (ed.PromptActive()) {
		// Build prompt text: "Label: text" and shorten HOME path for file-related prompts
		std::string label = ed.PromptLabel();
		std::string ptext = ed.PromptText();
		auto kind         = ed.CurrentPromptKind();
		if (kind == Editor::PromptKind::OpenFile || kind == Editor::PromptKind::SaveAs ||
		    kind == Editor::PromptKind::Chdir) {
			const char *home_c = std::getenv("HOME");
			if (home_c && *home_c) {
				std::string home(home_c);
				// Ensure we match only at the start
				if (ptext.rfind(home, 0) == 0) {
					std::string rest = ptext.substr(home.size());
					if (rest.empty())
						ptext = "~";
					else if (rest[0] == '/' || rest[0] == '\\')
						ptext = std::string("~") + rest;
				}
			}
		}
		// Prefer keeping the tail of the filename visible when it exceeds the window
		std::string msg;
		if (kind == Editor::PromptKind::Command) {
			msg = ": ";
		} else if (!label.empty()) {
			msg = label + ": ";
		}
		// When dealing with file-related prompts, left-trim the filename text so the tail stays visible
		if ((kind == Editor::PromptKind::OpenFile || kind == Editor::PromptKind::SaveAs || kind ==
		     Editor::PromptKind::Chdir) && cols > 0) {
			int avail = cols - static_cast<int>(msg.size());
			if (avail <= 0) {
				// No room for label; fall back to showing the rightmost portion of the whole string
				std::string whole = msg + ptext;
				if ((int) whole.size() > cols)
					whole = whole.substr(whole.size() - cols);
				msg = whole;
			} else {
				if ((int) ptext.size() > avail) {
					ptext = ptext.substr(ptext.size() - avail);
				}
				msg += ptext;
			}
		} else {
			// Non-file prompts: simple concatenation and clip by terminal
			msg += ptext;
		}

		// Draw left-aligned, clipped to width
		if (!msg.empty())
			mvaddnstr(rows - 1, 0, msg.c_str(), std::max(0, cols));

		// End status rendering for prompt mode
		attroff(A_REVERSE);
		// Restore logical cursor position in content area
		if (saved_cur_y >= 0 && saved_cur_x >= 0)
			move(saved_cur_y, saved_cur_x);
		return;
	}

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
			try {
				fname = ed.DisplayNameFor(*b);
			} catch (...) {
				fname = b->Filename();
				try {
					fname = std::filesystem::path(fname).filename().string();
				} catch (...) {}
			}
		} else {
			fname = "[no name]";
		}
		left += "  ";
		// Insert buffer position prefix "[x/N] " before filename
		{
			std::size_t total = ed.BufferCount();
			if (total > 0) {
				std::size_t idx1 = ed.CurrentBufferIndex() + 1; // human-friendly 1-based
				left += "[";
				left += std::to_string(static_cast<unsigned long long>(idx1));
				left += "/";
				left += std::to_string(static_cast<unsigned long long>(total));
				left += "] ";
			}
		}
		left += fname;
		if (b && b->Dirty())
			left += " *";
		// Append read-only indicator
		if (b && b->IsReadOnly())
			left += " [RO]";
		// Append total line count as "<n>L"
		if (b) {
			unsigned long lcount = static_cast<unsigned long>(b->Rows().size());
			left += " ";
			left += std::to_string(lcount);
			left += "L";
		}
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