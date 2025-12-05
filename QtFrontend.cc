#include "QtFrontend.h"

#include <QApplication>
#include <QWidget>
#include <QKeyEvent>
#include <QTimer>
#include <QScreen>
#include <QFont>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QFileDialog>
#include <QFontDialog>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <regex>

#include "Editor.h"
#include "Command.h"
#include "Buffer.h"
#include "GUITheme.h"
#include "Highlight.h"

namespace {
class MainWindow : public QWidget {
public:
	explicit MainWindow(class QtInputHandler &ih, QWidget *parent = nullptr)
		: QWidget(parent), input_(ih)
	{
		// Match ImGui window title format
		setWindowTitle(QStringLiteral("kge - kyle's graphical editor ")
		               + QStringLiteral(KTE_VERSION_STR));
		resize(1280, 800);
		setFocusPolicy(Qt::StrongFocus);
	}


	bool WasClosed() const
	{
		return closed_;
	}


	void SetEditor(Editor *ed)
	{
		ed_ = ed;
	}


	void SetFontFamilyAndSize(QString family, int px)
	{
		if (family.isEmpty())
			family = QStringLiteral("Brass Mono");
		if (px <= 0)
			px = 18;
		font_family_ = std::move(family);
		font_px_     = px;
		update();
	}

protected:
	void keyPressEvent(QKeyEvent *event) override
	{
		// Route to editor keymap; if handled, accept and stop propagation so
		// Qt doesn't trigger any default widget shortcuts.
		if (input_.ProcessKeyEvent(*event)) {
			event->accept();
			return;
		}
		QWidget::keyPressEvent(event);
	}


