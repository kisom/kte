#include "QtRenderer.h"

#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QFont>
#include <QFontMetrics>

#include "Editor.h"

namespace {
class EditorWidget : public QWidget {
public:
	explicit EditorWidget(QWidget *parent = nullptr) : QWidget(parent)
	{
		setAttribute(Qt::WA_OpaquePaintEvent);
		setFocusPolicy(Qt::StrongFocus);
	}


	void SetEditor(Editor *ed)
	{
		ed_ = ed;
	}

protected:
	void paintEvent(QPaintEvent *event) override
	{
		Q_UNUSED(event);
		QPainter p(this);
		// Background
		const QColor bg(28, 28, 30);
		p.fillRect(rect(), bg);

		// Font and metrics
		QFont f("JetBrains Mono", 13);
		p.setFont(f);
		QFontMetrics fm(f);
		const int line_h = fm.height();

		// Title
		p.setPen(QColor(220, 220, 220));
		p.drawText(8, fm.ascent() + 4, QStringLiteral("kte (Qt frontend)"));

		// Status bar at bottom
		const int bar_h = line_h + 6; // padding
		const int bar_y = height() - bar_h;
		QRect status_rect(0, bar_y, width(), bar_h);
		p.fillRect(status_rect, QColor(40, 40, 44));
		p.setPen(QColor(180, 180, 140));
		if (ed_) {
			const QString status = QString::fromStdString(ed_->Status());
			// draw at baseline within the bar
			const int baseline = bar_y + 3 + fm.ascent();
			p.drawText(8, baseline, status);
		}
	}

private:
	Editor *ed_ = nullptr;
};
} // namespace

void
QtRenderer::Draw(Editor &ed)
{
	if (!widget_)
		return;

	// If our widget is an EditorWidget, pass the editor pointer for painting
	if (auto *ew = dynamic_cast<EditorWidget *>(widget_)) {
		ew->SetEditor(&ed);
	}
	// Request a repaint
	widget_->update();
}