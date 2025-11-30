/*
 * TestRenderer.h - minimal renderer for testing (no actual display)
 */
#ifndef KTE_TEST_RENDERER_H
#define KTE_TEST_RENDERER_H

#include "Renderer.h"
#include <cstddef>


class TestRenderer : public Renderer {
public:
	TestRenderer() = default;

	~TestRenderer() override = default;

	void Draw(Editor &ed) override;


	std::size_t GetDrawCount() const
	{
		return draw_count_;
	}


	void ResetDrawCount()
	{
		draw_count_ = 0;
	}

private:
	std::size_t draw_count_ = 0;
};

#endif // KTE_TEST_RENDERER_H