	void paintEvent(QPaintEvent *event) override
	{
		Q_UNUSED(event);
		QPainter p(this);
		p.setRenderHint(QPainter::TextAntialiasing, true);

		// Colors from GUITheme palette (Qt branch)
		auto to_qcolor = [](const KteColor &c) -> QColor {
			int r = int(std::round(c.x * 255.0f));
			int g = int(std::round(c.y * 255.0f));
			int b = int(std::round(c.z * 255.0f));
			int a = int(std::round(c.w * 255.0f));
			return QColor(r, g, b, a);
		};
		const auto pal         = kte::GetPalette();
		const QColor bg        = to_qcolor(pal.bg);
		const QColor fg        = to_qcolor(pal.fg);
		const QColor sel_bg    = to_qcolor(pal.sel_bg);
		const QColor cur_bg    = to_qcolor(pal.cur_bg);
		const QColor status_bg = to_qcolor(pal.status_bg);
		const QColor status_fg = to_qcolor(pal.status_fg);

		// Background
		p.fillRect(rect(), bg);

		// Font/metrics (configured or defaults)
		QFont f(font_family_, font_px_);
		p.setFont(f);
		QFontMetrics fm(f);
		const int line_h = fm.height();
		const int ch_w   = std::max(1, fm.horizontalAdvance(QStringLiteral(" ")));

		// Layout metrics
		const int pad_l    = 8;
		const int pad_t    = 6;
		const int pad_r    = 8;
		const int pad_b    = 6;
		const int status_h = line_h + 6; // status bar height

		// Content area (text viewport)
		const QRect content_rect(pad_l,
		                         pad_t,
		                         width() - pad_l - pad_r,
		                         height() - pad_t - pad_b - status_h);

		// Text viewport occupies all content area (no extra title row)
		QRect viewport(content_rect.x(), content_rect.y(), content_rect.width(), content_rect.height());

		// Draw buffer contents
		if (ed_ && viewport.height() > 0 && viewport.width() > 0) {
			const Buffer *buf = ed_->CurrentBuffer();
			if (buf) {
				const auto &lines         = buf->Rows();
				const std::size_t nrows   = lines.size();
				const std::size_t rowoffs = buf->Rowoffs();
				const std::size_t coloffs = buf->Coloffs();
				const std::size_t cy      = buf->Cury();
				const std::size_t cx      = buf->Curx();

				// Visible line count
				const int max_lines        = (line_h > 0) ? (viewport.height() / line_h) : 0;
				const std::size_t last_row = std::min<std::size_t>(
					nrows, rowoffs + std::max(0, max_lines));

				// Tab width: follow ImGuiRenderer default of 4
				const std::size_t tabw = 4;

				// Prepare painter clip to viewport
				p.save();
				p.setClipRect(viewport);

    // Iterate visible lines
    for (std::size_t i = rowoffs, vis_idx = 0; i < last_row; ++i, ++vis_idx) {
        // Materialize the Buffer::Line into a std::string for
        // regex/iterator usage and general string ops.
        const std::string line = static_cast<std::string>(lines[i]);
					const int y        = viewport.y() + static_cast<int>(vis_idx) * line_h;
					const int baseline = y + fm.ascent();

					// Helper: convert src col -> rx with tab expansion
					auto src_to_rx_line = [&](std::size_t src_col) -> std::size_t {
						std::size_t rx = 0;
						for (std::size_t k = 0; k < src_col && k < line.size(); ++k) {
							rx += (line[k] == '\t') ? (tabw - (rx % tabw)) : 1;
						}
						return rx;
					};

					// Search-match background highlights first (under text)
					if (ed_->SearchActive() && !ed_->SearchQuery().empty()) {
						std::vector<std::pair<std::size_t, std::size_t> > hl_src_ranges;
						// Compute ranges per line (source indices)
						if (ed_->PromptActive() &&
						    (ed_->CurrentPromptKind() == Editor::PromptKind::RegexSearch ||
						     ed_->CurrentPromptKind() ==
						     Editor::PromptKind::RegexReplaceFind)) {
							try {
								std::regex rx(ed_->SearchQuery());
								for (auto it = std::sregex_iterator(
									     line.begin(), line.end(), rx);
								     it != std::sregex_iterator(); ++it) {
									const auto &m  = *it;
									std::size_t sx = static_cast<std::size_t>(m.
										position());
									std::size_t ex =
										sx + static_cast<std::size_t>(m.
											length());
									hl_src_ranges.emplace_back(sx, ex);
								}
							} catch (const std::regex_error &) {
								// Invalid regex: ignore, status line already shows errors
							}
						} else {
							const std::string &q = ed_->SearchQuery();
							if (!q.empty()) {
								std::size_t pos = 0;
								while ((pos = line.find(q, pos)) != std::string::npos) {
									hl_src_ranges.emplace_back(pos, pos + q.size());
									pos += q.size();
								}
							}
						}

						if (!hl_src_ranges.empty()) {
							const bool has_current =
								ed_->SearchMatchLen() > 0 && ed_->SearchMatchY() == i;
							const std::size_t cur_x = has_current ? ed_->SearchMatchX() : 0;
							const std::size_t cur_end = has_current
								? (ed_->SearchMatchX() + ed_->SearchMatchLen())
								: 0;
							for (const auto &rg: hl_src_ranges) {
								std::size_t sx   = rg.first, ex = rg.second;
								std::size_t rx_s = src_to_rx_line(sx);
								std::size_t rx_e = src_to_rx_line(ex);
								if (rx_e <= coloffs)
									continue; // fully left of view
								int vx0 = viewport.x() + static_cast<int>((
									          (rx_s > coloffs ? rx_s - coloffs : 0)
									          * ch_w));
								int vx1 = viewport.x() + static_cast<int>((
									          (rx_e - coloffs) * ch_w));
								QRect r(vx0, y, std::max(0, vx1 - vx0), line_h);
								if (r.width() <= 0)
									continue;
								bool is_current =
									has_current && sx == cur_x && ex == cur_end;
								QColor col = is_current
									             ? QColor(255, 220, 120, 140)
									             : QColor(200, 200, 0, 90);
								p.fillRect(r, col);
							}
						}
					}

					// Selection background (if active on this line)
					if (buf->MarkSet() && (
						    i == buf->MarkCury() || i == cy || (
							    i > std::min(buf->MarkCury(), cy) && i < std::max(
								    buf->MarkCury(), cy)))) {
						std::size_t sx = 0, ex = 0;
						if (buf->MarkCury() == i && cy == i) {
							sx = std::min(buf->MarkCurx(), cx);
							ex = std::max(buf->MarkCurx(), cx);
						} else if (i == buf->MarkCury()) {
							sx = buf->MarkCurx();
							ex = line.size();
						} else if (i == cy) {
							sx = 0;
							ex = cx;
						} else {
							sx = 0;
							ex = line.size();
						}
						std::size_t rx_s = src_to_rx_line(sx);
						std::size_t rx_e = src_to_rx_line(ex);
						if (rx_e > coloffs) {
							int vx0 = viewport.x() + static_cast<int>((rx_s > coloffs
								          ? rx_s - coloffs
								          : 0) * ch_w);
							int vx1 = viewport.x() + static_cast<int>(
								          (rx_e - coloffs) * ch_w);
							QRect sel_r(vx0, y, std::max(0, vx1 - vx0), line_h);
							if (sel_r.width() > 0)
								p.fillRect(sel_r, sel_bg);
						}
					}

					// Build expanded line (tabs -> spaces) for drawing
					std::string expanded;
					expanded.reserve(line.size() + 8);
					std::size_t rx_acc = 0;
					for (char c: line) {
						if (c == '\t') {
							std::size_t adv = (tabw - (rx_acc % tabw));
							expanded.append(adv, ' ');
							rx_acc += adv;
						} else {
							expanded.push_back(c);
							rx_acc += 1;
						}
					}

					// Syntax highlighting spans or plain text
					if (buf->SyntaxEnabled() && buf->Highlighter() && buf->Highlighter()->
					    HasHighlighter()) {
						kte::LineHighlight lh = buf->Highlighter()->GetLine(
							*buf, static_cast<int>(i), buf->Version());
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
								0, std::min(s_raw, (int) line_len)));
							std::size_t e = static_cast<std::size_t>(std::max(
								(int) s, std::min(e_raw, (int) line_len)));
							if (s < e)
								spans.push_back({s, e, sp.kind});
						}
						std::sort(spans.begin(), spans.end(),
						          [](const SSpan &a, const SSpan &b) {
							          return a.s < b.s;
						          });

						auto colorFor = [](kte::TokenKind k) -> QColor {
							// GUITheme provides colors via ImGui vector; avoid direct dependency types
							const auto v = kte::SyntaxInk(k);
							return QColor(int(v.x * 255.0f), int(v.y * 255.0f),
							              int(v.z * 255.0f), int(v.w * 255.0f));
						};

						// Helper to convert src col to expanded rx
						auto src_to_rx_full = [&](std::size_t sidx) -> std::size_t {
							std::size_t rx = 0;
							for (std::size_t k = 0; k < sidx && k < line.size(); ++k) {
								rx += (line[k] == '\t') ? (tabw - (rx % tabw)) : 1;
							}
							return rx;
						};

						if (spans.empty()) {
							// No highlight spans: draw the whole (visible) expanded line in default fg
							if (coloffs < expanded.size()) {
								const char *start =
									expanded.c_str() + static_cast<int>(coloffs);
								p.setPen(fg);
								p.drawText(viewport.x(), baseline,
								           QString::fromUtf8(start));
							}
						} else {
							// Draw colored spans
							for (const auto &sp: spans) {
								std::size_t rx_s = src_to_rx_full(sp.s);
								std::size_t rx_e = src_to_rx_full(sp.e);
								if (rx_e <= coloffs)
									continue; // left of viewport
								std::size_t draw_start = (rx_s > coloffs)
									? rx_s
									: coloffs;
								std::size_t draw_end = std::min<std::size_t>(
									rx_e, expanded.size());
								if (draw_end <= draw_start)
									continue;
								std::size_t screen_x = draw_start - coloffs;
								int px = viewport.x() + int(screen_x * ch_w);
								int len = int(draw_end - draw_start);
								p.setPen(colorFor(sp.k));
								p.drawText(px, baseline,
								           QString::fromUtf8(
									           expanded.c_str() + draw_start, len));
							}
						}
					} else {
						// Draw expanded text clipped by coloffs
						if (static_cast<std::size_t>(coloffs) < expanded.size()) {
							const char *start =
								expanded.c_str() + static_cast<int>(coloffs);
							p.setPen(fg);
							p.drawText(viewport.x(), baseline, QString::fromUtf8(start));
						}
					}

					// Cursor indicator on current line
					if (i == cy) {
						std::size_t rx_cur = src_to_rx_line(cx);
						if (rx_cur >= coloffs) {
							// Compute exact pixel x by measuring expanded substring [coloffs, rx_cur)
							std::size_t start = std::min<std::size_t>(
								coloffs, expanded.size());
							std::size_t end = std::min<
								std::size_t>(rx_cur, expanded.size());
							int px_advance = 0;
							if (end > start) {
								const QString sub = QString::fromUtf8(
									expanded.c_str() + start,
									static_cast<int>(end - start));
								px_advance = fm.horizontalAdvance(sub);
							}
							int x0 = viewport.x() + px_advance;
							QRect r(x0, y, ch_w, line_h);
							p.fillRect(r, cur_bg);
						}
					}
				}

