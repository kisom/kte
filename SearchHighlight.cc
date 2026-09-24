#include "SearchHighlight.h"

#include <algorithm>

#include "Editor.h"

namespace kte {
SearchHighlight::SearchHighlight(const Editor &ed)
{
	active_ = ed.SearchActive() && !ed.SearchQuery().empty();
	if (!active_)
		return;
	query_ = ed.SearchQuery();
	regex_ = ed.PromptActive() && (ed.CurrentPromptKind() == Editor::PromptKind::RegexSearch ||
	                               ed.CurrentPromptKind() == Editor::PromptKind::RegexReplaceFind);
	if (regex_) {
		std::string err;
		rx_valid_ = rx_.Compile(query_, err);
	}
}


void
SearchHighlight::Ranges(std::string_view line, std::vector<std::pair<std::size_t, std::size_t> > &out) const
{
	out.clear();
	if (!active_)
		return;
	if (regex_) {
		if (!rx_valid_ || line.size() > Regex::IncrementalLineLimit())
			return;
		std::size_t from = 0, pos = 0, len = 0;
		while (from <= line.size() && rx_.Search(line, from, pos, len)) {
			out.emplace_back(pos, pos + len);
			from = pos + std::max<std::size_t>(len, 1);
		}
		return;
	}
	std::size_t pos = 0;
	while ((pos = line.find(query_, pos)) != std::string_view::npos) {
		out.emplace_back(pos, pos + query_.size());
		pos += query_.size();
	}
}
} // namespace kte
