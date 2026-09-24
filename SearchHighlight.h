/*
 * SearchHighlight.h - the search-match ranges every renderer highlights.
 *
 * Built once per frame from the editor's search state (the regex, when the
 * regex search or regex replace prompt is up, is compiled here once rather
 * than per line), then asked for each visible line's matches.
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "RegexEngine.h"

class Editor;

namespace kte {
class SearchHighlight {
public:
	explicit SearchHighlight(const Editor &ed);

	// Whether a search is up with a non-empty query.
	[[nodiscard]] bool Active() const
	{
		return active_;
	}

	// The matches in one line as [start, end) byte ranges, ordered and not
	// overlapping. Nothing for an invalid pattern (the status line shows the
	// error) or, in regex mode, a line longer than
	// Regex::IncrementalLineLimit().
	void Ranges(std::string_view line, std::vector<std::pair<std::size_t, std::size_t> > &out) const;

private:
	bool active_   = false;
	bool regex_    = false;
	bool rx_valid_ = false;
	std::string query_;
	Regex rx_;
};
} // namespace kte
