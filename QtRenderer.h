/*
 * QtRenderer - minimal Qt-based renderer
 */
#pragma once

#include "Renderer.h"

class QWidget;

class QtRenderer final : public Renderer {
public:
	QtRenderer() = default;

	~QtRenderer() override = default;


	void Attach(QWidget *widget)
	{
		widget_ = widget;
	}


	void Draw(Editor &ed) override;

private:
	QWidget *widget_ = nullptr; // not owned
};