#include "TreeSitterHighlighter.h"

#ifdef KTE_ENABLE_TREESITTER

#include "Buffer.h"
#include <utility>

namespace kte {
TreeSitterHighlighter::TreeSitterHighlighter(const TSLanguage *lang, std::string filetype)
	: language_(lang), filetype_(std::move(filetype)) {}


TreeSitterHighlighter::~TreeSitterHighlighter()
{
	disposeParser();
}


void
TreeSitterHighlighter::ensureParsed(const Buffer & /*buf*/) const
{
	// Intentionally a stub to avoid pulling the Tree-sitter API and library by default.
	// In future, when linking against tree-sitter, initialize parser_, set language_,
	// and build tree_ from the buffer contents.
}


void
TreeSitterHighlighter::disposeParser() const
{
	// Stub; nothing to dispose when not actually creating parser/tree
}


void
TreeSitterHighlighter::HighlightLine(const Buffer &/*buf*/, int /*row*/, std::vector<HighlightSpan> &/*out*/) const
{
	// For now, no-op. When tree-sitter is wired, map nodes to TokenKind spans per line.
}


std::unique_ptr<LanguageHighlighter>
CreateTreeSitterHighlighter(const char *filetype,
                            const void * (*get_lang)())
{
	const auto *lang = reinterpret_cast<const TSLanguage *>(get_lang ? get_lang() : nullptr);
	return std::make_unique < TreeSitterHighlighter > (lang, filetype ? std::string(filetype) : std::string());
}
} // namespace kte

#endif // KTE_ENABLE_TREESITTER