				p.restore();
			}
		}

		// Status bar
		const int bar_y = height() - status_h;
		QRect status_rect(0, bar_y, width(), status_h);
		p.fillRect(status_rect, status_bg);
		p.setPen(status_fg);
		if (ed_) {
			const int pad         = 6;
			const int left_x      = status_rect.x() + pad;
			const int right_x_max = status_rect.x() + status_rect.width() - pad;
			const int baseline_y  = bar_y + (status_h + fm.ascent() - fm.descent()) / 2;

			// If a prompt is active, mirror ImGui/TUI: show only the prompt across the bar
			if (ed_->PromptActive()) {
				std::string label = ed_->PromptLabel();
				std::string text  = ed_->PromptText();

				// Map $HOME to ~ for path prompts (Open/Save/Chdir)
				auto kind = ed_->CurrentPromptKind();
				if (kind == Editor::PromptKind::OpenFile ||
				    kind == Editor::PromptKind::SaveAs ||
				    kind == Editor::PromptKind::Chdir) {
					const char *home_c = std::getenv("HOME");
					if (home_c && *home_c) {
						std::string home(home_c);
						if (text.rfind(home, 0) == 0) {
							std::string rest = text.substr(home.size());
							if (rest.empty())
								text = "~";
							else if (!rest.empty() && (rest[0] == '/' || rest[0] == '\\'))
								text = std::string("~") + rest;
						}
					}
				}

				std::string prefix;
				if (kind == Editor::PromptKind::Command)
					prefix = ": ";
				else if (!label.empty())
					prefix = label + ": ";

				// Compose text and elide per behavior:
				const int max_w        = status_rect.width() - 2 * pad;
				QString qprefix        = QString::fromStdString(prefix);
				QString qtext          = QString::fromStdString(text);
				int avail_w            = std::max(0, max_w - fm.horizontalAdvance(qprefix));
				Qt::TextElideMode mode = Qt::ElideRight;
				if (kind == Editor::PromptKind::OpenFile ||
				    kind == Editor::PromptKind::SaveAs ||
				    kind == Editor::PromptKind::Chdir) {
					mode = Qt::ElideLeft;
				}
				QString shown = fm.elidedText(qtext, mode, avail_w);
				p.drawText(left_x, baseline_y, qprefix + shown);
			} else {
				// Build left segment: app/version, buffer idx/total, filename [+dirty], line count
				QString left;
				left += QStringLiteral("kge ");
				left += QStringLiteral(KTE_VERSION_STR);

				const Buffer *buf = ed_->CurrentBuffer();
				if (buf) {
					// buffer index/total
					std::size_t total = ed_->BufferCount();
					if (total > 0) {
						std::size_t idx1 = ed_->CurrentBufferIndex() + 1; // 1-based
						left += QStringLiteral("  [");
						left += QString::number(static_cast<qlonglong>(idx1));
						left += QStringLiteral("/");
						left += QString::number(static_cast<qlonglong>(total));
						left += QStringLiteral("] ");
					} else {
						left += QStringLiteral("  ");
					}

					// buffer display name
					std::string disp;
					try {
						disp = ed_->DisplayNameFor(*buf);
					} catch (...) {
						disp = buf->Filename();
					}
					if (disp.empty())
						disp = "[No Name]";
					left += QString::fromStdString(disp);
					if (buf->Dirty())
						left += QStringLiteral(" *");

					// total lines suffix " <n>L"
					unsigned long lcount = static_cast<unsigned long>(buf->Rows().size());
					left += QStringLiteral(" ");
					left += QString::number(static_cast<qlonglong>(lcount));
					left += QStringLiteral("L");
				}

				// Build right segment: cursor and mark
				QString right;
				if (buf) {
					int row1       = static_cast<int>(buf->Cury()) + 1;
					int col1       = static_cast<int>(buf->Curx()) + 1;
					bool have_mark = buf->MarkSet();
					int mrow1      = have_mark ? static_cast<int>(buf->MarkCury()) + 1 : 0;
					int mcol1      = have_mark ? static_cast<int>(buf->MarkCurx()) + 1 : 0;
					if (have_mark)
						right = QString("%1,%2 | M: %3,%4").arg(row1).arg(col1).arg(mrow1).arg(
							mcol1);
					else
						right = QString("%1,%2 | M: not set").arg(row1).arg(col1);
				}

				// Middle message: status text
				QString mid = QString::fromStdString(ed_->Status());

				// Measure and layout
				int left_w  = fm.horizontalAdvance(left);
				int right_w = fm.horizontalAdvance(right);
				int lx      = left_x;
				int rx      = std::max(left_x, right_x_max - right_w);

				// If overlap, elide left to make space for right
				if (lx + left_w + pad > rx) {
					int max_left_w = std::max(0, rx - lx - pad);
					left           = fm.elidedText(left, Qt::ElideRight, max_left_w);
					left_w         = fm.horizontalAdvance(left);
				}

				// Draw left and right
				p.drawText(lx, baseline_y, left);
				if (!right.isEmpty())
					p.drawText(rx, baseline_y, right);

				// Middle message clipped between end of left and start of right
				int mid_left  = lx + left_w + pad;
				int mid_right = std::max(mid_left, rx - pad);
				int mid_w     = std::max(0, mid_right - mid_left);
				if (mid_w > 0 && !mid.isEmpty()) {
					QString mid_show = fm.elidedText(mid, Qt::ElideRight, mid_w);
					p.save();
					p.setClipRect(QRect(mid_left, bar_y, mid_w, status_h));
					p.drawText(mid_left, baseline_y, mid_show);
					p.restore();
				}
			}
		}
	}


	void resizeEvent(QResizeEvent *event) override
	{
		QWidget::resizeEvent(event);
		if (!ed_)
			return;
		// Update editor dimensions based on new size
		QFont f(font_family_, font_px_);
		QFontMetrics fm(f);
		const int line_h   = std::max(12, fm.height());
		const int ch_w     = std::max(6, fm.horizontalAdvance(QStringLiteral(" ")));
		const int pad_l    = 8, pad_r = 8, pad_t = 6, pad_b = 6;
		const int status_h = line_h + 6;
		const int avail_w  = std::max(0, width() - pad_l - pad_r);
		const int avail_h  = std::max(0, height() - pad_t - pad_b - status_h);
		std::size_t rows   = std::max<std::size_t>(1, (avail_h / line_h));
		std::size_t cols   = std::max<std::size_t>(1, (avail_w / ch_w));
		ed_->SetDimensions(rows, cols);
	}


	void wheelEvent(QWheelEvent *event) override
	{
		if (!ed_) {
			QWidget::wheelEvent(event);
			return;
		}
		Buffer *buf = ed_->CurrentBuffer();
		if (!buf) {
			QWidget::wheelEvent(event);
			return;
		}

		// Recompute metrics to map pixel deltas to rows/cols
		QFont f(font_family_, font_px_);
		QFontMetrics fm(f);
		const int line_h = std::max(12, fm.height());
		const int ch_w   = std::max(6, fm.horizontalAdvance(QStringLiteral(" ")));

		// Determine scroll intent: use pixelDelta when available (trackpads), otherwise angleDelta
		QPoint pixel = event->pixelDelta();
		QPoint angle = event->angleDelta();

		double v_lines_delta = 0.0;
		double h_cols_delta  = 0.0;

		// Horizontal scroll with Shift or explicit horizontal delta
		bool horiz_mode = (event->modifiers() & Qt::ShiftModifier) || (!pixel.isNull() && pixel.x() != 0) || (
			                  !angle.isNull() && angle.x() != 0);

		if (!pixel.isNull()) {
			// Trackpad smooth scrolling (pixels)
			v_lines_delta = -static_cast<double>(pixel.y()) / std::max(1, line_h);
			h_cols_delta  = -static_cast<double>(pixel.x()) / std::max(1, ch_w);
		} else if (!angle.isNull()) {
			// Mouse wheel: 120 units per notch; map one notch to 3 lines similar to ImGui UX
			v_lines_delta = -static_cast<double>(angle.y()) / 120.0 * 3.0;
			// For horizontal wheels, each notch scrolls 8 columns
			h_cols_delta = -static_cast<double>(angle.x()) / 120.0 * 8.0;
		}

		// Accumulate fractional deltas across events
		v_scroll_accum_ += v_lines_delta;
		h_scroll_accum_ += h_cols_delta;

		int d_rows = 0;
		int d_cols = 0;
		if (std::fabs(v_scroll_accum_) >= 1.0 && (!horiz_mode || std::fabs(v_scroll_accum_) > std::fabs(
			                                          h_scroll_accum_))) {
			d_rows = static_cast<int>(v_scroll_accum_);
			v_scroll_accum_ -= d_rows;
		}
		if (std::fabs(h_scroll_accum_) >= 1.0 && (horiz_mode || std::fabs(h_scroll_accum_) >= std::fabs(
			                                          v_scroll_accum_))) {
			d_cols = static_cast<int>(h_scroll_accum_);
			h_scroll_accum_ -= d_cols;
		}

		if (d_rows != 0 || d_cols != 0) {
			std::size_t new_rowoffs = buf->Rowoffs();
			std::size_t new_coloffs = buf->Coloffs();
			// Clamp vertical between 0 and last row (leaving at least one visible line)
			if (d_rows != 0) {
				long nr = static_cast<long>(new_rowoffs) + d_rows;
				if (nr < 0)
					nr = 0;
				const auto nrows = static_cast<long>(buf->Rows().size());
				if (nr > std::max(0L, nrows - 1))
					nr = std::max(0L, nrows - 1);
				new_rowoffs = static_cast<std::size_t>(nr);
			}
			if (d_cols != 0) {
				long nc = static_cast<long>(new_coloffs) + d_cols;
				if (nc < 0)
					nc = 0;
				new_coloffs = static_cast<std::size_t>(nc);
			}
			buf->SetOffsets(new_rowoffs, new_coloffs);
			update();
			event->accept();
			return;
		}

		QWidget::wheelEvent(event);
	}


	void closeEvent(QCloseEvent *event) override
	{
		closed_ = true;
		QWidget::closeEvent(event);
	}

