#include "NullHighlighter.h"
#include "Buffer.h"

namespace kte {
void
NullHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
	const auto &rows = buf.Rows();
	if (row < 0 || static_cast<std::size_t>(row) >= rows.size())
		return;
	std::string s = static_cast<std::string>(rows[static_cast<std::size_t>(row)]);
	int n         = static_cast<int>(s.size());
	if (n <= 0)
		return;
	out.push_back({0, n, TokenKind::Default});
}
} // namespace kte