/*
 * TestFrontend.h - headless frontend for testing with programmable input
 */
#ifndef KTE_TEST_FRONTEND_H
#define KTE_TEST_FRONTEND_H

#include "Frontend.h"
#include "TestInputHandler.h"
#include "TestRenderer.h"


class TestFrontend final : public Frontend {
public:
	TestFrontend() = default;

	~TestFrontend() override = default;

	bool Init(Editor &ed) override;

	void Step(Editor &ed, bool &running) override;

	void Shutdown() override;


	TestInputHandler &Input()
	{
		return input_;
	}


	TestRenderer &Renderer()
	{
		return renderer_;
	}

private:
	TestInputHandler input_{};
	TestRenderer renderer_{};
};

#endif // KTE_TEST_FRONTEND_H