private:
	QtInputHandler &input_;
	bool closed_           = false;
	Editor *ed_            = nullptr;
	double v_scroll_accum_ = 0.0;
	double h_scroll_accum_ = 0.0;
	QString font_family_   = QStringLiteral("Brass Mono");
	int font_px_           = 18;
};
} // namespace

bool
GUIFrontend::Init(Editor &ed)
{
	int argc    = 0;
	char **argv = nullptr;
	app_        = new QApplication(argc, argv);

	window_ = new MainWindow(input_);
	window_->show();
	// Ensure the window becomes the active, focused window so it receives key events
	window_->activateWindow();
	window_->raise();
	window_->setFocus(Qt::OtherFocusReason);

	renderer_.Attach(window_);
	input_.Attach(&ed);
	if (auto *mw = dynamic_cast<MainWindow *>(window_))
		mw->SetEditor(&ed);

	// Load GUI configuration (kge.ini) and configure font for Qt
	config_ = GUIConfig::Load();

	// Apply background mode from config to match ImGui frontend behavior
	if (config_.background == "light")
		kte::SetBackgroundMode(kte::BackgroundMode::Light);
	else
		kte::SetBackgroundMode(kte::BackgroundMode::Dark);

	// Apply theme by name for Qt palette-based theming (maps to named palettes).
	// If unknown, falls back to the generic light/dark palette.
	(void) kte::ApplyQtThemeByName(config_.theme);
	if (window_)
		window_->update();

	// Map GUIConfig font name to a system family (Qt uses installed fonts)
	auto choose_family = [](const std::string &name) -> QString {
		QString fam;
		std::string n = name;
		std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) {
			return (char) std::tolower(c);
		});
		if (n.empty() || n == "default" || n == "brassmono" || n == "brassmonocode") {
			fam = QStringLiteral("Brass Mono");
		} else if (n == "jetbrains" || n == "jetbrains mono" || n == "jetbrains-mono") {
			fam = QStringLiteral("JetBrains Mono");
		} else if (n == "iosevka") {
			fam = QStringLiteral("Iosevka");
		} else if (n == "inconsolata" || n == "inconsolataex") {
			fam = QStringLiteral("Inconsolata");
		} else if (n == "space" || n == "spacemono" || n == "space mono") {
			fam = QStringLiteral("Space Mono");
		} else if (n == "go") {
			fam = QStringLiteral("Go Mono");
		} else if (n == "ibm" || n == "ibm plex mono" || n == "ibm-plex-mono") {
			fam = QStringLiteral("IBM Plex Mono");
		} else if (n == "fira" || n == "fira code" || n == "fira-code") {
			fam = QStringLiteral("Fira Code");
		} else if (!name.empty()) {
			fam = QString::fromStdString(name);
		}

		// Validate availability; choose a fallback if needed
		const auto families = QFontDatabase::families();
		if (!fam.isEmpty() && families.contains(fam)) {
			return fam;
		}
		// Preferred fallback chain on macOS; otherwise, try common monospace families
		const QStringList fallbacks = {
			QStringLiteral("Brass Mono"),
			QStringLiteral("JetBrains Mono"),
			QStringLiteral("SF Mono"),
			QStringLiteral("Menlo"),
			QStringLiteral("Monaco"),
			QStringLiteral("Courier New"),
			QStringLiteral("Courier"),
			QStringLiteral("Monospace")
		};
		for (const auto &fb: fallbacks) {
			if (families.contains(fb))
				return fb;
		}
		// As a last resort, return the request (Qt will substitute)
		return fam.isEmpty() ? QStringLiteral("Monospace") : fam;
	};

	QString family = choose_family(config_.font);
	int px_size    = (config_.font_size > 0.0f) ? (int) std::lround(config_.font_size) : 18;
	if (auto *mw = dynamic_cast<MainWindow *>(window_)) {
		mw->SetFontFamilyAndSize(family, px_size);
	}
	// Track current font in globals for command/status queries
	kte::gCurrentFontFamily = family.toStdString();
	kte::gCurrentFontSize   = static_cast<float>(px_size);

	// Set initial dimensions based on font metrics
	QFont f(family, px_size);
	QFontMetrics fm(f);
	const int line_h   = std::max(12, fm.height());
	const int ch_w     = std::max(6, fm.horizontalAdvance(QStringLiteral("M")));
	const int w        = window_->width();
	const int h        = window_->height();
	const int pad      = 16;
	const int status_h = line_h + 4;
	const int avail_w  = std::max(0, w - 2 * pad);
	const int avail_h  = std::max(0, h - 2 * pad - status_h);
	std::size_t rows   = std::max<std::size_t>(1, (avail_h / line_h) + 1); // + status
	std::size_t cols   = std::max<std::size_t>(1, (avail_w / ch_w));
	ed.SetDimensions(rows, cols);

	return true;
}


