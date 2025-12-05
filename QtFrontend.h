/*
 * QtFrontend - couples QtInputHandler + QtRenderer and owns Qt lifecycle
 */
#pragma once

#include "Frontend.h"
#include "GUIConfig.h"
#include "QtInputHandler.h"
#include "QtRenderer.h"

class QApplication;
class QWidget;

// Keep the public class name GUIFrontend to match main.cc selection logic.
class GUIFrontend final : public Frontend {
public:
	GUIFrontend() = default;

	~GUIFrontend() override = default;

	bool Init(Editor &ed) override;

	void Step(Editor &ed, bool &running) override;

	void Shutdown() override;

private:
	GUIConfig config_{};
	QtInputHandler input_{};
	QtRenderer renderer_{};

	QApplication *app_ = nullptr; // owned
	QWidget *window_   = nullptr; // owned
	int width_         = 1280;
	int height_        = 800;
};