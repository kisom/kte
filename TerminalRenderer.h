/*
 * TerminalRenderer - ncurses-based renderer for terminal mode
 */
#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include "Renderer.h"


class TerminalRenderer final : public Renderer {
public:
	TerminalRenderer();

	~TerminalRenderer() override;

	void Draw(Editor &ed) override;

private:
	// Where a row's visible part starts (the draw loop's skip up to the
	// horizontal scroll offset), cached per screen row. Scanning a very long
	// line from column 0 on every frame made each redraw O(line length).
	struct SkipCache {
		const void *buf       = nullptr;
		std::uint64_t version = 0;
		std::size_t row       = 0;
		std::size_t coloffs   = 0;
		std::size_t src       = 0; // byte offset where drawing resumes
		std::size_t col       = 0; // render column at that offset
		bool valid            = false;
	};

	std::vector<SkipCache> skip_cache_;

	// Render column of the cursor, cached the same way.
	struct CursorCache {
		const void *buf       = nullptr;
		std::uint64_t version = 0;
		std::size_t row       = 0;
		std::size_t cx        = 0;
		std::size_t rx        = 0;
		bool valid            = false;
	} cursor_cache_;
};
