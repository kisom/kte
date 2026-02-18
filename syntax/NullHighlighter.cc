#include "NullHighlighter.h"
#include "../Buffer.h"

namespace kte {
void
NullHighlighter::HighlightLine(const Buffer &buf, int row, std::vector<HighlightSpan> &out) const
{
	if (row < 0 || static_cast<std::size_t>(row) >= buf.Nrows())
		return;
	std::string s = buf.GetLineString(static_cast<std::size_t>(row));
	int n         = static_cast<int>(s.size());
	if (n <= 0)
		return;
	out.push_back({0, n, TokenKind::Default});
}
} // namespace kte