void
GUIFrontend::Step(Editor &ed, bool &running)
{
	// Pump Qt events
	if (app_)
		app_->processEvents();

	// Drain input queue
	for (;;) {
		MappedInput mi;
		if (!input_.Poll(mi))
			break;
		if (mi.hasCommand) {
			Execute(ed, mi.id, mi.arg, mi.count);
		}
	}

	if (ed.QuitRequested()) {
		running = false;
	}

	// --- Visual File Picker (Qt): invoked via CommandId::VisualFilePickerToggle ---
	if (ed.FilePickerVisible()) {
		QString startDir;
		if (!ed.FilePickerDir().empty()) {
			startDir = QString::fromStdString(ed.FilePickerDir());
		}
		QFileDialog dlg(window_, QStringLiteral("Open File"), startDir);
		dlg.setFileMode(QFileDialog::ExistingFile);
		if (dlg.exec() == QDialog::Accepted) {
			const QStringList files = dlg.selectedFiles();
			if (!files.isEmpty()) {
				const QString fp = files.front();
				std::string err;
				if (ed.OpenFile(fp.toStdString(), err)) {
					ed.SetStatus(std::string("Opened: ") + fp.toStdString());
				} else if (!err.empty()) {
					ed.SetStatus(std::string("Open failed: ") + err);
				} else {
					ed.SetStatus("Open failed");
				}
				// Update picker dir for next time
				QFileInfo info(fp);
				ed.SetFilePickerDir(info.dir().absolutePath().toStdString());
			}
		}
		// Close picker overlay regardless of outcome
		ed.SetFilePickerVisible(false);
		if (window_)
			window_->update();
	}

	// Apply any queued theme change requests (from command handler)
	if (kte::gThemeChangePending) {
		if (!kte::gThemeChangeRequest.empty()) {
			// Apply Qt palette theme by name; if unknown, keep current palette
			(void) kte::ApplyQtThemeByName(kte::gThemeChangeRequest);
		}
		kte::gThemeChangePending = false;
		kte::gThemeChangeRequest.clear();
		if (window_)
			window_->update();
	}

	// Visual font picker request (Qt only)
	if (kte::gFontDialogRequested) {
		// Seed initial font from current or default
		QFont seed;
		if (!kte::gCurrentFontFamily.empty()) {
			seed = QFont(QString::fromStdString(kte::gCurrentFontFamily),
			             (int) std::lround(kte::gCurrentFontSize > 0 ? kte::gCurrentFontSize : 18));
		} else {
			seed = window_ ? window_->font() : QFont();
		}
		bool ok            = false;
		const QFont chosen = QFontDialog::getFont(&ok, seed, window_, QStringLiteral("Choose Editor Font"));
		if (ok) {
			// Queue font change via existing hooks
			kte::gFontFamilyRequest = chosen.family().toStdString();
			// Use pixel size if available, otherwise convert from point size approximately
			int px = chosen.pixelSize();
			if (px <= 0) {
				// Approximate points to pixels (96 DPI assumption); Qt will rasterize appropriately
				px = (int) std::lround(chosen.pointSizeF() * 96.0 / 72.0);
				if (px <= 0)
					px = 18;
			}
			kte::gFontSizeRequest   = static_cast<float>(px);
			kte::gFontChangePending = true;
		}
		kte::gFontDialogRequested = false;
		if (window_)
			window_->update();
	}

	// Apply any queued font change requests (Qt)
	if (kte::gFontChangePending) {
		// Derive target family
		auto map_family = [](const std::string &name) -> QString {
			std::string n = name;
			std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) {
				return (char) std::tolower(c);
			});
			QString fam;
			if (n == "brass" || n == "brassmono" || n == "brass mono") {
				fam = QStringLiteral("Brass Mono");
			} else if (n == "jetbrains" || n == "jetbrains mono" || n == "jetbrains-mono") {
				fam = QStringLiteral("JetBrains Mono");
			} else if (n == "iosevka") {
				fam = QStringLiteral("Iosevka");
			} else if (n == "inconsolata" || n == "inconsolataex") {
				fam = QStringLiteral("Inconsolata");
			} else if (n == "space" || n == "spacemono" || n == "space mono") {
				fam = QStringLiteral("Space Mono");
			} else if (n == "go") {
				fam = QStringLiteral("Go Mono");
			} else if (n == "ibm" || n == "ibm plex mono" || n == "ibm-plex-mono") {
				fam = QStringLiteral("IBM Plex Mono");
			} else if (n == "fira" || n == "fira code" || n == "fira-code") {
				fam = QStringLiteral("Fira Code");
			} else if (!name.empty()) {
				fam = QString::fromStdString(name);
			}
			// Validate availability; choose fallback if needed
			const auto families = QFontDatabase::families();
			if (!fam.isEmpty() && families.contains(fam)) {
				return fam;
			}
			// Fallback chain
			const QStringList fallbacks = {
				QStringLiteral("Brass Mono"),
				QStringLiteral("JetBrains Mono"),
				QStringLiteral("SF Mono"),
				QStringLiteral("Menlo"),
				QStringLiteral("Monaco"),
				QStringLiteral("Courier New"),
				QStringLiteral("Courier"),
				QStringLiteral("Monospace")
			};
			for (const auto &fb: fallbacks) {
				if (families.contains(fb))
					return fb;
			}
			return fam.isEmpty() ? QStringLiteral("Monospace") : fam;
		};

		QString target_family;
		if (!kte::gFontFamilyRequest.empty()) {
			target_family = map_family(kte::gFontFamilyRequest);
		} else if (!kte::gCurrentFontFamily.empty()) {
			target_family = QString::fromStdString(kte::gCurrentFontFamily);
		}
		int target_px = 0;
		if (kte::gFontSizeRequest > 0.0f) {
			target_px = (int) std::lround(kte::gFontSizeRequest);
		} else if (kte::gCurrentFontSize > 0.0f) {
			target_px = (int) std::lround(kte::gCurrentFontSize);
		}
		if (target_px <= 0)
			target_px = 18;
		if (target_family.isEmpty())
			target_family = QStringLiteral("Monospace");

		if (auto *mw = dynamic_cast<MainWindow *>(window_)) {
			mw->SetFontFamilyAndSize(target_family, target_px);
		}
		// Update globals
		kte::gCurrentFontFamily = target_family.toStdString();
		kte::gCurrentFontSize   = static_cast<float>(target_px);
		// Reset requests
		kte::gFontChangePending = false;
		kte::gFontFamilyRequest.clear();
		kte::gFontSizeRequest = 0.0f;

		// Recompute editor dimensions to match new metrics
		QFont f(target_family, target_px);
		QFontMetrics fm(f);
		const int line_h   = std::max(12, fm.height());
		const int ch_w     = std::max(6, fm.horizontalAdvance(QStringLiteral("M")));
		const int w        = window_ ? window_->width() : 0;
		const int h        = window_ ? window_->height() : 0;
		const int pad      = 16;
		const int status_h = line_h + 4;
		const int avail_w  = std::max(0, w - 2 * pad);
		const int avail_h  = std::max(0, h - 2 * pad - status_h);
		std::size_t rows   = std::max<std::size_t>(1, (avail_h / line_h) + 1); // + status
		std::size_t cols   = std::max<std::size_t>(1, (avail_w / ch_w));
		ed.SetDimensions(rows, cols);

		if (window_)
			window_->update();
	}

	// Draw current frame (request repaint)
	renderer_.Draw(ed);

	// Detect window close
	if (auto *mw = dynamic_cast<MainWindow *>(window_)) {
		if (mw->WasClosed()) {
			running = false;
		}
	}
}


void
GUIFrontend::Shutdown()
{
	if (window_) {
		window_->close();
		delete window_;
		window_ = nullptr;
	}
	if (app_) {
		delete app_;
		app_ = nullptr;
	}
}