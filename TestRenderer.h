/*
 * TestRenderer.h - minimal renderer for testing (no actual display)
 */
#pragma once
#include <cstddef>

#include "Renderer.h"


class TestRenderer final : public Renderer {
public:
	TestRenderer() = default;

	~TestRenderer() override = default;

	void Draw(Editor &ed) override;


	[[nodiscard]] std::size_t GetDrawCount() const
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